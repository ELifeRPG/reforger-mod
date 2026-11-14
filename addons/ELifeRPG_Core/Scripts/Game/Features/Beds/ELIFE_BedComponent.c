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
[ComponentEditorProps(category: "ELifeRPG/Beds", description: "On a bed prop: one character at a time lies down on it. Needs an ELIFE_LieUserAction and the bed's animations.")]
class ELIFE_BedComponentClass : ELIFE_PosePropComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! The lie-down is a custom loiter on the bed with the bed prop's own graph, started where the character is owned.
//! He turns to the start beside a long side, back to the bed; the clips sit him down, turn him half round (root
//! motion) and lay him along the bed. The bed is spot 0 of ELIFE_PosePropComponent.
class ELIFE_BedComponent : ELIFE_PosePropComponent
{
	[Attribute("", UIWidgets.ResourceNamePicker, "Animation graph with the lie-down, idle and get-up for this bed (state at NPC_Speech 1).", "agr")]
	protected ResourceName m_sGraph;

	[Attribute("", UIWidgets.ResourceNamePicker, "Its animation set instance.", "asi")]
	protected ResourceName m_sGraphInstance;

	[Attribute("8600", UIWidgets.EditBox, "How long the lie-down clip plays, in ms, from the start of the pose. Getting up is offered after it.")]
	protected int m_iLieDownMs;

	[Attribute("0.96", UIWidgets.EditBox, "From the bed's centre line (its Z axis) to the start, in m. The clips end with the hips this far behind the start.")]
	protected float m_fStartDistance;

	[Attribute("0.5", UIWidgets.EditBox, "Lying down is offered this close to the start, in m. From further the pose starts off the spot.")]
	protected float m_fStartReach;

	static const int SPOT = 0;

	//! NPC_Speech value of the graph's "Lie" state.
	protected static const int LIE = 1;
	protected static const string CONTROL_VARIABLE = "NPC_Speech";
	//! Vanilla's alignment alone turns him too fast: he turns to the start first, at this speed (deg per second).
	protected static const float TURN_SPEED = 180;
	//! Turned enough within this many degrees; the alignment does the rest.
	protected static const float TURN_DONE = 4;
	protected static const int TURN_STEP_MS = 20;
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

	//! Where the sleeper is owned.
	protected IEntity m_Local;
	protected ELIFE_EBedPhase m_ePhase;
	protected int m_iPhaseStart;
	//! Turning: the heading we step, not the body's (steps from the body's yaw were too small for the body to follow).
	protected float m_fHeading;
	//! Get-up asked for during the lie-down: done once it has played.
	protected bool m_bGetUpAsked;

