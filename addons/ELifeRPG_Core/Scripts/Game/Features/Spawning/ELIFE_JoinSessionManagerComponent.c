//------------------------------------------------------------------------------------------------
class ELIFE_JoinSessionManagerComponentClass : SCR_BaseGameModeComponentClass
{
}

//------------------------------------------------------------------------------------------------
/*!
	Server-side owner of every join session (spec 5.2, 5.3, 5.8, 5.9): state, request tokens, timeouts and kicks.
	Every log line starts `ELIFE_Join | ` and carries session=, identity= and player= through ELIFE_JoinSession.Describe().
	Backend calls are not made here yet; Tasks 4 and 5 fill the stubs marked below.
*/
class ELIFE_JoinSessionManagerComponent : SCR_BaseGameModeComponent
{
	[Attribute("15", UIWidgets.EditBox, desc: "Backend request timeout, seconds")]
	float m_fBackendTimeoutSeconds;

	[Attribute("300", UIWidgets.EditBox, desc: "Idle timeout in create and select, seconds")]
	float m_fIdleTimeoutSeconds;

	[Attribute("120", UIWidgets.EditBox, desc: "Seconds a rejected or error screen waits before the kick")]
	float m_fRejectedKickSeconds;

	[Attribute("5", UIWidgets.EditBox, desc: "Death respawn delay, seconds")]
	float m_fRespawnDelaySeconds;

	[Attribute("30", UIWidgets.EditBox, desc: "Seconds between accepted Check again presses on the link screen; the client shows the same countdown")]
	float m_fRecheckCooldownSeconds;

	[Attribute("3", UIWidgets.EditBox, desc: "Spawn attempts before the player is rejected")]
	int m_iMaxSpawnAttempts;

	[Attribute("3", UIWidgets.EditBox, desc: "Automatic connect retries when the backend answers unlinked")]
	int m_iUnlinkedMaxRetries;

	[Attribute("5", UIWidgets.EditBox, desc: "Seconds before the first unlinked retry; doubles on each retry")]
	float m_fUnlinkedRetryDelaySeconds;

	[Attribute("", UIWidgets.EditBox, desc: "Whitelist check URL; empty when not configured")]
	string m_sWhitelistUrl;

	[Attribute("", UIWidgets.ResourceNamePicker, desc: "Appearance prefabs offered on the create screen; index 0 is the default", "et")]
	ref array<ResourceName> m_aAppearancePrefabs;

	[Attribute("0 0 0", UIWidgets.EditBox, desc: "Menu camera position")]
	vector m_vMenuCameraPosition;

	[Attribute("0 0 0", UIWidgets.EditBox, desc: "Menu camera angles")]
	vector m_vMenuCameraAngles;

	protected const int TICK_MS = 1000;
	protected const int FIRE_AND_FORGET_MS = 30000;
	protected const string CIV_PREFAB = "{9FA4749AF6C55D96}Prefabs/Characters/Factions/LIFE/Character_LIFE.et";

	protected ref map<int, ref ELIFE_JoinSession> m_mSessions = new map<int, ref ELIFE_JoinSession>();
	protected ref map<int, ref ELIFE_JoinRequestContext> m_mRequestContexts = new map<int, ref ELIFE_JoinRequestContext>();
	//! Holds the untracked disconnect requests alive until they answer or FIRE_AND_FORGET_MS passes; the callback only holds its instance weakly.
	protected ref array<ref ELIFE_JoinRequestContext> m_aFireAndForget = new array<ref ELIFE_JoinRequestContext>();
	//! Holds the name filter requests alive until the platform answers; a late answer is dropped when its request is no longer current.
	protected ref array<ref ELIFE_JoinNameFilterRequest> m_aNameFilters = new array<ref ELIFE_JoinNameFilterRequest>();

#ifdef WORKBENCH
	protected bool m_bClockOverride;
	protected int m_iClockOverrideMs;
	protected bool m_bTestNoSend;
	protected bool m_bTestApiMissing;
#endif

	//------------------------------------------------------------------------------------------------
	static ELIFE_JoinSessionManagerComponent GetInstance()
	{
		BaseGameMode gameMode = GetGame().GetGameMode();
		if (!gameMode)
			return null;

		return ELIFE_JoinSessionManagerComponent.Cast(gameMode.FindComponent(ELIFE_JoinSessionManagerComponent));
	}

