//------------------------------------------------------------------------------------------------
//! Where a character is in the lie-down, on the machine that owns him.
enum ELIFE_EBedPhase
{
	NONE,
	//! Turning to the start, before the pose.
	TURNING,
	//! The lie-down plays; it can't be cut short.
	LYING_DOWN,
	//! Lying still: getting up is allowed.
	LYING,
	//! The get-up plays.
	GETTING_UP
}

//------------------------------------------------------------------------------------------------
//! Where a sleeper is owned: his lie-down on one bed.
class ELIFE_BedSleeper
{
	IEntity m_Character;
	ELIFE_EBedPhase m_ePhase;
	int m_iPhaseStart;
	//! When the pose really started (vanilla holds it back while holstering and aligning); 0 before.
	int m_iPoseStart;
	//! Turning: the heading we step, not the body's (steps from the body's yaw were too small for the body to follow).
	float m_fHeading;
	//! Get-up asked for during the lie-down: done once it has played.
	bool m_bGetUpAsked;
}

//------------------------------------------------------------------------------------------------
[ComponentEditorProps(category: "ELifeRPG/Beds", description: "On a bed prop: characters lie down on it, one per bed or one per side. Needs an ELIFE_LieUserAction and the bed's animations.")]
class ELIFE_BedComponentClass : ELIFE_PosePropComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! The lie-down is a custom loiter on the bed with the bed prop's own graph, started where the character is owned.
//! He turns to the start beside a long side, back to the bed; the clips sit him down, turn him half round (root
//! motion) and lay him along the bed. Spot 0 of ELIFE_PosePropComponent, or with one sleeper per side spot 0 for the
//! side at -width and spot 1 for the other. The clips lay the head to the character's left or right, whichever is the head end from his side.
class ELIFE_BedComponent : ELIFE_PosePropComponent
{
	[Attribute("", UIWidgets.ResourceNamePicker, "Animation graph with the lie-down, idle and get-up for this bed (states at NPC_Speech 1: head to the left, 2: to the right).", "agr")]
	protected ResourceName m_sGraph;

	[Attribute("", UIWidgets.ResourceNamePicker, "Its animation set instance.", "asi")]
	protected ResourceName m_sGraphInstance;

	[Attribute("8600", UIWidgets.EditBox, "How long the lie-down clip plays, in ms, from the start of the pose. Getting up is offered after it.")]
	protected int m_iLieDownMs;

	[Attribute("0.96", UIWidgets.EditBox, "From the bed's centre line (its long axis) to the start, in m. The clips end with the hips 0.96 m behind the start.")]
	protected float m_fStartDistance;

	[Attribute("0.5", UIWidgets.EditBox, "Lying down is offered this close to the start, in m. From further the pose starts off the spot.")]
	protected float m_fStartReach;

	[Attribute("0", UIWidgets.CheckBox, "The bed's long side runs along its X axis instead of Z (e.g. the military cots).")]
	protected bool m_bLengthAlongX;

	[Attribute("0", UIWidgets.CheckBox, "One sleeper per long side, each on his half (double beds). Otherwise one sleeper for the whole bed.")]
	protected bool m_bSleeperPerSide;

	[Attribute("0", UIWidgets.CheckBox, "The head end is the bed's +Z end (+X with the long side along X) instead of -Z (-X).")]
	protected bool m_bHeadAtPositiveEnd;

