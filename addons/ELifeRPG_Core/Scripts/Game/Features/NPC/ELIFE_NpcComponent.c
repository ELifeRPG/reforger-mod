//------------------------------------------------------------------------------------------------
[EntityEditorProps(category: "ELifeRPG/NPC", description: "An NPC who serves one customer at a time: visits, idle, facing, gestures and voice")]
class ELIFE_NpcComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! Why the NPC refused. An NPC's own reason enum starts with these same values and adds its own after them.
enum ELIFE_ENpcReason
{
	NONE,
	BUSY,
	OUT_OF_RANGE,
	//! He can't serve at all right now (backend down, nothing set up).
	UNAVAILABLE,
	//! Outside his opening hours (ELIFE_NpcSchedule), or not back at his spot yet.
	CLOSED
}

//------------------------------------------------------------------------------------------------
//! The generic core of a service NPC (see docs/npc-dialogue.md). The server owns him; customers reach him through
//! ELIFE_NpcCharacterComponent on their own character. A kind of NPC subclasses this and overrides the hooks:
//! IsReady/Prepare, CheckCustomer, OnVisitStart/OnVisitEnd, OnRequest (server) and OnMessage/OnGranted (client).
class ELIFE_NpcComponent : ScriptComponent
{
	protected static const int TICK_INTERVAL_MS = 1000;
	//! Same as the ATM: without input for this long, the customer has walked away from the keyboard.
	protected static const int AFK_TIMEOUT_MS = 300000;
	//! Before the idle starts: after spawning (the controller isn't ready to animate yet) and after a customer leaves.
	protected static const int IDLE_START_DELAY_MS = 1000;
	//! The idle's normal exit can wait forever for an animation tag the server may not read; it's closed after this.
	protected static const int IDLE_EXIT_TIMEOUT_MS = 2000;
	//! Clients: how often an idle that is still starting is checked again.
	protected static const int PROXY_IDLE_RETRY_MS = 100;
	//! Server-side: how often he updates his heading while turning.
	protected static const int ATTENTION_TICK_MS = 50;
	//! Once he faces his target this closely he stops turning.
	protected static const float ATTENTION_MIN_TURN_DEG = 2;
	//! A heading ~180 degrees away makes the turn animation overshoot the long way round.
	protected static const float HEADING_LEAD_DEG = 90;
	//! The turn animation has a dead zone, so back at his spot he settles within this.
	protected static const float HOME_SETTLE_DEG = 20;
	protected static const int HOME_SETTLE_MS = 500;
	protected static const int GESTURE_DURATION_MS = 2000;
	//! After the line starts: a gesture started in the same frame as stopping his idle (the greeting) was swallowed.
	protected static const int GESTURE_DELAY_MS = 300;
	//! Lines the customer asks for are at least this far apart; one asked sooner waits, and a newer one replaces it.
	protected static const int ASKED_LINE_COOLDOWN_MS = 500;
	//! How often the server compares the in-game clock with his opening hours.
	protected static const int SCHEDULE_CHECK_MS = 5000;

	[Attribute("2.5", UIWidgets.Slider, "How far from him a customer can stand, in metres", "0.5 10 0.1")]
	protected float m_fUseRange;

	[Attribute("", UIWidgets.ResourceNamePicker, "What he can say, an ELIFE_NpcVoiceTable", "conf")]
	protected ResourceName m_sVoiceTable;

	[Attribute("", UIWidgets.ResourceNamePicker, "The conversation with him, an ELIFE_DialogueConfig", "conf")]
	protected ResourceName m_sDialogueConfig;

	[Attribute("", UIWidgets.EditBox, "His name above subtitles and on hints")]
	protected string m_sDisplayName;

	[Attribute(desc: "Optional opening hours. Without them he is always open; with them he leaves his spot when closed (ELIFE_NpcRoutine). A spot can override them.")]
	protected ref ELIFE_NpcSchedule m_Schedule;

	[RplProp()]
	protected bool m_bClosed;
	//! In-game minute of the day he opens next, for the talk action and the refusal hint; -1 is unknown.
	[RplProp()]
	protected int m_iOpensAtMinute = -1;

	//! Server-side: off duty he walks around with AI; created when he first leaves his spot.
	protected ref ELIFE_NpcRoutine m_Routine;
	//! Server-side: where the spot put him.
	protected vector m_vHomePosition;
	//! Server-side: he couldn't leave his spot (no AI world), so he stays there while closed.
	protected bool m_bStaysAtSpot;