	//------------------------------------------------------------------------------------------------
	//! Server. Free beds within range of a position, nearest first.
	static void FindFree(vector position, float range, notnull array<ELIFE_BedComponent> outBeds)
	{
		outBeds.Clear();
		array<float> distances = {};
		float rangeSq = range * range;
		foreach (ELIFE_BedComponent bed : s_aBeds)
		{
			if (!bed || bed.GetHolder(SPOT))
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
	//! The character holds the pose on this bed (from the start of the lie-down to the end of the get-up).
	bool IsLyingHere(IEntity character)
	{
		return ELIFE_Poses.IsInPoseOn(character, GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	//! Where the character is owned: his phase on this bed.
	ELIFE_EBedPhase GetPhase(IEntity character)
	{
		if (!character || character != m_Local)
			return ELIFE_EBedPhase.NONE;

		//! The pose ended without us (jumping out of the idle, death) or never started.
		if (m_ePhase >= ELIFE_EBedPhase.LYING && !IsLyingHere(character))
			return ELIFE_EBedPhase.NONE;

		return m_ePhase;
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
	//! round and ends with the hips on the bed's centre line.
	void GetStartTransform(notnull IEntity character, out vector transform[4])
	{
		GetStartTransformFrom(character.GetOrigin(), transform);
	}

	//------------------------------------------------------------------------------------------------
	//! The start on the long side nearer to a position, e.g. where an NPC walks from.
	void GetStartTransformFrom(vector position, out vector transform[4])
	{
		vector bed[4];
		GetOwner().GetWorldTransform(bed);

		float side = 1;
		if (GetOwner().CoordToLocal(position)[0] < 0)
			side = -1;

		vector outward = bed[0] * side;
		transform[0] = -bed[2] * side;
		transform[1] = bed[1];
		transform[2] = outward;
		transform[3] = bed[3] + outward * m_fStartDistance;
	}

	//------------------------------------------------------------------------------------------------
	//! Server and where the character is owned (the action runs on both; for an NPC that is the server).
	//! The server takes the bed, the owner turns him to the start and lies him down. False if the server finds the
	//! bed taken by someone else (an NPC was beaten to it) or the bed has no animations.
	bool LieDown(notnull IEntity character)
	{
		if (m_sGraph.IsEmpty() || m_sGraphInstance.IsEmpty())
		{
			Print(string.Format("%1 | no animation graph set on %2", ClassName(), GetOwner()), LogLevel.WARNING);
			return false;
		}

		if (Replication.IsServer())
		{
			if (IsTakenByOther(SPOT, character))
				return false;

			Take(SPOT, character);
		}

		if (!ELIFE_Poses.IsOwned(character) || GetPhase(character) != ELIFE_EBedPhase.NONE)
			return true;

		SCR_CharacterControllerComponent controller = ELIFE_Poses.GetController(character);
		if (!controller)
			return false;

		if (m_Local != character)
			ELIFE_Poses.SetStarting(m_Local, false);

		m_Local = character;
		m_bGetUpAsked = false;
		SetPhase(ELIFE_EBedPhase.TURNING);
		m_fHeading = character.GetYawPitchRoll()[0];
		GetGame().GetCallqueue().Remove(Turn);
		GetGame().GetCallqueue().CallLater(Turn, TURN_STEP_MS, true, character);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Where the character is owned. While still turning to the start he just doesn't lie down; during the lie-down
	//! the get-up waits until it has played. The server frees the bed once the pose has ended.
	void GetUp(notnull IEntity character)
	{
		if (!ELIFE_Poses.IsOwned(character))
			return;

		ELIFE_EBedPhase phase = GetPhase(character);
		if (phase == ELIFE_EBedPhase.TURNING)
		{
			StopTurning();
			SetPhase(ELIFE_EBedPhase.NONE);
			if (Replication.IsServer())
				Release(SPOT, character);
		}
		else if (phase == ELIFE_EBedPhase.LYING_DOWN)
			m_bGetUpAsked = true;
		else if (phase == ELIFE_EBedPhase.LYING)
			StandUp(character);
	}

	//------------------------------------------------------------------------------------------------
	protected void Turn(IEntity character)
	{
		SCR_CharacterControllerComponent controller = ELIFE_Poses.GetController(character);
		if (!character || !controller || controller.IsDead() || character != m_Local)
		{
			StopTurning();
			SetPhase(ELIFE_EBedPhase.NONE);
			return;
		}

		vector transform[4];
		GetStartTransform(character, transform);
		if (vector.DistanceXZ(character.GetOrigin(), transform[3]) > m_fStartReach + TURN_DRIFT)
		{
			Print(string.Format("%1 | %2 left the start while turning", ClassName(), character), LogLevel.VERBOSE);
			StopTurning();
			SetPhase(ELIFE_EBedPhase.NONE);
			if (Replication.IsServer())
				Release(SPOT, character);

			return;
		}

		float want = Math3D.MatrixToAngles(transform)[0];
		float step = TURN_SPEED * TURN_STEP_MS / 1000;
		m_fHeading += Math.Clamp(AngleTo(m_fHeading, want), -step, step);
		controller.SetHeadingAngle(m_fHeading * Math.DEG2RAD, true);

		bool facing = Math.AbsFloat(AngleTo(character.GetYawPitchRoll()[0], want)) <= TURN_DONE;
		if (!facing && System.GetTickCount() - m_iPhaseStart < TURN_TIMEOUT_MS)
			return;

		StopTurning();
		StartPose(character, controller, transform);
	}

	//------------------------------------------------------------------------------------------------
	protected void StopTurning()
	{
		GetGame().GetCallqueue().Remove(Turn);
	}

	//------------------------------------------------------------------------------------------------
	protected void StartPose(notnull IEntity character, notnull SCR_CharacterControllerComponent controller, vector transform[4])
	{
		//! Like vanilla's emote configs: no command (the main graph's CMD_Gestures would play its own loiter) and the
		//! variable by name, bound once our graph is attached.
		SCR_LoiterCustomAnimData data = SCR_LoiterCustomAnimData.CreateInstance(commandName: string.Empty,
			graphName: m_sGraph, graphInstanceName: m_sGraphInstance, controlVariableValue: LIE, controlVariableName: CONTROL_VARIABLE);

		//! Player input off, or jumping, firing or sprinting would cut the lie-down short. Back on once lying.
		controller.StartLoitering(GetOwner(), ELoiteringType.CUSTOM, true, true, true, transform, true, data);
		SetPhase(ELIFE_EBedPhase.LYING_DOWN);
		GetGame().GetCallqueue().Remove(WaitForPose);
		GetGame().GetCallqueue().CallLater(WaitForPose, TURN_STEP_MS, true, character);
	}

	//------------------------------------------------------------------------------------------------
	//! The clip's time counts from when the pose really starts: vanilla holds it back while holstering and aligning.
	protected void WaitForPose(IEntity character)
	{
		if (!character || character != m_Local || m_ePhase != ELIFE_EBedPhase.LYING_DOWN)
		{
			GetGame().GetCallqueue().Remove(WaitForPose);
			if (!character)
				SetPhase(ELIFE_EBedPhase.NONE);

			return;
		}

		if (IsLyingHere(character))
		{
			GetGame().GetCallqueue().Remove(WaitForPose);
			GetGame().GetCallqueue().CallLater(OnLaidDown, m_iLieDownMs, false, character);
			return;
		}

		if (System.GetTickCount() - m_iPhaseStart < START_TIMEOUT_MS)
			return;

		//! Never started: drop the held-back start (not a pose that started elsewhere meanwhile) and give the input back.
		Print(string.Format("%1 | the lying pose of %2 never started", ClassName(), character), LogLevel.WARNING);
		GetGame().GetCallqueue().Remove(WaitForPose);
		SCR_CharacterControllerComponent controller = ELIFE_Poses.GetController(character);
		if (controller && !controller.IsLoitering() && controller.GetScrInputContext() && controller.GetScrInputContext().m_iLoiteringType >= 0)
			controller.StopLoitering(false);

		SetInputDisabled(character, false);
		SetPhase(ELIFE_EBedPhase.NONE);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnLaidDown(IEntity character)
	{
		if (!character || character != m_Local || m_ePhase != ELIFE_EBedPhase.LYING_DOWN)
			return;

		if (!IsLyingHere(character))
		{
			SetPhase(ELIFE_EBedPhase.NONE);
			return;
		}

		SetPhase(ELIFE_EBedPhase.LYING);
		if (m_bGetUpAsked)
		{
			StandUp(character);
			return;
		}

		//! Jumping now gets up: vanilla stops the loiter normally, with the get-up.
		SetInputDisabled(character, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void StandUp(notnull IEntity character)
	{
		SCR_CharacterControllerComponent controller = ELIFE_Poses.GetController(character);
		if (!controller)
			return;

		SetInputDisabled(character, true);
		controller.StopLoitering(false);
		SetPhase(ELIFE_EBedPhase.GETTING_UP);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetPhase(ELIFE_EBedPhase phase)
	{
		m_ePhase = phase;
		m_iPhaseStart = System.GetTickCount();
		ELIFE_Poses.SetStarting(m_Local, phase == ELIFE_EBedPhase.TURNING || phase == ELIFE_EBedPhase.LYING_DOWN);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetInputDisabled(notnull IEntity character, bool disabled)
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

		if (IsLyingHere(holder))
			return false;

		SCR_CharacterControllerComponent controller = ELIFE_Poses.GetController(holder);
		return !controller || controller.IsDead() || GetLieError(holder) > m_fStartReach + TURN_DRIFT + SERVER_DRIFT_MARGIN;
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
		return GetLieError(character);
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
		if (m_Local && m_ePhase >= ELIFE_EBedPhase.LYING_DOWN && ELIFE_Poses.IsOwned(m_Local))
		{
			SetInputDisabled(m_Local, false);
			SCR_CharacterControllerComponent controller = ELIFE_Poses.GetController(m_Local);
			//! Also a start vanilla still holds back (holstering, aligning): it would start later on no bed.
			SCR_ScriptedCharacterInputContext input;
			if (controller)
				input = controller.GetScrInputContext();

			bool pending = controller && !controller.IsLoitering() && input && input.m_iLoiteringType >= 0;
			if (controller && (controller.IsLoiteringOnEntity(owner) || pending))
				controller.StopLoitering(false);
		}

		ELIFE_Poses.SetStarting(m_Local, false);
		m_Local = null;
		super.OnDelete(owner);
		s_aBeds.RemoveItem(this);
		GetGame().GetCallqueue().Remove(WaitForPose);
		GetGame().GetCallqueue().Remove(Turn);
		GetGame().GetCallqueue().Remove(OnLaidDown);
	}
}