	//! NPC_Speech values of the graph's states: lying down with the head to the character's left or to his right.
	protected static const int LIE_HEAD_LEFT = 1;
	protected static const int LIE_HEAD_RIGHT = 2;
	protected static const string CONTROL_VARIABLE = "NPC_Speech";
	//! Vanilla's alignment alone turns him too fast: he turns to the start first, at this speed (deg per second).
	protected static const float TURN_SPEED = 180;
	//! Turned enough within this many degrees; the alignment does the rest.
	protected static const float TURN_DONE = 4;
	protected static const int TICK_MS = 20;
	//! Lies down anyway after this, facing or not.
	protected static const int TURN_TIMEOUT_MS = 2000;
	//! Walking further than this past the reach during the turn cancels the lie-down; it would start off the bed.
	protected static const float TURN_DRIFT = 0.5;
	//! The server only frees a pending bed this much further out: overshoot after input goes off, position lag.
	protected static const float SERVER_DRIFT_MARGIN = 0.5;
	//! Vanilla holds the start back while holstering and aligning; it gives up on a pose not started by then.
	protected static const int START_TIMEOUT_MS = 4000;
	protected static const int LATENCY_MARGIN_MS = 2000;

	//! Every bed in the world, for FindFree.
	protected static ref array<ELIFE_BedComponent> s_aBeds = {};

	//! Where the sleepers are owned: their lie-downs on this bed.
	protected ref array<ref ELIFE_BedSleeper> m_aSleepers = {};