	//! Client-side: the current line, cut off by the next one.
	protected AudioHandle m_hVoice = AudioHandle.Invalid;
	//! Client-side: the effect going with the current line, on his character sound component so it can't move his lips.
	protected AudioHandle m_hLineSfx = AudioHandle.Invalid;
	//! Index into the voice table's voices, set by the spot.
	protected int m_iVoice;
	//! Server-side: last wording per line, so he doesn't repeat himself.
	protected ref map<int, int> m_mLastVariant = new map<int, int>();
	//! Client-side: when a line with a repeat cooldown was last heard, per line.
	protected ref map<int, int> m_mLastHeardTick = new map<int, int>();
	//! Client-side: the subtitle heard last, for a dialogue that opens just after it; tick 0 is none.
	protected string m_sLastSubtitle;
	protected int m_iLastLineTick;
	//! Server-side: when the customer last had him say something, as a tick count.
	protected int m_iLastAskedLineTick;
	//! Server-side: the asked line waiting out the cooldown.
	protected int m_iPendingAskedLine = -1;
	//! Server-side: since when he has been close to his home direction while turning home, as a tick count; 0 is not yet.
	protected int m_iHomeSettleTick;
	//! Server-side: the idle is playing its exit animation.
	protected bool m_bIdleExiting;
	protected bool m_bIdleExitRetried;
	//! Server-side: who he faces. Outlives the visit so he watches a customer leave while waving goodbye.
	protected IEntity m_LookAt;
	//! Server-side: when his current gesture ends, as a tick count.
	protected int m_iGestureEndTick;
	//! Server-side: until when he keeps facing the customer for a gesture that is playing or about to start.
	protected int m_iFaceCustomerUntilTick;
	//! Server-side: the way the spot faces, where he turns back to after a visit.
	protected float m_fHomeYaw;

	[RplProp(onRplName: "OnCustomerChanged")]
	protected int m_iCustomerPlayerId;

	protected IEntity m_Customer;
	protected int m_iLastRequestTick;
	//! Server-side: counts visits, so the customer's client can tell this visit's messages from a late one of the last.
	protected int m_iVisitId;
	//! Server-side: OnVisitStart ran for this visit.
	protected bool m_bVisitStarted;
	//! Server-side: customers who asked while he was getting ready (see IsReady); answered by AnswerWaiting.
	protected ref array<int> m_aWaitingPlayerIds = {};

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		//! The damage manager initialises after this component; disabled in the next frame.
		GetGame().GetCallqueue().CallLater(DisableDamage, 0);