	//------------------------------------------------------------------------------------------------
	override void OnGameModeStart()
	{
		super.OnGameModeStart();

		if (Replication.IsServer())
			GetGame().GetCallqueue().CallLater(Tick, TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	override void OnGameModeEnd(SCR_GameModeEndData data)
	{
		GetGame().GetCallqueue().Remove(Tick);

		super.OnGameModeEnd(data);
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_JoinSession GetSession(int playerId)
	{
		return m_mSessions.Get(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! The character the player picked and the backend confirmed, or "" when none is confirmed. Server only; a client has no sessions.
	//! Only SPAWNING and IN_GAME count. LOADING_CHARACTER is still waiting for CharacterSelected. A failed select goes back to SELECTING
	//! and keeps m_sSelectedCharacterId, so the state, not the id, decides. REJECTED and the earlier states never confirm a character.
	string GetConfirmedCharacterId(int playerId)
	{
		ELIFE_JoinSession s = GetSession(playerId);
		if (!s)
			return "";

		if (s.m_eState != ELIFE_EJoinState.SPAWNING && s.m_eState != ELIFE_EJoinState.IN_GAME)
			return "";

		return s.m_sSelectedCharacterId;
	}

	//------------------------------------------------------------------------------------------------
	//! Creates the session for a connecting player, then runs the connect: duplicate identity first, then PlayerConnected.
	//! With no backend client yet (it is created after the player may already be connecting), the connect waits in TickSession instead.
	void StartSession(int playerId)
	{
		// A leftover session for this player id is dropped here, but its backend disconnect is sent tracked below, like a duplicate.
		ELIFE_JoinSession old = GetSession(playerId);
		string leftoverIdentity = "";
		if (old)
		{
			if (old.m_eState == ELIFE_EJoinState.IN_GAME)
			{
				// TODO: save character position/gear/state to the backend before release (no endpoint yet)
			}

			leftoverIdentity = BackendIdentityOf(old);
			RemoveSession(old, "restart");
		}

		ELIFE_JoinSession s = new ELIFE_JoinSession();
		s.m_iPlayerId = playerId;
		s.m_sSessionId = CreateSessionId(playerId);
		// Read once from the registry (the engine is asked in Register only). The session keeps this snapshot.
		s.m_sIdentityId = ELIFE_PlayerIdentityRegistry.Register(playerId);
		s.m_sPlatform = ELIFE_PlayerIdentityRegistry.GetPlatform(playerId);
		s.m_iLastActivityMs = NowMs();
		m_mSessions.Set(playerId, s);

		SetState(s, ELIFE_EJoinState.SESSION_PENDING, "player connected");

		if (s.m_sIdentityId.IsEmpty())
		{
			Reject(s, ELIFE_EJoinScreen.ERROR, MakeErrorPayload("#ELIFE-Join_ErrorIdentity"), "no identity");
			return;
		}

		// The backend client is created in ELIFE_GameMode.OnGameStart, which can run after the player's audit. Wait for it instead of rejecting.
		Push(s, ELIFE_EJoinScreen.CONNECTING, null);

		if (!HasBackend())
		{
			s.m_bAwaitingApi = true;
			s.m_sLeftoverIdentity = leftoverIdentity;
			s.m_iRequestDeadlineMs = DeadlineIn(NowMs(), SecondsToMs(m_fBackendTimeoutSeconds));
			Print(string.Format("ELIFE_Join | %1 waiting for backend client", s.Describe()), LogLevel.NORMAL);
			return;
		}

		ConnectSession(s, leftoverIdentity);
	}

	//------------------------------------------------------------------------------------------------
	//! The connect itself, once the backend client exists. Runs from StartSession, or from TickSession when a wait ends.
	protected void ConnectSession(ELIFE_JoinSession s, string leftoverIdentity)
	{
		// Same identity on another player id: that session is ended first. A leftover session for this player id
		// needs clearing too. Either way the backend gets a tracked PlayerDisconnected, and its reply (or the
		// request timeout) starts this connect, so the backend never sees two live sessions for one identity.
		string clearIdentity = leftoverIdentity;
		ELIFE_JoinSession other = FindOtherSessionByIdentity(s);
		if (other)
		{
			if (IsConnected(other.m_iPlayerId))
				KickPlayer(other, "duplicate identity");

			clearIdentity = BackendIdentityOf(other);
			RemoveSession(other, "duplicate identity");
		}

		if (!clearIdentity.IsEmpty())
		{
			s.m_bAwaitingDuplicateClear = true;
			SendPlayerDisconnectedTracked(s, clearIdentity);
			return;
		}

		SendPlayerConnected(s);
	}

	//------------------------------------------------------------------------------------------------
	//! Ends the session in any state, then tells the backend the identity left (fire-and-forget, untracked).
	void EndSession(int playerId, string reason)
	{
		ELIFE_JoinSession s = GetSession(playerId);
		if (!s)
			return;

		bool wasInGame = s.m_eState == ELIFE_EJoinState.IN_GAME;
		string identity = BackendIdentityOf(s);
		RemoveSession(s, reason);

		if (wasInGame)
		{
			// TODO: save character position/gear/state to the backend before release (no endpoint yet)
		}

		SendPlayerDisconnectedUntracked(s, identity);
	}

	//------------------------------------------------------------------------------------------------
	//! Moves the session to ENDED and forgets it. Sends nothing to the backend.
	protected void RemoveSession(ELIFE_JoinSession s, string reason)
	{
		SetState(s, ELIFE_EJoinState.ENDED, reason);

		s.m_sLinkPin = "";
		s.m_sLinkUrl = "";
		s.m_bLinkSkipped = false;
		if (m_mRequestContexts.Contains(s.m_iPlayerId))
			m_mRequestContexts.Remove(s.m_iPlayerId);

		if (GetSession(s.m_iPlayerId) == s)
			m_mSessions.Remove(s.m_iPlayerId);

		s.m_iRespawnAtMs = 0;
		s.m_iSpawnAttempts = 0;
		s.m_iUnlinkedRetryAtMs = 0;
		s.m_bAwaitingApi = false;
		s.m_sLeftoverIdentity = "";
	}

	//------------------------------------------------------------------------------------------------
	//! The identity the backend still counts as connected for this session. A session waiting for the backend never connected, so only its leftover counts.
	protected string BackendIdentityOf(ELIFE_JoinSession s)
	{
		if (s.m_bAwaitingApi)
			return s.m_sLeftoverIdentity;

		return s.m_sIdentityId;
	}

	//------------------------------------------------------------------------------------------------
	//! True when the backend client exists and calls can be made. A missing client is simulated in self-tests; a no-send run needs none.
	protected bool HasBackend()
	{
#ifdef WORKBENCH
		if (m_bTestApiMissing)
			return false;

		if (m_bTestNoSend)
			return true;
#endif

		return ELIFE_Api.GetInstance() != null;
	}

	//------------------------------------------------------------------------------------------------
	//! Client asked to create a character. Checks the name and appearance, then the profanity filter, then the backend create.
	void HandleRequestCreate(int playerId, string firstName, string lastName, int appearanceIndex)
	{
		ELIFE_JoinSession s = AcceptRpc(playerId, "RpcAsk_RequestCreate", ELIFE_EJoinState.CREATING);
		if (!s)
			return;

		string fullName;
		string errorKey;
		if (!ELIFE_CharacterNameValidator.Validate(firstName, lastName, fullName, errorKey))
		{
			PushCreate(s, errorKey, firstName, lastName);
			return;
		}

		if (appearanceIndex < 0 || appearanceIndex >= GetAppearanceCount())
		{
			PushCreate(s, "#ELIFE-Join_AppearanceInvalid", firstName, lastName);
			return;
		}

#ifdef WORKBENCH
		// Self-tests only: leave the request in flight without filtering or calling the backend.
		if (m_bTestNoSend)
		{
			BeginRequest(s);
			return;
		}
#endif

		ELIFE_JoinRequestContext ctx = BeginRequest(s);
		ctx.m_sFirstName = firstName;
		ctx.m_sLastName = lastName;
		ctx.m_iAppearanceIndex = appearanceIndex;

		ELIFE_JoinNameFilterRequest filter = new ELIFE_JoinNameFilterRequest(this);
		filter.m_Context = ctx;
		filter.m_sFullName = fullName;
		m_aNameFilters.Insert(filter);
		ELIFE_ProfanityFilter.Filter(fullName, filter);
	}

	//------------------------------------------------------------------------------------------------
	//! Client asked to spawn one of its characters. The id must belong to this session's list; otherwise nothing changes.
	void HandleRequestSelect(int playerId, string characterId)
	{
		ELIFE_JoinSession s = AcceptRpc(playerId, "RpcAsk_RequestSelect", ELIFE_EJoinState.SELECTING);
		if (!s)
			return;

		if (!s.HasCharacter(characterId))
		{
			Print(string.Format("ELIFE_Join | %1 select of character %2 not owned", s.Describe(), characterId), LogLevel.WARNING);
			PushSelect(s, "#ELIFE-Join_SelectFailed");
			return;
		}

		SelectCharacter(s, characterId);
	}

	//------------------------------------------------------------------------------------------------
	//! Client asked for the create screen from the select screen.
	void HandleRequestOpenCreate(int playerId)
	{
		ELIFE_JoinSession s = AcceptRpc(playerId, "RpcAsk_RequestOpenCreate", ELIFE_EJoinState.SELECTING);
		if (!s)
			return;

		if (!SetState(s, ELIFE_EJoinState.CREATING, "open create"))
			return;

		PushCreate(s, "", "", "");
	}

	//------------------------------------------------------------------------------------------------
	//! Client went back from create to select. Needs at least one character to show.
	void HandleRequestBackToSelect(int playerId)
	{
		ELIFE_JoinSession s = AcceptRpc(playerId, "RpcAsk_RequestBackToSelect", ELIFE_EJoinState.CREATING, true);
		if (!s)
			return;

		if (!SetState(s, ELIFE_EJoinState.SELECTING, "back to select"))
			return;

		PushSelect(s, "");
	}

	//------------------------------------------------------------------------------------------------
	//! Client pressed "Check again" on the link screen. Only in LINKING and not in flight, and at most once per cooldown; asks the backend again through the connect path (no duplicate clear).
	void HandleRequestRecheck(int playerId)
	{
		ELIFE_JoinSession s = AcceptRpc(playerId, "RpcAsk_RequestRecheck", ELIFE_EJoinState.LINKING);
		if (!s)
			return;

		int now = NowMs();
		if (s.m_iLastRecheckAtMs != 0 && now - s.m_iLastRecheckAtMs < SecondsToMs(m_fRecheckCooldownSeconds))
		{
			Print(string.Format("ELIFE_Join | %1 rejected RpcAsk_RequestRecheck: cooldown", s.Describe()), LogLevel.WARNING);
			return;
		}

		if (!SetState(s, ELIFE_EJoinState.SESSION_PENDING, "recheck"))
			return;

		s.m_iLastRecheckAtMs = now;
		s.m_bRechecking = true;
		SendPlayerConnected(s);
	}

	//------------------------------------------------------------------------------------------------
	//! Client chose "Continue without linking" on the link screen. Same guard as the recheck. The link code and URL are dropped and the character screen follows.
	void HandleRequestSkipLink(int playerId)
	{
		ELIFE_JoinSession s = AcceptRpc(playerId, "RpcAsk_RequestSkipLink", ELIFE_EJoinState.LINKING);
		if (!s)
			return;

		s.m_bLinkSkipped = true;
		s.m_sLinkPin = "";
		s.m_sLinkUrl = "";
		Print(string.Format("ELIFE_Join | %1 link skipped, continuing without a web account link", s.Describe()), LogLevel.NORMAL);
		GoToCharacterScreen(s, "link skipped");
	}

	//------------------------------------------------------------------------------------------------
	//! Client closed a rejected or error screen; kick without waiting for the timer.
	void HandleAckClosed(int playerId)
	{
		ELIFE_JoinSession s = GetSession(playerId);

		// Leave during a recheck: the request in flight does not block it, so only the state and flag are checked here.
		if (s && s.m_eState == ELIFE_EJoinState.SESSION_PENDING && s.m_bRechecking)
		{
			s.m_iLastActivityMs = NowMs();
			KickAndEnd(s, "link screen left during recheck");
			return;
		}

		ELIFE_EJoinState closedFrom = ELIFE_EJoinState.REJECTED;
		string reason = "rejected screen closed";
		if (s && s.m_eState == ELIFE_EJoinState.LINKING)
		{
			closedFrom = ELIFE_EJoinState.LINKING;
			reason = "link screen left";
		}

		s = AcceptRpc(playerId, "RpcAsk_AckClosed", closedFrom);
		if (!s)
			return;

		KickAndEnd(s, reason);
	}

	//------------------------------------------------------------------------------------------------
	int GetAppearanceCount()
	{
		if (!m_aAppearancePrefabs)
			return 0;

		return m_aAppearancePrefabs.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Out-of-range index gives index 0; an empty list gives the CIV prefab.
	ResourceName GetAppearancePrefab(int index)
	{
		int count = GetAppearanceCount();
		if (count == 0)
			return CIV_PREFAB;

		if (index < 0 || index >= count)
			index = 0;

		return m_aAppearancePrefabs[index];
	}

	//------------------------------------------------------------------------------------------------
	//! Position and prefab for the next spawn: the last death spot when there is one (respawn point), otherwise a LIFE spawn point (first join). False when no position can be found.
	bool BuildSpawnRequest(int playerId, out ResourceName prefab, out vector pos, out vector angles)
	{
		ELIFE_JoinSession s = GetSession(playerId);
		if (!s)
			return false;

		prefab = GetAppearancePrefab(s.m_iSelectedAppearance);

		// TODO: use the saved backend position when available
		if (s.m_vLastDeathPos != vector.Zero)
			return ELIFE_SpawnLocator.FindRespawnPosition(s.m_vLastDeathPos, pos, angles, s.Describe());

		return ELIFE_SpawnLocator.FindJoinPosition(pos, angles, s.Describe());
	}

	//------------------------------------------------------------------------------------------------
	//! Authority only: the spawn process runs the custom spawn preparation here. Gear and state come from the prefab for now.
	override bool PreparePlayerEntity_S(SCR_SpawnRequestComponent requestComponent, SCR_SpawnHandlerComponent handlerComponent, SCR_SpawnData data, IEntity entity)
	{
		// TODO: apply gear/state from the backend once a load endpoint exists; prefab default loadout is used today
		return super.PreparePlayerEntity_S(requestComponent, handlerComponent, data, entity);
	}

	//------------------------------------------------------------------------------------------------
	//! Server spawn hook. The character is in the world: SPAWNING goes to IN_GAME and the death spot is forgotten.
	void OnSpawned(int playerId, IEntity entity)
	{
		ELIFE_JoinSession s = GetSession(playerId);
		if (!s || s.m_eState != ELIFE_EJoinState.SPAWNING)
			return;

		if (!SetState(s, ELIFE_EJoinState.IN_GAME, "spawned"))
			return;

		s.m_vLastDeathPos = vector.Zero;
		s.m_vLastKnownPos = vector.Zero;
		s.m_iSpawnAttempts = 0;
		Push(s, ELIFE_EJoinScreen.IN_GAME, null);

		// TODO: periodic save of position/gear/state (no endpoint yet)
	}

	//------------------------------------------------------------------------------------------------
	//! Server spawn hook. Retries up to m_iMaxSpawnAttempts, about a second apart, then rejects the player.
	void OnSpawnFailed(int playerId)
	{
		ELIFE_JoinSession s = GetSession(playerId);
		if (!s || s.m_eState != ELIFE_EJoinState.SPAWNING)
		{
			Print(string.Format("ELIFE_Join | session= identity= player=%1 spawn failure ignored, no session in SPAWNING", playerId), LogLevel.WARNING);
			return;
		}

		s.m_iSpawnAttempts++;
		if (s.m_iSpawnAttempts >= m_iMaxSpawnAttempts)
		{
			Print(string.Format("ELIFE_Join | %1 spawn failed %2 times", s.Describe(), s.m_iSpawnAttempts), LogLevel.WARNING);
			Reject(s, ELIFE_EJoinScreen.ERROR, MakeErrorPayload("#ELIFE-Join_SpawnFailed"), "spawn failed");
			return;
		}

		Print(string.Format("ELIFE_Join | %1 spawn attempt %2 failed, retrying", s.Describe(), s.m_iSpawnAttempts), LogLevel.WARNING);

#ifdef WORKBENCH
		// Self-tests use negative ids; they must not schedule a real respawn.
		if (playerId < 0)
			return;
#endif

		GetGame().GetCallqueue().CallLater(RetrySpawn, SecondsToMs(1), false, playerId, s.m_sSessionId);
	}

	//------------------------------------------------------------------------------------------------
	//! Retry after a failed spawn. Goes ahead only when the same session is still SPAWNING.
	protected void RetrySpawn(int playerId, string sessionId)
	{
		ELIFE_JoinSession s = GetSession(playerId);
		if (!s || s.m_sSessionId != sessionId || s.m_eState != ELIFE_EJoinState.SPAWNING)
		{
			Print(string.Format("ELIFE_Join | session=%1 identity= player=%2 spawn retry dropped, session no longer SPAWNING", sessionId, playerId), LogLevel.WARNING);
			return;
		}

		ELIFE_JoinSpawnLogic spawnLogic = ELIFE_JoinSpawnLogic.GetInstance();
		if (!spawnLogic)
		{
			Reject(s, ELIFE_EJoinScreen.ERROR, MakeErrorPayload("#ELIFE-Join_ErrorBackend"), "no spawn logic");
			return;
		}

		spawnLogic.SpawnLoadedCharacter_S(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Server death hook. IN_GAME goes to SPAWNING once per death, and the respawn is due m_fRespawnDelaySeconds later.
	void OnEntityLost(int playerId)
	{
		ELIFE_JoinSession s = GetSession(playerId);
		if (!s || s.m_eState != ELIFE_EJoinState.IN_GAME)
			return;

		if (!SetState(s, ELIFE_EJoinState.SPAWNING, "entity lost"))
			return;

		// Live position first, else the last one Tick cached while IN_GAME. Test ids skip the engine lookup.
		s.m_vLastDeathPos = s.m_vLastKnownPos;
		if (playerId > 0)
		{
			PlayerManager playerManager = GetGame().GetPlayerManager();
			IEntity entity = null;
			if (playerManager)
				entity = playerManager.GetPlayerControlledEntity(playerId);

			if (entity)
				s.m_vLastDeathPos = entity.GetOrigin();
		}

		s.m_iSpawnAttempts = 0;
		s.m_iRespawnAtMs = DeadlineIn(NowMs(), SecondsToMs(m_fRespawnDelaySeconds));

		// TODO: gear on death (lost items vs default CIV loadout) and save it to the backend
	}

	//------------------------------------------------------------------------------------------------
	//! Moves the session to `to` when the join state machine allows it. Logs a WARNING and changes nothing otherwise.
	bool SetState(ELIFE_JoinSession s, ELIFE_EJoinState to, string reason)
	{
		string from = typename.EnumToString(ELIFE_EJoinState, s.m_eState);
		string toName = typename.EnumToString(ELIFE_EJoinState, to);

		if (!ELIFE_JoinSession.IsTransitionAllowed(s.m_eState, to))
		{
			Print(string.Format("ELIFE_Join | %1 %2 -> %3 refused (%4)", s.Describe(), from, toName, reason), LogLevel.WARNING);
			return false;
		}

		Print(string.Format("ELIFE_Join | %1 %2 -> %3 (%4)", s.Describe(), from, toName, reason), LogLevel.NORMAL);
		s.m_eState = to;
		s.m_iStateEnteredAtMs = NowMs();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Sends a screen to the player's client. Test sessions (no controller) record the screen and payload and send nothing.
	protected void Push(ELIFE_JoinSession s, ELIFE_EJoinScreen screen, JsonApiStruct payload)
	{
		string payloadJson = "{}";
		if (payload)
		{
			payload.Pack();
			payloadJson = payload.AsString();
		}

#ifdef WORKBENCH
		s.m_eTestLastScreen = screen;
		s.m_sTestLastPayload = payloadJson;
#endif

		ELIFE_PlayerController controller = FindPlayerController(s.m_iPlayerId);
		if (!controller)
		{
			Print(string.Format("ELIFE_Join | %1 no player controller, screen %2 not sent", s.Describe(), typename.EnumToString(ELIFE_EJoinScreen, screen)), LogLevel.NORMAL);
			return;
		}

		controller.ELIFE_ShowSession(screen, payloadJson);
	}

	//------------------------------------------------------------------------------------------------
	//! Moves to REJECTED, shows the screen and arms the kick timer. Does nothing when the move is refused.
	protected void Reject(ELIFE_JoinSession s, ELIFE_EJoinScreen screen, JsonApiStruct payload, string reason)
	{
		if (!SetState(s, ELIFE_EJoinState.REJECTED, reason))
			return;

		s.m_sLinkPin = "";
		s.m_sLinkUrl = "";
		Push(s, screen, payload);
		s.m_iKickAtMs = DeadlineIn(NowMs(), SecondsToMs(m_fRejectedKickSeconds));
	}

	//------------------------------------------------------------------------------------------------
	//! Kicks the player. Negative (test) ids only record the request, so self-tests can read it.
	protected void KickPlayer(ELIFE_JoinSession s, string reason)
	{
#ifdef WORKBENCH
		if (s.m_iPlayerId < 0)
		{
			s.m_bTestKickRequested = true;
			Print(string.Format("ELIFE_Join | %1 kick requested (test session): %2", s.Describe(), reason), LogLevel.NORMAL);
			return;
		}
#endif

		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (playerManager)
			playerManager.KickPlayer(s.m_iPlayerId, PlayerManagerKickReason.KICK);

		Print(string.Format("ELIFE_Join | %1 kicked: %2", s.Describe(), reason), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! Starts a backend request: marks it in flight, bumps the token and returns the context that will carry it.
	ELIFE_JoinRequestContext BeginRequest(ELIFE_JoinSession s)
	{
		s.m_bRequestInFlight = true;
		s.m_iRequestToken++;
		s.m_iRequestDeadlineMs = DeadlineIn(NowMs(), SecondsToMs(m_fBackendTimeoutSeconds));

		ELIFE_JoinRequestContext ctx = new ELIFE_JoinRequestContext(this);
		ctx.m_iPlayerId = s.m_iPlayerId;
		ctx.m_sSessionId = s.m_sSessionId;
		ctx.m_iToken = s.m_iRequestToken;
		m_mRequestContexts.Set(s.m_iPlayerId, ctx);
		return ctx;
	}

	//------------------------------------------------------------------------------------------------
	//! True only when the reply belongs to the request still in flight. A stale reply logs and returns false.
	bool AcceptResponse(ELIFE_JoinRequestContext ctx, out ELIFE_JoinSession s)
	{
		s = GetSession(ctx.m_iPlayerId);
		if (!s || !s.m_bRequestInFlight || s.m_sSessionId != ctx.m_sSessionId || s.m_iRequestToken != ctx.m_iToken)
		{
			if (s)
				Print(string.Format("ELIFE_Join | %1 stale response token=%2", s.Describe(), ctx.m_iToken), LogLevel.WARNING);
			else
				Print(string.Format("ELIFE_Join | session=%1 identity= player=%2 stale response, no session", ctx.m_sSessionId, ctx.m_iPlayerId), LogLevel.WARNING);

			s = null;
			return false;
		}

		s.m_bRequestInFlight = false;
		s.m_iRequestDeadlineMs = 0;
		s.m_iRequestToken++;
		if (m_mRequestContexts.Contains(ctx.m_iPlayerId))
			m_mRequestContexts.Remove(ctx.m_iPlayerId);

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Backend result for the player connect. Maps the answer to CREATING, SELECTING, or a rejected screen (spec 5.3).
	void OnPlayerConnectedResult(ELIFE_JoinRequestContext ctx, ELIFE_EApiStatusCode status, JsonApiStruct data)
	{
		ELIFE_JoinSession s;
		if (!AcceptResponse(ctx, s))
			return;

		ELIFE_PlayerConnectedResponse reply = ELIFE_PlayerConnectedResponse.Cast(data);
		if (status != ELIFE_EApiStatusCode.SUCCESS || !reply)
		{
			FailConnect(s, "connect failed");
			return;
		}

		// A recheck is over once its answer arrives; a retry scheduled below is a plain connect.
		// Captured first, because the flag is cleared here and the link hint depends on it.
		bool answersRecheck = s.m_bRechecking;
		s.m_bRechecking = false;

		// The generated enum comes back Unknown for the backend's string values, so the raw status is the fallback.
		string rawStatus = ReadRawStatus(ctx);
		Print(string.Format("ELIFE_Join | %1 connect reply raw status=%2", s.Describe(), rawStatus), LogLevel.NORMAL);

		ELIFE_PlayerSessionStatus replyStatus = reply.status;
		if (replyStatus == ELIFE_PlayerSessionStatus.Unknown)
			replyStatus = ParseStatusString(rawStatus);

		if (replyStatus == ELIFE_PlayerSessionStatus.Blocked)
		{
			// TODO: fill reason, until and appealUrl once the connect reply carries them (no ban fields yet)
			Reject(s, ELIFE_EJoinScreen.BANNED, new ELIFE_JoinBanPayload(), "blocked");
			return;
		}

		if (replyStatus == ELIFE_PlayerSessionStatus.NotWhitelisted)
		{
			ELIFE_JoinWhitelistPayload whitelist = new ELIFE_JoinWhitelistPayload();
			whitelist.url = m_sWhitelistUrl;
			Reject(s, ELIFE_EJoinScreen.NOT_WHITELISTED, whitelist, "not whitelisted");
			return;
		}

		// Unlinked: the Bridge could not resolve the identity and created nothing, so this is not a new player. Retry instead of showing the link screen.
		if (replyStatus == ELIFE_PlayerSessionStatus.Unlinked)
		{
			ScheduleUnlinkedRetry(s);
			return;
		}

		// Unknown values are admitted as active: the Bridge adds statuses, and a player is better let in than locked out. The raw value is logged for review.
		if (replyStatus != ELIFE_PlayerSessionStatus.Active)
		{
			Print(string.Format("ELIFE_Join | %1 unknown connect status [%2], admitted as active", s.Describe(), rawStatus), LogLevel.WARNING);
		}

		ProceedAfterConnect(s, reply, answersRecheck);
	}

	//------------------------------------------------------------------------------------------------
	//! Active connect reply: stores the account, its characters and the link code, then shows the optional link screen when the web account is not linked, or goes to the character screen.
	//! `webAccountLinked` decides, not the status: a player who never touched the portal reads active. `answersRecheck` only picks the hint on the link screen.
	protected void ProceedAfterConnect(ELIFE_JoinSession s, ELIFE_PlayerConnectedResponse reply, bool answersRecheck)
	{
		s.m_sAccountId = reply.accountId;
		s.m_aCharacters.Clear();
		if (reply.characters)
		{
			foreach (ELIFE_CharacterSummary summary : reply.characters)
			{
				if (!summary)
					continue;

				ELIFE_JoinCharacterEntry entry = new ELIFE_JoinCharacterEntry();
				entry.id = summary.characterId;
				entry.name = summary.name;
				s.m_aCharacters.Insert(entry);
			}
		}

		// Only a fresh reply replaces the code and URL. The link screen and a failed recheck both read them from here.
		s.m_sLinkPin = reply.linkPin;
		s.m_sLinkUrl = reply.linkUrl;

		bool needsLink = !reply.webAccountLinked && !s.m_bLinkSkipped;
		if (needsLink && !reply.linkUrl.IsEmpty())
		{
			string hint = "";
			if (answersRecheck)
				hint = "#ELIFE-Join_LinkNotYet";

			ShowLinking(s, hint, "web account not linked");
			return;
		}

		if (needsLink)
			Print(string.Format("ELIFE_Join | %1 web account not linked and no link url, link screen not shown", s.Describe()), LogLevel.WARNING);

		GoToCharacterScreen(s, "connected");
	}

	//------------------------------------------------------------------------------------------------
	//! Character screen once the player may play: CREATING with no characters, SELECTING with some. `reason` labels the log line.
	protected void GoToCharacterScreen(ELIFE_JoinSession s, string reason)
	{
		if (s.m_aCharacters.Count() == 0)
		{
			if (!SetState(s, ELIFE_EJoinState.CREATING, string.Format("%1, no characters", reason)))
				return;

			PushCreate(s, "", "", "");
			return;
		}

		if (!SetState(s, ELIFE_EJoinState.SELECTING, string.Format("%1, characters listed", reason)))
			return;

		PushSelect(s, "");
	}

	//------------------------------------------------------------------------------------------------
	//! Schedules the next automatic connect for an unlinked identity: m_fUnlinkedRetryDelaySeconds for the first, doubled each time after.
	//! The session stays in SESSION_PENDING, so the CONNECTING screen stays up. Past m_iUnlinkedMaxRetries the player is rejected.
	protected void ScheduleUnlinkedRetry(ELIFE_JoinSession s)
	{
		if (s.m_iUnlinkedRetries >= m_iUnlinkedMaxRetries)
		{
			Reject(s, ELIFE_EJoinScreen.ERROR, MakeErrorPayload("#ELIFE-Join_ErrorBackend"), "unlinked, retries exhausted");
			return;
		}

		float delaySeconds = m_fUnlinkedRetryDelaySeconds;
		for (int i = 0; i < s.m_iUnlinkedRetries; i++)
		{
			delaySeconds = delaySeconds * 2;
		}

		s.m_iUnlinkedRetries++;
		s.m_iUnlinkedRetryAtMs = DeadlineIn(NowMs(), SecondsToMs(delaySeconds));
		Print(string.Format("ELIFE_Join | %1 unlinked, retry %2 of %3 in %4 s", s.Describe(), s.m_iUnlinkedRetries, m_iUnlinkedMaxRetries, delaySeconds), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! Backend result for the create. Success adds the character and selects it; an error shows the create screen again with its names.
	void OnCreateCharacterResult(ELIFE_JoinRequestContext ctx, ELIFE_EApiStatusCode status, JsonApiStruct data)
	{
		ELIFE_JoinSession s;
		if (!AcceptResponse(ctx, s))
			return;

		ELIFE_CharacterSummary summary = ELIFE_CharacterSummary.Cast(data);
		if (status != ELIFE_EApiStatusCode.SUCCESS || !summary || summary.characterId.IsEmpty())
		{
			// TODO: map backend validation errors (ProblemDetails) once the callback exposes them
			PushCreate(s, "#ELIFE-Join_CreateFailed", ctx.m_sFirstName, ctx.m_sLastName);
			return;
		}

		ELIFE_JoinCharacterEntry entry = new ELIFE_JoinCharacterEntry();
		entry.id = summary.characterId;
		entry.name = summary.name;
		s.m_aCharacters.Insert(entry);
		s.m_mAppearanceByCharacter.Set(summary.characterId, ctx.m_iAppearanceIndex);

		SelectCharacter(s, summary.characterId);
	}

	//------------------------------------------------------------------------------------------------
	//! Backend result for the character select. Success hands the character to the spawn; an error returns to the select list.
	void OnCharacterSelectedResult(ELIFE_JoinRequestContext ctx, ELIFE_EApiStatusCode status, JsonApiStruct data)
	{
		ELIFE_JoinSession s;
		if (!AcceptResponse(ctx, s))
			return;

		if (status != ELIFE_EApiStatusCode.SUCCESS)
		{
			// TODO: distinct "in use" once the backend reports it
			if (!SetState(s, ELIFE_EJoinState.SELECTING, "character select failed"))
				return;

			PushSelect(s, "#ELIFE-Join_SelectFailed");
			return;
		}

		if (!SetState(s, ELIFE_EJoinState.SPAWNING, "character selected"))
			return;

		ELIFE_JoinSpawnLogic spawnLogic = ELIFE_JoinSpawnLogic.GetInstance();
		if (!spawnLogic)
		{
			Reject(s, ELIFE_EJoinScreen.ERROR, MakeErrorPayload("#ELIFE-Join_ErrorBackend"), "no spawn logic");
			return;
		}

		spawnLogic.SpawnLoadedCharacter_S(s.m_iPlayerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Puts the chosen character into LOADING_CHARACTER and asks the backend to select it. Used by create success and by HandleRequestSelect.
	protected void SelectCharacter(ELIFE_JoinSession s, string characterId)
	{
		// TODO: this should load and lock the character (position, gear, state); today it only starts the Core session, which supersedes any other
		if (!SetState(s, ELIFE_EJoinState.LOADING_CHARACTER, "character chosen"))
			return;

		s.m_sSelectedCharacterId = characterId;
		s.m_sSelectedCharacterName = s.FindCharacterName(characterId);
		s.m_iSelectedAppearance = 0;
		if (s.m_mAppearanceByCharacter.Contains(characterId))
			s.m_iSelectedAppearance = s.m_mAppearanceByCharacter.Get(characterId);

		s.m_iSpawnAttempts = 0;
		Push(s, ELIFE_EJoinScreen.LOADING, null);

#ifdef WORKBENCH
		// Self-tests only: leave the request in flight without calling the backend.
		if (m_bTestNoSend)
		{
			BeginRequest(s);
			return;
		}
#endif

		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (!api)
		{
			SetState(s, ELIFE_EJoinState.SELECTING, "no backend");
			PushSelect(s, "#ELIFE-Join_SelectFailed");
			return;
		}

		ELIFE_JoinRequestContext ctx = BeginRequest(s);
		ELIFE_CharacterSelectedRequest body = new ELIFE_CharacterSelectedRequest();
		body.gameIdentityId = s.m_sIdentityId;
		body.characterId = characterId;
		// TODO: remove request debug logging after the create-character GUID bug is fixed
		Print(string.Format("ELIFE_Join | DEBUG %1 request=CharacterSelected gameIdentityId=[%2] len=%3", s.Describe(), body.gameIdentityId, body.gameIdentityId.Length()), LogLevel.NORMAL);
		ctx.m_Callback = api.CharacterSelected(body, ctx, "OnCharacterSelected");
	}

	//------------------------------------------------------------------------------------------------
	//! The platform's answer for a name check. Only a request that is still current goes on: a rejected name gets an error, a clean one goes to the backend create.
	void OnNameFiltered(ELIFE_JoinNameFilterRequest filter, string filteredText)
	{
		m_aNameFilters.RemoveItem(filter);

		ELIFE_JoinRequestContext ctx = filter.m_Context;
		ELIFE_JoinSession s = FindInFlight(ctx);
		if (!s)
			return;

		if (filteredText != filter.m_sFullName)
		{
			AcceptResponse(ctx, s);
			PushCreate(s, "#ELIFE-Join_NameNotAllowed", ctx.m_sFirstName, ctx.m_sLastName);
			return;
		}

		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (!api)
		{
			AcceptResponse(ctx, s);
			PushCreate(s, "#ELIFE-Join_CreateFailed", ctx.m_sFirstName, ctx.m_sLastName);
			return;
		}

		ELIFE_CreateCharacterRequest body = new ELIFE_CreateCharacterRequest();
		body.gameIdentityId = s.m_sIdentityId;
		body.name = filter.m_sFullName;
		// TODO: remove request debug logging after the create-character GUID bug is fixed
		Print(string.Format("ELIFE_Join | DEBUG %1 request=CreateCharacter gameIdentityId=[%2] len=%3 name=[%4]", s.Describe(), body.gameIdentityId, body.gameIdentityId.Length(), body.name), LogLevel.NORMAL);
		ctx.m_Callback = api.CreateCharacter(body, ctx, "OnCreateCharacter");
	}

	//------------------------------------------------------------------------------------------------
	//! The session if this context still is its in-flight request (same session id and token). Does not clear the request; a stale context logs and returns null.
	protected ELIFE_JoinSession FindInFlight(ELIFE_JoinRequestContext ctx)
	{
		ELIFE_JoinSession s = GetSession(ctx.m_iPlayerId);
		if (!s || !s.m_bRequestInFlight || s.m_sSessionId != ctx.m_sSessionId || s.m_iRequestToken != ctx.m_iToken)
		{
			Print(string.Format("ELIFE_Join | session=%1 identity= player=%2 name check answer dropped, request no longer current", ctx.m_sSessionId, ctx.m_iPlayerId), LogLevel.WARNING);
			return null;
		}

		return s;
	}

	//------------------------------------------------------------------------------------------------
	//! Backend result for a PlayerDisconnected. Tracked ones only come from a duplicate identity, and they start the connect.
	//! Untracked ones (EndSession) have nothing left to apply; the session is already gone.
	void OnPlayerDisconnectedResult(ELIFE_JoinRequestContext ctx, ELIFE_EApiStatusCode status, JsonApiStruct data)
	{
		if (ctx.m_iToken == 0)
		{
			if (status != ELIFE_EApiStatusCode.SUCCESS)
				Print(string.Format("ELIFE_Join | session=%1 identity= player=%2 disconnect not confirmed by backend", ctx.m_sSessionId, ctx.m_iPlayerId), LogLevel.WARNING);

			m_aFireAndForget.RemoveItem(ctx);
			return;
		}

		ELIFE_JoinSession s;
		if (!AcceptResponse(ctx, s))
			return;

		if (!s.m_bAwaitingDuplicateClear || s.m_eState != ELIFE_EJoinState.SESSION_PENDING)
			return;

		// The old session is already gone locally, so a failed disconnect still lets the new player connect.
		if (status != ELIFE_EApiStatusCode.SUCCESS)
			Print(string.Format("ELIFE_Join | %1 duplicate disconnect failed, connecting anyway", s.Describe()), LogLevel.WARNING);

		s.m_bAwaitingDuplicateClear = false;
		SendPlayerConnected(s);
	}

	//------------------------------------------------------------------------------------------------
	//! A backend request ran past its deadline. A late reply is stale after this, because the token moves on.
	protected void OnRequestTimeout(ELIFE_JoinSession s)
	{
		Print(string.Format("ELIFE_Join | %1 backend request timed out in state %2", s.Describe(), typename.EnumToString(ELIFE_EJoinState, s.m_eState)), LogLevel.WARNING);

		// The create names are read before the context goes, so the error screen can echo them.
		string firstName = "";
		string lastName = "";
		ELIFE_JoinRequestContext timedOut = m_mRequestContexts.Get(s.m_iPlayerId);
		if (timedOut)
		{
			firstName = timedOut.m_sFirstName;
			lastName = timedOut.m_sLastName;
		}

		s.m_bRequestInFlight = false;
		s.m_iRequestDeadlineMs = 0;
		s.m_iRequestToken++;
		if (m_mRequestContexts.Contains(s.m_iPlayerId))
			m_mRequestContexts.Remove(s.m_iPlayerId);

		if (s.m_eState == ELIFE_EJoinState.SESSION_PENDING)
		{
			if (s.m_bAwaitingDuplicateClear)
			{
				// The old session's disconnect never answered. It is already gone locally, so connect anyway.
				s.m_bAwaitingDuplicateClear = false;
				SendPlayerConnected(s);
			}
			else
			{
				FailConnect(s, "connect timed out");
			}
		}
		else if (s.m_eState == ELIFE_EJoinState.CREATING)
		{
			PushCreate(s, "#ELIFE-Join_CreateFailed", firstName, lastName);
		}
		else if (s.m_eState == ELIFE_EJoinState.LOADING_CHARACTER)
		{
			if (SetState(s, ELIFE_EJoinState.SELECTING, "character select timed out"))
				PushSelect(s, "#ELIFE-Join_SelectFailed");
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Once a second on the server (see OnGameModeStart). Handles request timeouts, kicks, idle and respawn.
	void Tick()
	{
		int now = NowMs();
		PruneFireAndForget(now);

		array<int> playerIds = new array<int>();
		foreach (int playerId, ELIFE_JoinSession s : m_mSessions)
		{
			playerIds.Insert(playerId);
		}

		foreach (int playerId : playerIds)
		{
			ELIFE_JoinSession s = GetSession(playerId);
			if (s)
				TickSession(s, now);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void TickSession(ELIFE_JoinSession s, int now)
	{
		int playerId = s.m_iPlayerId;

		// Keeps the last live position of a spawned character, for a death that finds no live entity.
		if (s.m_eState == ELIFE_EJoinState.IN_GAME && playerId > 0)
		{
			PlayerManager playerManager = GetGame().GetPlayerManager();
			IEntity entity = null;
			if (playerManager)
				entity = playerManager.GetPlayerControlledEntity(playerId);

			if (entity)
				s.m_vLastKnownPos = entity.GetOrigin();
		}

		// Waiting for the backend client: no request is in flight, so the request timeout below does not apply.
		// The wait ends as soon as the client exists, otherwise it rejects at its deadline.
		if (s.m_bAwaitingApi)
		{
			if (HasBackend())
			{
				s.m_bAwaitingApi = false;
				s.m_iRequestDeadlineMs = 0;
				string leftoverIdentity = s.m_sLeftoverIdentity;
				s.m_sLeftoverIdentity = "";
				Print(string.Format("ELIFE_Join | %1 backend client ready, connect resumes", s.Describe()), LogLevel.NORMAL);
				ConnectSession(s, leftoverIdentity);
				return;
			}

			if (IsDue(s.m_iRequestDeadlineMs, now))
			{
				s.m_bAwaitingApi = false;
				s.m_iRequestDeadlineMs = 0;
				s.m_sLeftoverIdentity = "";
				Reject(s, ELIFE_EJoinScreen.ERROR, MakeErrorPayload("#ELIFE-Join_ErrorBackend"), "no backend");
			}

			return;
		}

		if (s.m_bRequestInFlight && IsDue(s.m_iRequestDeadlineMs, now))
		{
			OnRequestTimeout(s);
			if (GetSession(playerId) != s)
				return;
		}

		// Unlinked retry due. The session waits in SESSION_PENDING with nothing in flight, so the request timeout above never
		// applies to it, and the idle check below skips SESSION_PENDING. Any other state only drops the deadline.
		if (IsDue(s.m_iUnlinkedRetryAtMs, now))
		{
			s.m_iUnlinkedRetryAtMs = 0;
			if (s.m_eState == ELIFE_EJoinState.SESSION_PENDING && !s.m_bRequestInFlight)
				SendPlayerConnected(s);

			return;
		}

		if (IsDue(s.m_iKickAtMs, now))
		{
			KickAndEnd(s, "rejected screen not acknowledged");
			return;
		}

		bool idleState = s.m_eState == ELIFE_EJoinState.CREATING || s.m_eState == ELIFE_EJoinState.SELECTING || s.m_eState == ELIFE_EJoinState.LINKING;
		if (idleState && now - s.m_iLastActivityMs > SecondsToMs(m_fIdleTimeoutSeconds))
		{
			KickAndEnd(s, "idle");
			return;
		}

		if (IsDue(s.m_iRespawnAtMs, now))
		{
			s.m_iRespawnAtMs = 0;
			if (s.m_eState != ELIFE_EJoinState.SPAWNING)
				return;

			// Respawn with the same character and no menu.
			ELIFE_JoinSpawnLogic spawnLogic = ELIFE_JoinSpawnLogic.GetInstance();
			if (!spawnLogic)
			{
				Reject(s, ELIFE_EJoinScreen.ERROR, MakeErrorPayload("#ELIFE-Join_ErrorBackend"), "no spawn logic");
				return;
			}

			spawnLogic.SpawnLoadedCharacter_S(playerId);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Kick, then drop the session. The kicked client's disconnect later finds no session and does nothing.
	protected void KickAndEnd(ELIFE_JoinSession s, string reason)
	{
		KickPlayer(s, reason);
		EndSession(s.m_iPlayerId, reason);
	}

	//------------------------------------------------------------------------------------------------
	//! Guard for a client RPC. Returns the session with its activity refreshed, or null with a WARNING when the RPC is out of order.
	protected ELIFE_JoinSession AcceptRpc(int playerId, string rpcName, ELIFE_EJoinState required, bool needsCharacters = false)
	{
		ELIFE_JoinSession s = GetSession(playerId);
		if (!s)
		{
			Print(string.Format("ELIFE_Join | session= identity= player=%1 rejected %2, no session", playerId, rpcName), LogLevel.WARNING);
			return null;
		}

		bool ok = s.m_eState == required && !s.m_bRequestInFlight;
		if (ok && needsCharacters)
			ok = s.m_aCharacters.Count() > 0;

		if (!ok)
		{
			string why = "";
			if (s.m_bRequestInFlight)
				why = " (request in flight)";

			Print(string.Format("ELIFE_Join | %1 rejected %2 in state %3%4", s.Describe(), rpcName, typename.EnumToString(ELIFE_EJoinState, s.m_eState), why), LogLevel.WARNING);
			return null;
		}

		s.m_iLastActivityMs = NowMs();
		return s;
	}

	//------------------------------------------------------------------------------------------------
	//! Tracked connect: marks the session in flight and sends PlayerConnected. The reply comes back to OnPlayerConnectedResult.
	protected void SendPlayerConnected(ELIFE_JoinSession s)
	{
#ifdef WORKBENCH
		// Self-tests only: leave the request in flight without calling the backend.
		if (m_bTestNoSend)
		{
			BeginRequest(s);
			return;
		}
#endif

		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (!api)
		{
			FailConnect(s, "no backend");
			return;
		}

		ELIFE_JoinRequestContext ctx = BeginRequest(s);
		ELIFE_PlayerConnectedRequest body = new ELIFE_PlayerConnectedRequest();
		body.gameIdentityId = s.m_sIdentityId;
		body.platform = s.m_sPlatform;
		// TODO: remove request debug logging after the create-character GUID bug is fixed
		Print(string.Format("ELIFE_Join | DEBUG %1 request=PlayerConnected gameIdentityId=[%2] len=%3", s.Describe(), body.gameIdentityId, body.gameIdentityId.Length()), LogLevel.NORMAL);
		ctx.m_Callback = api.PlayerConnected(body, ctx, "OnPlayerConnected");
	}

	//------------------------------------------------------------------------------------------------
	//! Tracked disconnect for a duplicate identity. Its reply, or the request timeout, starts the new connect.
	protected void SendPlayerDisconnectedTracked(ELIFE_JoinSession s, string identity)
	{
		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (!api)
		{
			s.m_bAwaitingDuplicateClear = false;
			Reject(s, ELIFE_EJoinScreen.ERROR, MakeErrorPayload("#ELIFE-Join_ErrorBackend"), "no backend");
			return;
		}

		ELIFE_JoinRequestContext ctx = BeginRequest(s);
		ELIFE_PlayerDisconnectedRequest body = new ELIFE_PlayerDisconnectedRequest();
		body.gameIdentityId = identity;
		// TODO: remove request debug logging after the create-character GUID bug is fixed
		Print(string.Format("ELIFE_Join | DEBUG %1 request=PlayerDisconnected/tracked gameIdentityId=[%2] len=%3", s.Describe(), body.gameIdentityId, body.gameIdentityId.Length()), LogLevel.NORMAL);
		api.PlayerDisconnected(body, ctx, "OnPlayerDisconnected");
	}

	//------------------------------------------------------------------------------------------------
	//! Fire-and-forget disconnect from EndSession. Not tracked, so its reply is dropped; the manager holds the context until it answers or FIRE_AND_FORGET_MS passes.
	//! `identity` is the one the backend still counts as connected (see BackendIdentityOf). Empty means nothing to disconnect.
	protected void SendPlayerDisconnectedUntracked(ELIFE_JoinSession s, string identity)
	{
		if (identity.IsEmpty())
			return;

		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (!api)
		{
			Print(string.Format("ELIFE_Join | %1 no backend, disconnect not sent", s.Describe()), LogLevel.WARNING);
			return;
		}

		ELIFE_JoinRequestContext ctx = new ELIFE_JoinRequestContext(this);
		ctx.m_iPlayerId = s.m_iPlayerId;
		ctx.m_sSessionId = s.m_sSessionId;
		ctx.m_iToken = 0;
		ctx.m_iDeadlineMs = DeadlineIn(NowMs(), FIRE_AND_FORGET_MS);
		m_aFireAndForget.Insert(ctx);

		ELIFE_PlayerDisconnectedRequest body = new ELIFE_PlayerDisconnectedRequest();
		body.gameIdentityId = identity;
		// TODO: remove request debug logging after the create-character GUID bug is fixed
		Print(string.Format("ELIFE_Join | DEBUG %1 request=PlayerDisconnected/untracked gameIdentityId=[%2] len=%3", s.Describe(), body.gameIdentityId, body.gameIdentityId.Length()), LogLevel.NORMAL);
		api.PlayerDisconnected(body, ctx, "OnPlayerDisconnected");
	}

	//------------------------------------------------------------------------------------------------
	//! Drops untracked disconnect contexts whose hold has run out. A late reply then finds no instance and does nothing.
	protected void PruneFireAndForget(int now)
	{
		for (int i = m_aFireAndForget.Count() - 1; i >= 0; i--)
		{
			if (IsDue(m_aFireAndForget[i].m_iDeadlineMs, now))
				m_aFireAndForget.Remove(i);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Another live session with the same identity (a different player id), or null.
	protected ELIFE_JoinSession FindOtherSessionByIdentity(ELIFE_JoinSession self)
	{
		foreach (int playerId, ELIFE_JoinSession other : m_mSessions)
		{
			if (playerId != self.m_iPlayerId && other.m_sIdentityId == self.m_sIdentityId)
				return other;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsConnected(int playerId)
	{
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return false;

		return playerManager.IsPlayerConnected(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Shows the create screen with an optional error key and the names the player typed. Back is offered only when the player has a character.
	protected void PushCreate(ELIFE_JoinSession s, string error, string firstName, string lastName)
	{
		ELIFE_JoinCreatePayload payload = new ELIFE_JoinCreatePayload();
		payload.canGoBack = 0;
		if (s.m_aCharacters.Count() > 0)
			payload.canGoBack = 1;

		payload.error = error;
		payload.firstName = firstName;
		payload.lastName = lastName;
		payload.appearanceCount = GetAppearanceCount();
		Push(s, ELIFE_EJoinScreen.CREATING, payload);
	}

	//------------------------------------------------------------------------------------------------
	//! Shows the select screen with the session's characters and an optional error key.
	protected void PushSelect(ELIFE_JoinSession s, string error)
	{
		ELIFE_JoinSelectPayload payload = new ELIFE_JoinSelectPayload();
		foreach (ELIFE_JoinCharacterEntry entry : s.m_aCharacters)
			payload.characters.Insert(entry);

		payload.error = error;
		Push(s, ELIFE_EJoinScreen.SELECTING, payload);
	}

	//------------------------------------------------------------------------------------------------
	//! Moves to LINKING and shows the link screen with the stored code and an optional hint. Does nothing when the move is refused.
	protected void ShowLinking(ELIFE_JoinSession s, string hint, string reason)
	{
		if (!SetState(s, ELIFE_EJoinState.LINKING, reason))
			return;

		ELIFE_JoinLinkPayload payload = new ELIFE_JoinLinkPayload();
		payload.url = s.m_sLinkUrl;
		payload.code = s.m_sLinkPin;
		payload.hint = hint;

		// TODO: remove link code debug logging
		Print(string.Format("ELIFE_Join | DEBUG %1 link code length=%2", s.Describe(), payload.code.Length()), LogLevel.NORMAL);

		Push(s, ELIFE_EJoinScreen.LINKING, payload);
	}

	//------------------------------------------------------------------------------------------------
	//! A connect failed without a usable answer. A recheck goes back to LINKING with the error hint, so the player can try again; a first connect is rejected.
	protected void FailConnect(ELIFE_JoinSession s, string reason)
	{
		if (!s.m_bRechecking)
		{
			Reject(s, ELIFE_EJoinScreen.ERROR, MakeErrorPayload("#ELIFE-Join_ErrorBackend"), reason);
			return;
		}

		s.m_bRechecking = false;
		ShowLinking(s, "#ELIFE-Join_ErrorBackend", reason);
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_JoinErrorPayload MakeErrorPayload(string messageKey)
	{
		ELIFE_JoinErrorPayload payload = new ELIFE_JoinErrorPayload();
		payload.messageKey = messageKey;
		return payload;
	}

	//------------------------------------------------------------------------------------------------
	//! Status string from the raw connect body, or "" when there is no body (tests, or no callback). The body also carries linkPin, which is never read out or logged.
	protected string ReadRawStatus(ELIFE_JoinRequestContext ctx)
	{
		if (!ctx.m_Callback)
			return "";

		string body = ctx.m_Callback.GetData();
		if (body.IsEmpty())
			return "";

		ELIFE_JoinStatusRaw raw = new ELIFE_JoinStatusRaw();
		raw.ExpandFromRAW(body);
		return raw.status;
	}

	//------------------------------------------------------------------------------------------------
	//! Maps the backend's status strings. Anything else gives Unknown, which the handler admits as active and logs as a WARNING.
	static ELIFE_PlayerSessionStatus ParseStatusString(string value)
	{
		if (value == "active")
			return ELIFE_PlayerSessionStatus.Active;

		if (value == "unlinked")
			return ELIFE_PlayerSessionStatus.Unlinked;

		if (value == "blocked")
			return ELIFE_PlayerSessionStatus.Blocked;

		if (value == "not_whitelisted")
			return ELIFE_PlayerSessionStatus.NotWhitelisted;

		return ELIFE_PlayerSessionStatus.Unknown;
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_PlayerController FindPlayerController(int playerId)
	{
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return null;

		return ELIFE_PlayerController.Cast(playerManager.GetPlayerController(playerId));
	}

	//------------------------------------------------------------------------------------------------
	protected string CreateSessionId(int playerId)
	{
		return string.Format("%1-%2-%3", playerId, NowMs(), Math.RandomIntInclusive(0, 99999));
	}

	//------------------------------------------------------------------------------------------------
	protected int SecondsToMs(float seconds)
	{
		return Math.Round(seconds * 1000);
	}

	//------------------------------------------------------------------------------------------------
	//! Armed deadline `delayMs` after `nowMs`. Never 0, because 0 means "not armed" on ELIFE_JoinSession. Wrapping is fine.
	protected int DeadlineIn(int nowMs, int delayMs)
	{
		int at = nowMs + delayMs;
		if (at == 0)
			at = 1;

		return at;
	}

	//------------------------------------------------------------------------------------------------
	//! True when a deadline is armed and has passed. Compares by difference, so System.GetTickCount() wrapping does not matter.
	protected bool IsDue(int deadlineMs, int nowMs)
	{
		return deadlineMs != 0 && nowMs - deadlineMs >= 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Milliseconds from System.GetTickCount(); the self-tests can replace it with a fake clock.
	protected int NowMs()
	{
#ifdef WORKBENCH
		if (m_bClockOverride)
			return m_iClockOverrideMs;
#endif

		return System.GetTickCount();
	}

#ifdef WORKBENCH
	//------------------------------------------------------------------------------------------------
	//! Self-tests only. -1 returns to real time.
	void SetClockOverrideForTests(int ms)
	{
		m_bClockOverride = ms != -1;
		m_iClockOverrideMs = ms;
	}

	//------------------------------------------------------------------------------------------------
	//! Self-tests only. Puts a hand-built session into the manager.
	void InsertSessionForTests(ELIFE_JoinSession s)
	{
		m_mSessions.Set(s.m_iPlayerId, s);
	}

	//------------------------------------------------------------------------------------------------
	//! Self-tests only. The request context the player's session is waiting on, or null.
	ELIFE_JoinRequestContext RequestContextForTests(int playerId)
	{
		return m_mRequestContexts.Get(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Self-tests only. Holds a name filter request the way the real path does, so OnNameFiltered can remove it.
	void AddNameFilterForTests(ELIFE_JoinNameFilterRequest filter)
	{
		m_aNameFilters.Insert(filter);
	}

	//------------------------------------------------------------------------------------------------
	//! Self-tests only. How many name filter requests are still held.
	int NameFilterCountForTests()
	{
		return m_aNameFilters.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Self-tests only. A connect or reconnect leaves its request in flight and does not call the backend.
	void SetNoSendForTests(bool noSend)
	{
		m_bTestNoSend = noSend;
	}

	//------------------------------------------------------------------------------------------------
	//! Self-tests only. Simulates a missing backend client (ELIFE_Api) without touching the real one.
	void SetApiMissingForTests(bool missing)
	{
		m_bTestApiMissing = missing;
	}

	//------------------------------------------------------------------------------------------------
	//! Self-tests only. Forgets every session and request, and returns to real time.
	void ClearSessionsForTests()
	{
		m_mSessions = new map<int, ref ELIFE_JoinSession>();
		m_mRequestContexts = new map<int, ref ELIFE_JoinRequestContext>();
		m_bClockOverride = false;
		m_bTestNoSend = false;
		m_bTestApiMissing = false;
	}

	//------------------------------------------------------------------------------------------------
	//! Self-tests only. Drops every session and request with a negative (test) player id and returns to real time.
	void ClearTestSessions()
	{
		array<int> testIds = new array<int>();
		foreach (int playerId, ELIFE_JoinSession s : m_mSessions)
		{
			if (playerId < 0)
				testIds.Insert(playerId);
		}

		foreach (int playerId : testIds)
		{
			m_mSessions.Remove(playerId);
		}

		array<int> testContextIds = new array<int>();
		foreach (int playerId, ELIFE_JoinRequestContext ctx : m_mRequestContexts)
		{
			if (playerId < 0)
				testContextIds.Insert(playerId);
		}

		foreach (int playerId : testContextIds)
		{
			m_mRequestContexts.Remove(playerId);
		}

		m_bClockOverride = false;
		m_bTestNoSend = false;
		m_bTestApiMissing = false;
	}
#endif
}