	//------------------------------------------------------------------------------------------------
	//! Server. Beds with a free spot within range of a position, nearest first.
	static void FindFree(vector position, float range, notnull array<ELIFE_BedComponent> outBeds)
	{
		outBeds.Clear();
		array<float> distances = {};
		float rangeSq = range * range;
		foreach (ELIFE_BedComponent bed : s_aBeds)
		{
			if (!bed || !bed.HasFreeSpot())
				continue;

			float distance = vector.DistanceSq(position, bed.GetOwner().GetOrigin());
			if (distance > rangeSq)
				continue;

			int at = 0;
			while (at < distances.Count() && distances[at] <= distance)
				at++;

			distances.InsertAt(distance, at);
			outBeds.InsertAt(bed, at);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Server.
	bool HasFreeSpot()
	{
		if (!GetHolder(0))
			return true;

		return m_bSleeperPerSide && !GetHolder(1);
	}

	//------------------------------------------------------------------------------------------------
	//! The spot of the side a position is on.
	int GetSpotFor(vector position)
	{
		return GetSpotOfSide(GetSide(position));
	}

	//------------------------------------------------------------------------------------------------
	//! The character holds the pose on this bed (from the start of the lie-down to the end of the get-up).
	bool IsLyingHere(IEntity character)
	{
		return ELIFE_Poses.IsInPoseOn(character, GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	//! Where the character is owned: his phase on this bed.
	ELIFE_EBedPhase GetPhase(IEntity character)
	{
		ELIFE_BedSleeper sleeper = FindSleeper(character);
		if (!sleeper)
			return ELIFE_EBedPhase.NONE;

		//! The pose ended without us (jumping out of the idle, death) or never started.
		if (sleeper.m_ePhase >= ELIFE_EBedPhase.LYING && !IsLyingHere(character))
			return ELIFE_EBedPhase.NONE;

		return sleeper.m_ePhase;
	}

	//------------------------------------------------------------------------------------------------
	//! How far, along the ground, the character is from the start the pose put him on. The clips only turn him, so
	//! more than a little means he was pushed (like ELIFE_Seats.GetSitError).
	float GetLieError(notnull IEntity character)
	{
		vector transform[4];
		GetStartTransform(character, transform);
		return vector.DistanceXZ(character.GetOrigin(), transform[3]);
	}

	//------------------------------------------------------------------------------------------------
	//! Close enough to the start on the character's side.
	bool IsAtStart(notnull IEntity character)
	{
		vector transform[4];
		GetStartTransform(character, transform);
		return vector.DistanceXZ(character.GetOrigin(), transform[3]) <= m_fStartReach;
	}

	//------------------------------------------------------------------------------------------------
	//! Where the lie-down starts: beside the long side the character is on, back to the bed. The pose turns him half
	//! round and ends with the hips m_fStartDistance - 0.96 m from the bed's centre line (on it, or on his half).
	void GetStartTransform(notnull IEntity character, out vector transform[4])
	{
		GetStartTransformFrom(character.GetOrigin(), transform);
	}

	//------------------------------------------------------------------------------------------------
	//! The start on the long side nearer to a position, e.g. where an NPC walks from.
	void GetStartTransformFrom(vector position, out vector transform[4])
	{
		GetStartTransformOnSide(GetSide(position), transform);
	}

	//------------------------------------------------------------------------------------------------
	//! Server. The start of a side the character may take, the nearer one first. False if both are taken.
	bool GetFreeStartTransform(notnull IEntity character, out vector transform[4])
	{
		int side = GetSide(character.GetOrigin());
		if (!IsTakenByOther(GetSpotOfSide(side), character))
		{
			GetStartTransformOnSide(side, transform);
			return true;
		}

		if (!m_bSleeperPerSide || IsTakenByOther(GetSpotOfSide(-side), character))
			return false;

		GetStartTransformOnSide(-side, transform);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Server and where the character is owned (the action runs on both; for an NPC that is the server).
	//! The server takes his side's spot, the owner turns him to the start and lies him down. False if the server finds
	//! it taken by someone else (an NPC was beaten to it) or the bed has no animations.
	bool LieDown(notnull IEntity character)
	{
		if (m_sGraph.IsEmpty() || m_sGraphInstance.IsEmpty())
		{
			Print(string.Format("%1 | no animation graph set on %2", ClassName(), GetOwner()), LogLevel.WARNING);
			return false;
		}

		int spot = GetSpotFor(character.GetOrigin());
		if (Replication.IsServer())
		{
			if (IsTakenByOther(spot, character))
				return false;

			Take(spot, character);
		}

		if (!ELIFE_Poses.IsOwned(character) || GetPhase(character) != ELIFE_EBedPhase.NONE)
			return true;

		if (!ELIFE_Poses.GetController(character))
			return false;

		ELIFE_BedSleeper sleeper = FindSleeper(character);
		if (!sleeper)
		{
			sleeper = new ELIFE_BedSleeper();
			sleeper.m_Character = character;
			m_aSleepers.Insert(sleeper);
		}

		sleeper.m_bGetUpAsked = false;
		sleeper.m_iPoseStart = 0;
		sleeper.m_fHeading = character.GetYawPitchRoll()[0];
		SetPhase(sleeper, ELIFE_EBedPhase.TURNING);
		StartTicking();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Where the character is owned. While still turning to the start he just doesn't lie down; during the lie-down
	//! the get-up waits until it has played. The server frees the spot once the pose has ended.
	void GetUp(notnull IEntity character)
	{
		if (!ELIFE_Poses.IsOwned(character))
			return;

		ELIFE_BedSleeper sleeper = FindSleeper(character);
		ELIFE_EBedPhase phase = GetPhase(character);
		if (phase == ELIFE_EBedPhase.TURNING)
			CancelTurn(sleeper);
		else if (phase == ELIFE_EBedPhase.LYING_DOWN)
			sleeper.m_bGetUpAsked = true;
		else if (phase == ELIFE_EBedPhase.LYING)
			StandUp(sleeper);
	}

	//------------------------------------------------------------------------------------------------
	//! +1 or -1: which long side of the bed a position is on.
	protected int GetSide(vector position)
	{
		vector local = GetOwner().CoordToLocal(position);
		float across = local[0];
		if (m_bLengthAlongX)
			across = local[2];

		if (across < 0)
			return -1;

		return 1;
	}

	//------------------------------------------------------------------------------------------------
	protected int GetSpotOfSide(int side)
	{
		if (m_bSleeperPerSide && side > 0)
			return 1;

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	protected void GetStartTransformOnSide(int side, out vector transform[4])
	{
		vector bed[4];
		GetOwner().GetWorldTransform(bed);

		vector across = bed[0];
		if (m_bLengthAlongX)
			across = bed[2];

		float sign = side;
		vector outward = across * sign;
		Math3D.DirectionAndUpMatrix(outward, bed[1], transform);
		transform[3] = bed[3] + outward * m_fStartDistance;
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_BedSleeper FindSleeper(IEntity character)
	{
		if (!character)
			return null;

		foreach (ELIFE_BedSleeper sleeper : m_aSleepers)
		{
			if (sleeper.m_Character == character)
				return sleeper;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected void StartTicking()
	{
		GetGame().GetCallqueue().Remove(Tick);
		GetGame().GetCallqueue().CallLater(Tick, TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	//! Where owned, while anyone here turns or lies down: the turn, the start of the pose and the end of the lie-down.
	protected void Tick()
	{
		bool busy;
		array<ELIFE_BedSleeper> gone = {};
		foreach (ELIFE_BedSleeper sleeper : m_aSleepers)
		{
			if (!sleeper.m_Character)
			{
				gone.Insert(sleeper);
				continue;
			}

			if (sleeper.m_ePhase == ELIFE_EBedPhase.TURNING)
				TickTurning(sleeper);
			else if (sleeper.m_ePhase == ELIFE_EBedPhase.LYING_DOWN)
				TickLyingDown(sleeper);

			busy = busy || sleeper.m_ePhase == ELIFE_EBedPhase.TURNING || sleeper.m_ePhase == ELIFE_EBedPhase.LYING_DOWN;
		}

		foreach (ELIFE_BedSleeper sleeper : gone)
		{
			m_aSleepers.RemoveItem(sleeper);
		}

		if (!busy)
			GetGame().GetCallqueue().Remove(Tick);
	}

	//------------------------------------------------------------------------------------------------
	protected void TickTurning(notnull ELIFE_BedSleeper sleeper)
	{
		IEntity character = sleeper.m_Character;
		SCR_CharacterControllerComponent controller = ELIFE_Poses.GetController(character);
		if (!controller || controller.IsDead())
		{
			SetPhase(sleeper, ELIFE_EBedPhase.NONE);
			return;
		}

		vector transform[4];
		GetStartTransform(character, transform);
		if (vector.DistanceXZ(character.GetOrigin(), transform[3]) > m_fStartReach + TURN_DRIFT)
		{
			Print(string.Format("%1 | %2 left the start while turning", ClassName(), character), LogLevel.VERBOSE);
			CancelTurn(sleeper);
			return;
		}

		float want = Math3D.MatrixToAngles(transform)[0];
		float step = TURN_SPEED * TICK_MS / 1000;
		sleeper.m_fHeading += Math.Clamp(AngleTo(sleeper.m_fHeading, want), -step, step);
		controller.SetHeadingAngle(sleeper.m_fHeading * Math.DEG2RAD, true);

		bool facing = Math.AbsFloat(AngleTo(character.GetYawPitchRoll()[0], want)) <= TURN_DONE;
		if (!facing && System.GetTickCount() - sleeper.m_iPhaseStart < TURN_TIMEOUT_MS)
			return;

		StartPose(sleeper, controller, transform);
	}

	//------------------------------------------------------------------------------------------------
	protected void CancelTurn(notnull ELIFE_BedSleeper sleeper)
	{
		SetPhase(sleeper, ELIFE_EBedPhase.NONE);
		if (Replication.IsServer() && sleeper.m_Character)
			Release(GetSpotFor(sleeper.m_Character.GetOrigin()), sleeper.m_Character);
	}

	//------------------------------------------------------------------------------------------------
	protected void StartPose(notnull ELIFE_BedSleeper sleeper, notnull SCR_CharacterControllerComponent controller, vector transform[4])
	{
		//! Like vanilla's emote configs: no command (the main graph's CMD_Gestures would play its own loiter) and the
		//! variable by name, bound once our graph is attached.
		int lie = LIE_HEAD_LEFT;
		if (IsHeadRightAt(transform))
			lie = LIE_HEAD_RIGHT;

		SCR_LoiterCustomAnimData data = SCR_LoiterCustomAnimData.CreateInstance(commandName: string.Empty,
			graphName: m_sGraph, graphInstanceName: m_sGraphInstance, controlVariableValue: lie, controlVariableName: CONTROL_VARIABLE);

		//! Player input off, or jumping, firing or sprinting would cut the lie-down short. Back on once lying.
		controller.StartLoitering(GetOwner(), ELoiteringType.CUSTOM, true, true, true, transform, true, data);
		SetPhase(sleeper, ELIFE_EBedPhase.LYING_DOWN);
	}

	//------------------------------------------------------------------------------------------------
	//! The head goes to the character's right (+X of the start) if that is towards the bed's head end.
	protected bool IsHeadRightAt(vector transform[4])
	{
		vector bed[4];
		GetOwner().GetWorldTransform(bed);

		vector headEnd = bed[2];
		if (m_bLengthAlongX)
			headEnd = bed[0];

		if (!m_bHeadAtPositiveEnd)
			headEnd = headEnd * -1.0;

		return vector.Dot(transform[0], headEnd) > 0;
	}

	//------------------------------------------------------------------------------------------------
	//! The clip's time counts from when the pose really starts: vanilla holds it back while holstering and aligning.
	protected void TickLyingDown(notnull ELIFE_BedSleeper sleeper)
	{
		IEntity character = sleeper.m_Character;
		int now = System.GetTickCount();
		if (sleeper.m_iPoseStart == 0)
		{
			if (IsLyingHere(character))
			{
				sleeper.m_iPoseStart = now;
				return;
			}

			if (now - sleeper.m_iPhaseStart < START_TIMEOUT_MS)
				return;

			//! Never started: drop the held-back start (not a pose that started elsewhere meanwhile) and give the input back.
			Print(string.Format("%1 | the lying pose of %2 never started", ClassName(), character), LogLevel.WARNING);
			SCR_CharacterControllerComponent controller = ELIFE_Poses.GetController(character);
			if (controller && !controller.IsLoitering() && controller.GetScrInputContext() && controller.GetScrInputContext().m_iLoiteringType >= 0)
				controller.StopLoitering(false);

			SetInputDisabled(character, false);
			SetPhase(sleeper, ELIFE_EBedPhase.NONE);
			return;
		}

		if (now - sleeper.m_iPoseStart < m_iLieDownMs)
			return;

		if (!IsLyingHere(character))
		{
			SetPhase(sleeper, ELIFE_EBedPhase.NONE);
			return;
		}

		SetPhase(sleeper, ELIFE_EBedPhase.LYING);
		if (sleeper.m_bGetUpAsked)
		{
			StandUp(sleeper);
			return;
		}

		//! Jumping now gets up: vanilla stops the loiter normally, with the get-up.
		SetInputDisabled(character, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void StandUp(notnull ELIFE_BedSleeper sleeper)
	{
		SCR_CharacterControllerComponent controller = ELIFE_Poses.GetController(sleeper.m_Character);
		if (!controller)
			return;

		SetInputDisabled(sleeper.m_Character, true);
		controller.StopLoitering(false);
		SetPhase(sleeper, ELIFE_EBedPhase.GETTING_UP);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetPhase(notnull ELIFE_BedSleeper sleeper, ELIFE_EBedPhase phase)
	{
		sleeper.m_ePhase = phase;
		sleeper.m_iPhaseStart = System.GetTickCount();
		ELIFE_Poses.SetStarting(sleeper.m_Character, phase == ELIFE_EBedPhase.TURNING || phase == ELIFE_EBedPhase.LYING_DOWN);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetInputDisabled(IEntity character, bool disabled)
	{
		SCR_CharacterControllerComponent controller = ELIFE_Poses.GetController(character);
		if (controller && controller.GetScrInputContext())
			controller.GetScrInputContext().m_bLoiteringDisablePlayerInput = disabled;
	}

	//------------------------------------------------------------------------------------------------
	//! Signed degrees from one yaw to another, -180 to 180.
	protected static float AngleTo(float from, float to)
	{
		return Math.Repeat(to - from + 180, 360) - 180;
	}

	//------------------------------------------------------------------------------------------------
	//! Server. Before the pose has started he is gone once dead or off the start: the owner cancelled the lie-down
	//! (a remote player's owner can't tell the server), so the bed doesn't stay taken for the whole grace time.
	override protected bool IsGone(int spot, IEntity holder, int now)
	{
		if (super.IsGone(spot, holder, now))
			return true;

		//! A side changer lying on the other side of a double bed no longer holds this one.
		if (IsLyingHere(holder))
			return GetSpotFor(holder.GetOrigin()) != spot;

		SCR_CharacterControllerComponent controller = ELIFE_Poses.GetController(holder);
		return !controller || controller.IsDead() || GetErrorAtSpot(spot, holder) > m_fStartReach + TURN_DRIFT + SERVER_DRIFT_MARGIN;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: how long a sleeper not yet in the pose may stay on the start: the turn, the (possibly held back) start
	//! and the trip of a remote player's pose to the server. Dead or off the start frees the bed sooner (IsGone).
	override protected int GetGraceMs()
	{
		return TURN_TIMEOUT_MS + START_TIMEOUT_MS + LATENCY_MARGIN_MS;
	}

	//------------------------------------------------------------------------------------------------
	override protected float GetPoseError(int spot, notnull IEntity character)
	{
		return GetErrorAtSpot(spot, character);
	}

	//------------------------------------------------------------------------------------------------
	//! Like GetLieError, but on double beds from the start of the spot's own side, not the one he is on now.
	protected float GetErrorAtSpot(int spot, notnull IEntity character)
	{
		if (!m_bSleeperPerSide)
			return GetLieError(character);

		int side = -1;
		if (spot == 1)
			side = 1;

		vector transform[4];
		GetStartTransformOnSide(side, transform);
		return vector.DistanceXZ(character.GetOrigin(), transform[3]);
	}

	//------------------------------------------------------------------------------------------------
	//! The get-up, not a plain stop: during the lie-down it waits until that has played.
	override protected void EndPose(int spot, notnull IEntity character)
	{
		GetUp(character);
	}

	//------------------------------------------------------------------------------------------------
	//! A get-up asked for during the lie-down waits for it: both clips (the get-up is the lie-down reversed).
	override protected int GetEndTimeoutMs()
	{
		return 2 * m_iLieDownMs + 2000;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		//! Not the World Editor's copies: that world stays loaded while playing in Workbench.
		if (GetGame().InPlayMode())
			s_aBeds.Insert(this);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		//! The bed goes (destroyed) under someone owned here: input back and up, the bed's "Get up" goes with it.
		foreach (ELIFE_BedSleeper sleeper : m_aSleepers)
		{
			IEntity character = sleeper.m_Character;
			if (!character)
				continue;

			if (sleeper.m_ePhase >= ELIFE_EBedPhase.LYING_DOWN && ELIFE_Poses.IsOwned(character))
			{
				SetInputDisabled(character, false);
				SCR_CharacterControllerComponent controller = ELIFE_Poses.GetController(character);
				//! Also a start vanilla still holds back (holstering, aligning): it would start later on no bed.
				SCR_ScriptedCharacterInputContext input;
				if (controller)
					input = controller.GetScrInputContext();

				bool pending = controller && !controller.IsLoitering() && input && input.m_iLoiteringType >= 0;
				if (controller && (controller.IsLoiteringOnEntity(owner) || pending))
					controller.StopLoitering(false);
			}

			ELIFE_Poses.SetStarting(character, false);
		}

		m_aSleepers.Clear();
		super.OnDelete(owner);
		s_aBeds.RemoveItem(this);
		GetGame().GetCallqueue().Remove(Tick);
	}
}