		if (Replication.IsServer() && GetGame().InPlayMode())
		{
			//! Before anything can turn him: the spot spawns him facing his desk.
			m_fHomeYaw = owner.GetYawPitchRoll()[0];
			m_vHomePosition = owner.GetOrigin();
			GetGame().GetCallqueue().CallLater(StartIdle, IDLE_START_DELAY_MS);
			GetGame().GetCallqueue().CallLater(CheckSchedule, SCHEDULE_CHECK_MS, true);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Hooks for a kind of NPC
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	//! Server-only. False while he still has to set something up before serving; Prepare() then starts it and
	//! the subclass calls AnswerWaiting() once done.
	protected bool IsReady()
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. See IsReady.
	protected void Prepare()
	{
		AnswerWaiting(true);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. A reason (ELIFE_ENpcReason or the NPC's own) to refuse this customer, NONE to serve them.
	//! Range and busy are checked before.
	protected int CheckCustomer(int playerId, notnull IEntity character)
	{
		return ELIFE_ENpcReason.NONE;
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. A visit started; by default he greets.
	protected void OnVisitStart(int playerId)
	{
		ELIFE_NpcVoiceTable table = GetVoiceTable();
		if (table)
			SayLine(table.m_sGreetingLine);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. The visit ended, however; reset what belongs to it.
	protected void OnVisitEnd()
	{
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. A request from a customer's client (ELIFE_NpcCharacterComponent.Request). Not checked yet:
	//! use CheckRequest. The meaning of kind, a, b and text is the NPC's own.
	void OnRequest(int playerId, notnull IEntity character, notnull ELIFE_NpcCharacterComponent link, int kind, int a, int b, string text)
	{
		Print(string.Format("%1 | request %2 from player %3 not handled", ClassName(), kind, playerId), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	//! Client-side. A message from the server for this machine's player (see Send).
	void OnMessage(notnull ELIFE_NpcCharacterComponent link, int visitId, int kind, int a, int b, string text)
	{
	}

	//------------------------------------------------------------------------------------------------
	//! Client-side. The server granted a visit; opens the conversation. False ends the visit again.
	bool OnGranted(notnull ELIFE_NpcCharacterComponent link)
	{
		return ELIFE_DialogueMenu.Open(new ELIFE_NpcDialogueController(this, link));
	}

	//------------------------------------------------------------------------------------------------
	//! Client-side. The hint for a refused visit.
	string GetRefusalText(int reason)
	{
		if (reason == ELIFE_ENpcReason.BUSY)
			return "#ELIFE-Npc_Busy";
		if (reason == ELIFE_ENpcReason.OUT_OF_RANGE)
			return "#ELIFE-Npc_OutOfRange";
		if (reason == ELIFE_ENpcReason.CLOSED)
			return WidgetManager.Translate("#ELIFE-Npc_Closed", GetOpensAtText());

		return "#ELIFE-Npc_Unavailable";
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. The line he says for a reason; empty is none. Walking out of range gets no comment: the
	//! customer can't hear it anyway.
	protected string GetReasonLineName(int reason)
	{
		ELIFE_NpcVoiceTable table = GetVoiceTable();
		if (!table)
			return "";

		if (reason == ELIFE_ENpcReason.BUSY)
			return table.m_sBusyLine;
		if (reason == ELIFE_ENpcReason.UNAVAILABLE)
			return table.m_sUnavailableLine;
		if (reason == ELIFE_ENpcReason.CLOSED)
			return table.m_sClosedLine;

		return "";
	}

	//------------------------------------------------------------------------------------------------
	// Visits (server)
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	//! Server-only.
	void RequestOpen(int playerId, notnull IEntity character, notnull ELIFE_NpcCharacterComponent link)
	{
		if (m_iCustomerPlayerId != 0 && (m_iCustomerPlayerId != playerId || character != m_Customer))
		{
			Refuse(link, ELIFE_ENpcReason.BUSY);
			return;
		}

		if (!IsUsableBy(character))
		{
			Print(string.Format("%1 | open refused, player %2 is dead or out of range", ClassName(), playerId), LogLevel.WARNING);
			Refuse(link, ELIFE_ENpcReason.OUT_OF_RANGE);
			return;
		}

		if (m_iCustomerPlayerId == 0 && !IsServing())
		{
			Refuse(link, ELIFE_ENpcReason.CLOSED);
			return;
		}

		int reason = CheckCustomer(playerId, character);
		if (reason != ELIFE_ENpcReason.NONE)
		{
			Refuse(link, reason);
			return;
		}

		if (m_iCustomerPlayerId == 0)
		{
			m_iCustomerPlayerId = playerId;
			m_Customer = character;
			m_iVisitId++;
			Replication.BumpMe();
			StopIdle();
			StartAttention();
			GetGame().GetCallqueue().CallLater(Tick, TICK_INTERVAL_MS, true);
			Print(string.Format("%1 | serving player %2", ClassName(), playerId), LogLevel.NORMAL);
		}

		m_iLastRequestTick = System.GetTickCount();

		if (IsReady())
		{
			BeginVisit();
			link.SendGrant(GetRplId(), true, ELIFE_ENpcReason.NONE, m_iVisitId);
			return;
		}

		if (!m_aWaitingPlayerIds.Contains(playerId))
			m_aWaitingPlayerIds.Insert(playerId);

		Prepare();
	}

	//------------------------------------------------------------------------------------------------
	//! Once he is ready, so he doesn't greet someone he then turns away as unavailable.
	protected void BeginVisit()
	{
		if (m_bVisitStarted || m_iCustomerPlayerId == 0)
			return;

		m_bVisitStarted = true;
		OnVisitStart(m_iCustomerPlayerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. He says why, then the customer's client shows it.
	protected void Refuse(notnull ELIFE_NpcCharacterComponent link, int reason)
	{
		SayLine(GetReasonLineName(reason));
		link.SendGrant(GetRplId(), false, reason);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only.
	void RequestClose(int playerId)
	{
		if (playerId == 0 || playerId != m_iCustomerPlayerId)
			return;

		ELIFE_NpcVoiceTable table = GetVoiceTable();
		if (table)
			SayLine(table.m_sGoodbyeLine);

		Release("closed by the customer");
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. True if the request comes from the current customer within range; it then counts as input.
	protected bool CheckRequest(int playerId, IEntity character)
	{
		if (!IsCustomer(playerId, character) || !IsUsableBy(character))
			return false;

		m_iLastRequestTick = System.GetTickCount();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. After Prepare(): only the current customer gets a grant; a refusal releases him so the next one can try again.
	protected void AnswerWaiting(bool available)
	{
		array<int> waiting = {};
		waiting.Copy(m_aWaitingPlayerIds);
		m_aWaitingPlayerIds.Clear();

		if (available)
			BeginVisit();

		foreach (int playerId : waiting)
		{
			ELIFE_NpcCharacterComponent link = GetLink(playerId);
			if (!link)
				continue;

			if (playerId != m_iCustomerPlayerId)
				Refuse(link, ELIFE_ENpcReason.BUSY);
			else if (available)
				link.SendGrant(GetRplId(), true, ELIFE_ENpcReason.NONE, m_iVisitId);
			else
				Refuse(link, ELIFE_ENpcReason.UNAVAILABLE);
		}

		if (!available && m_iCustomerPlayerId != 0)
			Release("unavailable");
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsCustomer(int playerId, IEntity character)
	{
		return playerId != 0 && playerId == m_iCustomerPlayerId && character == m_Customer;
	}

	//------------------------------------------------------------------------------------------------
	protected void Tick()
	{
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager || !m_Customer || !playerManager.IsPlayerConnected(m_iCustomerPlayerId) || playerManager.GetPlayerControlledEntity(m_iCustomerPlayerId) != m_Customer)
		{
			Release("the customer disconnected or the character is gone");
			return;
		}

		if (!IsUsableBy(m_Customer))
		{
			Release("the customer died or walked away");
			return;
		}

		if (System.GetTickCount() - m_iLastRequestTick > AFK_TIMEOUT_MS)
			Release("no input from the customer");
	}

	//------------------------------------------------------------------------------------------------
	protected void Release(string why)
	{
		GetGame().GetCallqueue().Remove(Tick);
		GetGame().GetCallqueue().Remove(SayPendingAskedLine);
		StopAttention();
		Print(string.Format("%1 | released player %2: %3", ClassName(), m_iCustomerPlayerId, why), LogLevel.NORMAL);

		m_iCustomerPlayerId = 0;
		m_Customer = null;
		m_bVisitStarted = false;
		OnVisitEnd();
		Replication.BumpMe();

		//! He closed during the visit; now he can leave.
		if (m_bClosed)
			GetGame().GetCallqueue().CallLater(CheckSchedule, IDLE_START_DELAY_MS);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. A message to that player's client, arriving in OnMessage there. False if the player has no character.
	bool Send(int playerId, int kind, int a = 0, int b = 0, string text = "")
	{
		ELIFE_NpcCharacterComponent link = GetLink(playerId);
		if (!link)
			return false;

		link.SendMessage(GetRplId(), m_iVisitId, kind, a, b, text);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. The link on the character the player controls.
	ELIFE_NpcCharacterComponent GetLink(int playerId)
	{
		IEntity character = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (!character)
			return null;

		return ELIFE_NpcCharacterComponent.Cast(character.FindComponent(ELIFE_NpcCharacterComponent));
	}

	//------------------------------------------------------------------------------------------------
	// Shared
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	bool IsUsableBy(IEntity user)
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(user);
		if (!character || character == GetOwner())
			return false;

		CharacterControllerComponent controller = character.GetCharacterController();
		if (!controller || controller.IsDead())
			return false;

		return vector.Distance(character.GetOrigin(), GetOwner().GetOrigin()) <= m_fUseRange;
	}

	//------------------------------------------------------------------------------------------------
	int GetCustomerPlayerId()
	{
		return m_iCustomerPlayerId;
	}

	//------------------------------------------------------------------------------------------------
	ResourceName GetDialogueConfig()
	{
		return m_sDialogueConfig;
	}

	//------------------------------------------------------------------------------------------------
	string GetDisplayName()
	{
		return m_sDisplayName;
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_NpcVoiceTable GetVoiceTable()
	{
		return ELIFE_NpcVoiceTable.Load(m_sVoiceTable);
	}

	//------------------------------------------------------------------------------------------------
	int GetVoiceCount()
	{
		ELIFE_NpcVoiceTable table = GetVoiceTable();
		if (!table)
			return 0;

		return table.GetVoiceCount();
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only, before the first customer.
	void SetVoice(int voice)
	{
		if (voice >= 0 && voice < GetVoiceCount())
			m_iVoice = voice;
	}

	//------------------------------------------------------------------------------------------------
	RplId GetRplId()
	{
		RplComponent rpl = RplComponent.Cast(GetOwner().FindComponent(RplComponent));
		if (!rpl)
			return RplId.Invalid();

		return rpl.Id();
	}

	//------------------------------------------------------------------------------------------------
	protected SCR_CharacterControllerComponent GetController()
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(GetOwner());
		if (!character)
			return null;

		return SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
	}

	//------------------------------------------------------------------------------------------------
	//! He can't be hurt, so he never goes down and can't be carried or searched either.
	protected void DisableDamage()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(owner.FindComponent(SCR_CharacterDamageManagerComponent));
		if (damage)
			damage.EnableDamageHandling(false);
	}

	//------------------------------------------------------------------------------------------------
	// Opening hours
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	//! Server-only, from the spot, before the first schedule check.
	void SetSchedule(ELIFE_NpcSchedule schedule)
	{
		if (schedule)
			m_Schedule = schedule;
	}

	//------------------------------------------------------------------------------------------------
	bool IsClosed()
	{
		return m_bClosed;
	}

	//------------------------------------------------------------------------------------------------
	//! "08:00", or empty if unknown.
	string GetOpensAtText()
	{
		return ELIFE_NpcSchedule.FormatMinute(m_iOpensAtMinute);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. Open and at his spot.
	protected bool IsServing()
	{
		return !m_bClosed && IsAtSpot();
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. Not out on his off-duty routine.
	protected bool IsAtSpot()
	{
		return !m_Routine || m_Routine.IsOnDuty();
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. A visit that started before closing runs to its end, then he goes off duty; at opening he walks
	//! back and opens once he is at his spot.
	protected void CheckSchedule()
	{
		if (!m_Schedule)
			return;

		int minute = ELIFE_NpcSchedule.GetCurrentMinute();
		if (minute < 0)
			return;

		if (m_Schedule.IsOpenAt(minute))
		{
			if (m_Routine && !m_Routine.IsOnDuty())
			{
				m_Routine.ReturnToDuty();
				return;
			}

			SetClosed(false);
			return;
		}

		SetClosed(true);

		if (m_iCustomerPlayerId == 0 && IsAtSpot() && !m_bStaysAtSpot)
			GoOffDuty();
	}

	//------------------------------------------------------------------------------------------------
	protected void SetClosed(bool closed)
	{
		int opensAt = -1;
		if (m_Schedule)
			opensAt = m_Schedule.GetOpenMinute();

		if (m_bClosed == closed && m_iOpensAtMinute == opensAt)
			return;

		m_bClosed = closed;
		m_iOpensAtMinute = opensAt;
		Replication.BumpMe();
		if (m_bClosed)
			Print(string.Format("%1 | closed until %2", ClassName(), GetOpensAtText()), LogLevel.NORMAL);
		else
			Print(string.Format("%1 | open", ClassName()), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected void GoOffDuty()
	{
		//! A turn back home after the last visit would fight the AI walk.
		GetGame().GetCallqueue().Remove(TurnHome);
		GetGame().GetCallqueue().Remove(TurnHomeStep);
		GetGame().GetCallqueue().Remove(UpdateAttention);
		StopIdle();

		//! An idle that is still starting ignores the stop; the next schedule check tries again.
		SCR_CharacterControllerComponent controller = GetController();
		if (m_bIdleExiting || (controller && controller.IsLoitering()))
			return;

		if (!m_Routine)
			m_Routine = new ELIFE_NpcRoutine(this, m_vHomePosition);

		if (m_Routine.GoOffDuty())
			return;

		m_bStaysAtSpot = true;
		GetGame().GetCallqueue().CallLater(StartIdle, IDLE_START_DELAY_MS);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		if (m_Routine)
			m_Routine.Dispose();
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. The routine brought him back to his spot; he faces his desk and opens if it is time.
	void OnBackOnDuty()
	{
		TurnHome();
		CheckSchedule();
	}

	//------------------------------------------------------------------------------------------------
	// Idle
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	//! Vanilla's standing loiter (the "Wait" emote), started by the server so every client sees it. Only while nobody is served.
	protected void StartIdle()
	{
		//! After a short visit the last idle may still be on its way out.
		if (m_bIdleExiting)
		{
			GetGame().GetCallqueue().CallLater(StartIdle, IDLE_START_DELAY_MS);
			return;
		}

		SCR_CharacterControllerComponent controller = GetController();
		if (!controller || m_iCustomerPlayerId != 0 || controller.IsLoitering() || !IsAtSpot())
			return;

		if (!controller.CanPlayLoiterAnimation(ELoiteringType.LOITERING))
		{
			Print(string.Format("%1 | the idle animation can't start", ClassName()), LogLevel.WARNING);
			return;
		}

		//! No root motion: the animation would turn and shift him away from his spot.
		vector noTarget[4];
		controller.StartLoitering(null, ELoiteringType.LOITERING, false, false, false, noTarget);
	}

	//------------------------------------------------------------------------------------------------
	//! The fast stop can leave the idle pose stuck, which blocks turning, so a running idle gets the normal exit.
	protected void StopIdle()
	{
		GetGame().GetCallqueue().Remove(StartIdle);

		SCR_CharacterControllerComponent controller = GetController();
		if (!controller || m_bIdleExiting)
			return;

		if (controller.IsLoitering())
		{
			//! An exit during the start animation is ignored; the attention tick calls this again.
			if (IsIdleStarting(controller))
				return;

			m_bIdleExiting = true;
			controller.StopLoitering(false);
			Rpc(RpcDo_StopIdle);
			GetGame().GetCallqueue().CallLater(FinishIdleExit, IDLE_EXIT_TIMEOUT_MS);
			return;
		}

		//! A start that is still pending has no animation yet, so cancelling it can't leave a pose behind.
		SCR_ScriptedCharacterInputContext input = controller.GetScrInputContext();
		if (input && input.m_iLoiteringType >= 0)
			controller.StopLoitering(true);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. If the exit hasn't played yet it is sent once more, then the idle is closed.
	protected void FinishIdleExit()
	{
		SCR_CharacterControllerComponent controller = GetController();
		SCR_CharacterCommandLoiter loiter;
		if (controller)
			loiter = SCR_CharacterCommandLoiter.Cast(controller.GetAnimationComponent().GetCommandScripted());

		if (!loiter)
		{
			m_bIdleExiting = false;
			m_bIdleExitRetried = false;
			return;
		}

		if (!m_bIdleExitRetried)
		{
			m_bIdleExitRetried = true;
			//! StopLoitering again would do nothing: the command already counts as exiting.
			ResendIdleExit(controller);
			Rpc(RpcDo_StopIdle);
			GetGame().GetCallqueue().CallLater(FinishIdleExit, IDLE_EXIT_TIMEOUT_MS);
			return;
		}

		m_bIdleExiting = false;
		m_bIdleExitRetried = false;
		loiter.FinishLoiter();

		//! FinishLoiter is local; without this the clients keep showing the idle.
		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(controller.GetAnimationComponent().GetCommandHandler());
		if (handler)
			handler.BroadCastLoiterFinish();
	}

	//------------------------------------------------------------------------------------------------
	//! Clients. Vanilla's StopLoitering on the server only reaches the server, so the clients start the exit here.
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_StopIdle()
	{
		StopProxyIdle();
	}

	//------------------------------------------------------------------------------------------------
	//! Clients. A client that got him after the stop was sent (joining, streaming in) still shows the idle.
	protected void OnCustomerChanged()
	{
		if (m_iCustomerPlayerId != 0)
			StopProxyIdle();
	}

	//------------------------------------------------------------------------------------------------
	//! Clients only; the server's own idle is stopped by StopIdle.
	protected void StopProxyIdle()
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(GetOwner());
		if (!character)
			return;

		RplComponent rpl = character.GetRplComponent();
		if (!rpl || !rpl.IsProxy())
			return;

		SCR_CharacterControllerComponent controller = GetController();
		if (!controller)
			return;

		SCR_CharacterCommandLoiter loiter = SCR_CharacterCommandLoiter.Cast(controller.GetAnimationComponent().GetCommandScripted());
		if (!loiter)
			return;

		GetGame().GetCallqueue().Remove(StopProxyIdle);
		if (IsIdleStarting(controller))
		{
			if (m_iCustomerPlayerId != 0)
				GetGame().GetCallqueue().CallLater(StopProxyIdle, PROXY_IDLE_RETRY_MS);

			return;
		}

		//! A no-op unless the idle is still playing.
		loiter.StopLoitering(false);
	}

	//------------------------------------------------------------------------------------------------
	//! True while the idle's start animation plays, which ignores an exit.
	protected static bool IsIdleStarting(notnull SCR_CharacterControllerComponent controller)
	{
		CharacterAnimationComponent anim = controller.GetAnimationComponent();
		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(anim.GetCommandHandler());
		if (!handler || !handler.GetScriptedStaticTable())
			return false;

		TAnimGraphTag tag = handler.GetScriptedStaticTable().m_IsLoiteringStartingTag;
		return anim.IsPrimaryTag(tag) || anim.IsSecondaryTag(tag);
	}

	//------------------------------------------------------------------------------------------------
	//! Sends the idle's normal exit (-1) to the animation graph again.
	protected static void ResendIdleExit(notnull SCR_CharacterControllerComponent controller)
	{
		CharacterAnimationComponent anim = controller.GetAnimationComponent();
		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(anim.GetCommandHandler());
		if (handler && handler.GetScriptedStaticTable())
			anim.CallCommand(handler.GetScriptedStaticTable().m_CommandGesture, -1, 0.0);
	}

	//------------------------------------------------------------------------------------------------
	// Facing
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	//! Yaw in degrees of a direction; 0 faces +Z, 90 faces +X.
	protected static float YawTo(vector direction)
	{
		return Math.Atan2(direction[0], direction[2]) * Math.RAD2DEG;
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. While serving, he turns towards the customer and looks at them.
	protected void StartAttention()
	{
		GetGame().GetCallqueue().Remove(TurnHome);
		GetGame().GetCallqueue().Remove(TurnHomeStep);
		GetGame().GetCallqueue().Remove(UpdateAttention);
		m_LookAt = m_Customer;
		GetGame().GetCallqueue().CallLater(UpdateAttention, ATTENTION_TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	protected void StopAttention()
	{
		GetGame().GetCallqueue().Remove(TurnHome);
		TurnHome();
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. Waits out a parting gesture, then turns back to his spot and idles.
	protected void TurnHome()
	{
		int remaining = m_iFaceCustomerUntilTick - System.GetTickCount();
		if (remaining > 0)
		{
			GetGame().GetCallqueue().CallLater(TurnHome, remaining);
			return;
		}

		GetGame().GetCallqueue().Remove(UpdateAttention);
		m_LookAt = null;
		m_iHomeSettleTick = 0;
		GetGame().GetCallqueue().Remove(TurnHomeStep);
		GetGame().GetCallqueue().CallLater(TurnHomeStep, ATTENTION_TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only.
	protected void TurnHomeStep()
	{
		bool done = TurnTowards(m_fHomeYaw);
		if (!done && GetTurnLeft(m_fHomeYaw) < HOME_SETTLE_DEG)
		{
			int now = System.GetTickCount();
			if (m_iHomeSettleTick == 0)
				m_iHomeSettleTick = now;
			else if (now - m_iHomeSettleTick >= HOME_SETTLE_MS)
				done = true;
		}

		if (done)
		{
			GetGame().GetCallqueue().Remove(TurnHomeStep);
			GetGame().GetCallqueue().CallLater(StartIdle, IDLE_START_DELAY_MS);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Signed degrees from his body's yaw to yaw, the short way round.
	protected float GetTurn(float yaw)
	{
		float turn = yaw - GetOwner().GetYawPitchRoll()[0];
		return turn - 360 * Math.Round(turn / 360);
	}

	//------------------------------------------------------------------------------------------------
	protected float GetTurnLeft(float yaw)
	{
		return Math.AbsFloat(GetTurn(yaw));
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. True once he faces yaw (degrees). An unchanged heading isn't set again, to save traffic.
	protected bool TurnTowards(float yaw)
	{
		SCR_CharacterControllerComponent controller = GetController();
		if (!controller)
			return true;

		float turn = GetTurn(yaw);
		if (Math.AbsFloat(turn) < ATTENTION_MIN_TURN_DEG)
			return true;

		float lead = Math.Clamp(turn, -HEADING_LEAD_DEG, HEADING_LEAD_DEG);
		float heading = ToHeading(GetOwner().GetYawPitchRoll()[0] + lead);
		float headingChange = heading - controller.GetInputContext().GetHeadingAngle();
		headingChange -= Math.PI2 * Math.Round(headingChange / Math.PI2);
		if (Math.AbsFloat(headingChange) * Math.RAD2DEG >= ATTENTION_MIN_TURN_DEG)
			controller.SetHeadingAngle(heading, true);

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! The heading takes radians from 0 to 2 pi; a negative one isn't reached.
	protected static float ToHeading(float yaw)
	{
		float heading = yaw * Math.DEG2RAD;
		return heading - Math.PI2 * Math.Floor(heading / Math.PI2);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. Turns his heading, which his aim and head follow.
	protected void UpdateAttention()
	{
		if (!m_LookAt)
			return;

		//! An idle that was still starting when he was served isn't loitering yet, so StopIdle missed it.
		if (m_iCustomerPlayerId != 0)
			StopIdle();

		TurnTowards(YawTo(m_LookAt.GetOrigin() - GetOwner().GetOrigin()));
	}

	//------------------------------------------------------------------------------------------------
	// Voice
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	//! Server-only. A line the customer's dialogue asks for when moving to a step: a question's answer, "anything else?".
	void RequestSay(int playerId, notnull IEntity character, string lineName)
	{
		if (!IsCustomer(playerId, character) || !IsUsableBy(character))
			return;

		ELIFE_NpcVoiceTable table = GetVoiceTable();
		int line = -1;
		ELIFE_NpcVoiceLine entry;
		if (table)
		{
			line = table.FindLine(lineName);
			entry = table.GetLine(line);
		}

		//! Outcomes and greetings are the server's to say, so a client can't claim a transfer went through.
		if (!entry || !entry.m_bAskable)
		{
			Print(string.Format("%1 | player %2 asked for line %3, which isn't askable", ClassName(), playerId, lineName), LogLevel.WARNING);
			return;
		}

		int now = System.GetTickCount();
		m_iLastRequestTick = now;
		m_iPendingAskedLine = line;
		GetGame().GetCallqueue().Remove(SayPendingAskedLine);

		int wait = 0;
		if (m_iLastAskedLineTick != 0)
			wait = ASKED_LINE_COOLDOWN_MS - (now - m_iLastAskedLineTick);

		if (wait > 0)
			GetGame().GetCallqueue().CallLater(SayPendingAskedLine, wait);
		else
			SayPendingAskedLine();
	}

	//------------------------------------------------------------------------------------------------
	protected void SayPendingAskedLine()
	{
		if (m_iCustomerPlayerId == 0)
			return;

		m_iLastAskedLineTick = System.GetTickCount();
		SayIndex(m_iPendingAskedLine);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. By name from the voice table; empty says nothing.
	void SayLine(string lineName)
	{
		if (lineName.IsEmpty())
			return;

		ELIFE_NpcVoiceTable table = GetVoiceTable();
		if (!table)
			return;

		int line = table.FindLine(lineName);
		if (line < 0)
		{
			Print(string.Format("%1 | no line %2 in the voice table", ClassName(), lineName), LogLevel.WARNING);
			return;
		}

		SayIndex(line);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. The server picks the wording so everyone hears the same one.
	protected void SayIndex(int line)
	{
		ELIFE_NpcVoiceTable table = GetVoiceTable();
		if (!table)
			return;

		ELIFE_NpcVoiceLine entry = table.GetLine(line);
		if (!entry)
			return;

		int variant = 1;
		int count = entry.m_iVariants;
		if (count > 1)
		{
			//! Any wording but the last one.
			int last = m_mLastVariant.Get(line);
			if (last < 1)
				last = Math.RandomIntInclusive(1, count);

			variant = (last - 1 + Math.RandomIntInclusive(1, count - 1)) % count + 1;
			m_mLastVariant.Set(line, variant);
		}

		Rpc(RpcDo_Say, m_iVoice, line, variant);

		if (RplSession.Mode() != RplMode.Dedicated)
			RpcDo_Say(m_iVoice, line, variant);

		if (entry.m_iGesture != 0)
		{
			//! Until the gesture starts; PlayGesture extends it to the gesture's end.
			int gestureStartTick = System.GetTickCount() + GESTURE_DELAY_MS;
			if (gestureStartTick > m_iFaceCustomerUntilTick)
				m_iFaceCustomerUntilTick = gestureStartTick;
			GetGame().GetCallqueue().CallLater(PlayGesture, GESTURE_DELAY_MS, false, entry.m_iGesture);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. A gesture already playing finishes first.
	protected void PlayGesture(int gesture)
	{
		if (System.GetTickCount() < m_iGestureEndTick)
			return;

		SCR_CharacterControllerComponent controller = GetController();
		bool started = controller && controller.TryStartCharacterGesture(gesture, GESTURE_DURATION_MS);
		if (!started)
			return;

		m_iGestureEndTick = System.GetTickCount() + GESTURE_DURATION_MS;
		m_iFaceCustomerUntilTick = m_iGestureEndTick;
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_Say(int voiceId, int line, int variant)
	{
		//! Every line while serving is another chance to end an idle this client still shows.
		if (m_iCustomerPlayerId != 0)
			StopProxyIdle();

		ELIFE_NpcVoiceTable table = GetVoiceTable();
		SCR_CommunicationSoundComponent voice = SCR_CommunicationSoundComponent.Cast(GetOwner().FindComponent(SCR_CommunicationSoundComponent));
		if (!table || !voice)
			return;

		ELIFE_NpcVoiceLine entry = table.GetLine(line);
		if (!entry)
			return;

		bool speaking = m_hVoice != AudioHandle.Invalid && !voice.IsFinishedPlaying(m_hVoice);

		//! The cooldown only starts when the line is heard, so a skipped one doesn't silence the next request.
		if (entry.m_iRepeatCooldownMs > 0)
		{
			int now = System.GetTickCount();
			int lastHeard = m_mLastHeardTick.Get(line);
			if (speaking || (lastHeard != 0 && now - lastHeard < entry.m_iRepeatCooldownMs))
				return;

			m_mLastHeardTick.Set(line, now);
		}

		if (speaking)
			voice.Terminate(m_hVoice);

		string soundEvent = table.GetSoundEvent(voiceId, line, variant);
		m_hVoice = voice.SoundEvent(soundEvent);
		if (m_hVoice == AudioHandle.Invalid)
			Print(string.Format("%1 | no sound for %2", ClassName(), soundEvent), LogLevel.WARNING);

		PlayLineSfx(entry.m_sSfx);

		m_sLastSubtitle = table.GetSubtitle(voiceId, line, variant);
		m_iLastLineTick = System.GetTickCount();
		if (!ELIFE_DialogueMenu.OnSpeakerSaid(GetOwner(), m_sLastSubtitle))
			ELIFE_NpcSubtitles.Show(GetOwner(), m_sDisplayName, m_sLastSubtitle, voice, m_hVoice);
	}

	//------------------------------------------------------------------------------------------------
	//! Client-side. The subtitle of the line heard within the last maxAgeMs.
	bool GetRecentSubtitle(int maxAgeMs, out string subtitle)
	{
		if (m_iLastLineTick == 0 || System.GetTickCount() - m_iLastLineTick > maxAgeMs)
			return false;

		subtitle = m_sLastSubtitle;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Client-side. Cuts off the previous line's effect, but not a one-off from PlaySfx.
	protected void PlayLineSfx(string soundEvent)
	{
		if (soundEvent.IsEmpty())
			return;

		CharacterSoundComponent sound = GetSoundComponent();
		if (!sound)
			return;

		if (m_hLineSfx != AudioHandle.Invalid)
			sound.Terminate(m_hLineSfx);

		m_hLineSfx = PlayOn(sound, soundEvent);
	}

	//------------------------------------------------------------------------------------------------
	//! Client-side, only on this machine: an effect from his CharacterSoundComponent at his position.
	void PlaySfx(string soundEvent)
	{
		CharacterSoundComponent sound = GetSoundComponent();
		if (sound)
			PlayOn(sound, soundEvent);
	}

	//------------------------------------------------------------------------------------------------
	protected CharacterSoundComponent GetSoundComponent()
	{
		return CharacterSoundComponent.Cast(GetOwner().FindComponent(CharacterSoundComponent));
	}

	//------------------------------------------------------------------------------------------------
	protected AudioHandle PlayOn(CharacterSoundComponent sound, string soundEvent)
	{
		AudioHandle handle = sound.SoundEvent(soundEvent);
		if (handle == AudioHandle.Invalid)
			Print(string.Format("%1 | no sound for %2", ClassName(), soundEvent), LogLevel.WARNING);

		return handle;
	}

	//------------------------------------------------------------------------------------------------
	void ~ELIFE_NpcComponent()
	{
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(Tick);
			GetGame().GetCallqueue().Remove(DisableDamage);
			GetGame().GetCallqueue().Remove(StartIdle);
			GetGame().GetCallqueue().Remove(FinishIdleExit);
			GetGame().GetCallqueue().Remove(StopProxyIdle);
			GetGame().GetCallqueue().Remove(UpdateAttention);
			GetGame().GetCallqueue().Remove(TurnHome);
			GetGame().GetCallqueue().Remove(TurnHomeStep);
			GetGame().GetCallqueue().Remove(PlayGesture);
			GetGame().GetCallqueue().Remove(SayPendingAskedLine);
			GetGame().GetCallqueue().Remove(CheckSchedule);
		}
	}
}
