//------------------------------------------------------------------------------------------------
enum ELIFE_ENpcRoutineState
{
	//! At his spot, AI off; the NPC component serves.
	ON_DUTY,
	//! After a bad landing: a step back in front of the seat before walking in again.
	BACKING_OFF,
	TO_SEAT,
	SITTING,
	TO_BED,
	LYING,
	//! Pushed off the bed: the get-up plays before he walks back to the start.
	BED_RETRY,
	//! The get-up plays before he walks back.
	GETTING_UP,
	WANDERING,
	PAUSING,
	RETURNING
}

//------------------------------------------------------------------------------------------------
//! Server-only. Off duty an NPC sits on a free seat or lies on a free bed near his spot, or wanders, using vanilla
//! AI; needs an AI world with a navmesh.
class ELIFE_NpcRoutine
{
	protected static const ResourceName GROUP_PREFAB = "{000CD338713F2B5A}Prefabs/AI/Groups/Group_Base.et";
	protected static const ResourceName MOVE_WAYPOINT_PREFAB = "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et";

	protected static const int TICK_MS = 500;
	//! Vanilla's move waypoint completes 5 m early; with this he still stops about 0.85 m short of it.
	protected static const float WAYPOINT_RADIUS = 0.5;
	//! Completed waypoints are deleted later: the group's running activity still reads them that frame.
	protected static const int WAYPOINT_DELETE_DELAY_MS = 2000;
	protected static const float ARRIVED_DISTANCE = 0.8;
	//! Standing still this close to a target also counts as arrived; he stops short of move waypoints.
	protected static const float STOPPED_NEAR_DISTANCE = 3;
	protected static const float STOPPED_STEP = 0.05;

	//! The waypoint lies past the seat point so the AI gets close; it sits down on the way in.
	protected static const float SEAT_WAYPOINT_PAST = 0.35;
	protected static const float SIT_FROM_DISTANCE = 0.5;
	protected static const float SIT_FROM_MAX_DISTANCE = 1.2;
	//! The pose takes a moment to start before losing it counts as being pushed off.
	protected static const float SIT_GRACE_S = 3;
	//! The tolerance check on a seat starts this long after sitting down, once the slide onto it is over.
	protected static const float SIT_CHECK_S = 1.5;
	//! Sitting or lying, he must stay this close to where the pose put him; others can push him off.
	protected static const float REST_TOLERANCE = 0.15;
	//! Fresh approaches to the same seat or bed after a bad landing or stop, before he picks another place.
	protected static const int SIT_RETRIES = 2;
	protected static const float BACK_OFF_DISTANCE = 1.2;

	protected static const float SEAT_RANGE = 20;
	//! Choice weight is 1 / (this + distance in m): a place 2 m away is about three times as likely as one 15 m away.
	protected static const float SEAT_DISTANCE_WEIGHT = 4;
	//! Breaks are mostly when NPCs sleep: a bed weighs this much more than a seat as far away.
	protected static const float BED_PREFERENCE = 1.5;
	//! Lies down on the way in from this close to the bed's start.
	protected static const float LIE_FROM_DISTANCE = 0.5;
	//! Stopped, he still lies down from this close: the turn and the alignment slide him onto the start. Under the
	//! bed's turn cancel distance (reach 0.5 + drift 0.5).
	protected static const float LIE_FROM_MAX_DISTANCE = 0.9;
	//! The waypoint lies this far past the start, inside the bed: he stops short of it (about 0.85 m), at the start.
	protected static const float BED_WAYPOINT_PAST = 0.6;
	//! The turn and the start of the pose come first; losing it before this doesn't count.
	protected static const float LIE_GRACE_S = 6;
	//! The get-up is over by then at the latest (one asked for during the lie-down waits for it: up to ~18 s).
	protected static const float GET_UP_TIMEOUT_S = 25;
	protected static const float WANDER_RADIUS = 20;
	//! A walk that takes longer than this has got stuck; he picks something else.
	protected static const float WALK_TIMEOUT_S = 40;
	protected static const float PAUSE_MIN_S = 5;
	protected static const float PAUSE_MAX_S = 15;
	//! Back at his spot he is placed exactly on it if he stopped this close.
	protected static const float HOME_SNAP_DISTANCE = 1;

	protected ELIFE_NpcComponent m_Npc;
	protected IEntity m_Character;
	protected vector m_vHome;

	protected SCR_AIGroup m_Group;
	protected AIWaypoint m_Waypoint;
	protected SCR_AISmartActionSentinelComponent m_Seat;
	protected ELIFE_BedComponent m_Bed;
	protected ELIFE_ENpcRoutineState m_eState = ELIFE_ENpcRoutineState.ON_DUTY;
	protected float m_fStateSeconds;
	protected float m_fPauseSeconds;
	protected vector m_vTarget;
	protected vector m_vLastPosition;
	protected int m_iSitRetries;

	//------------------------------------------------------------------------------------------------
	void ELIFE_NpcRoutine(notnull ELIFE_NpcComponent npc, vector home)
	{
		m_Npc = npc;
		m_Character = npc.GetOwner();
		m_vHome = home;
	}

	//------------------------------------------------------------------------------------------------
	void ~ELIFE_NpcRoutine()
	{
		GetGame().GetCallqueue().Remove(Tick);
	}

	//------------------------------------------------------------------------------------------------
	//! Deletes the AI group and waypoint it made; the NPC is going away.
	void Dispose()
	{
		GetGame().GetCallqueue().Remove(Tick);

		if (m_Waypoint)
			SCR_EntityHelper.DeleteEntityAndChildren(m_Waypoint);

		if (m_Group)
			SCR_EntityHelper.DeleteEntityAndChildren(m_Group);

		m_Waypoint = null;
		m_Group = null;
	}

	//------------------------------------------------------------------------------------------------
	bool IsOnDuty()
	{
		return m_eState == ELIFE_ENpcRoutineState.ON_DUTY;
	}

	//------------------------------------------------------------------------------------------------
	bool IsReturning()
	{
		return m_eState == ELIFE_ENpcRoutineState.RETURNING || m_eState == ELIFE_ENpcRoutineState.GETTING_UP;
	}

	//------------------------------------------------------------------------------------------------
	//! False if he can't leave his spot (no AI world): then he stays on duty, closed.
	bool GoOffDuty()
	{
		if (!IsOnDuty())
			return true;

		if (!GetGame().GetAIWorld() || !SetAIActive(true))
		{
			Print(string.Format("%1 | no AI world or AI on %2, staying at the spot", ClassName(), m_Character), LogLevel.WARNING);
			return false;
		}

		LookForRest();
		GetGame().GetCallqueue().Remove(Tick);
		GetGame().GetCallqueue().CallLater(Tick, TICK_MS, true);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! He gets up if he sits or lies and walks back to his spot; the NPC component is told once he is there.
	void ReturnToDuty()
	{
		if (IsOnDuty() || IsReturning())
			return;

		LeaveSeat();
		if (LeaveBed())
		{
			ReplaceWaypoint(null);
			SetState(ELIFE_ENpcRoutineState.GETTING_UP);
			return;
		}

		WalkTo(m_vHome, ELIFE_ENpcRoutineState.RETURNING);
	}

	//------------------------------------------------------------------------------------------------
	//! A free seat or bed near his spot, else he wanders. skipSeat, skipBed: not to pick this time, e.g. the one he
	//! kept landing off or couldn't reach.
	protected void LookForRest(SCR_AISmartActionSentinelComponent skipSeat = null, ELIFE_BedComponent skipBed = null)
	{
		LeaveSeat();
		LeaveBed();

		array<SCR_AISmartActionSentinelComponent> seats = {};
		ELIFE_Seats.FindFree(m_vHome, SEAT_RANGE, seats);
		if (skipSeat)
			seats.RemoveItem(skipSeat);

		array<ELIFE_BedComponent> beds = {};
		ELIFE_BedComponent.FindFree(m_vHome, SEAT_RANGE, beds);
		if (skipBed)
			beds.RemoveItem(skipBed);

		if (seats.IsEmpty() && beds.IsEmpty())
		{
			Wander();
			return;
		}

		m_iSitRetries = 0;
		int pick = PickRest(seats, beds);
		if (pick < seats.Count())
		{
			m_Seat = seats[pick];
			WalkToSeat();
			return;
		}

		m_Bed = beds[pick - seats.Count()];
		WalkToBed();
	}

	//------------------------------------------------------------------------------------------------
	//! Random, but nearer places are likelier, so he doesn't always take the same one; beds a little more (BED_PREFERENCE).
	//! Returns an index into seats, then beds after them.
	protected int PickRest(notnull array<SCR_AISmartActionSentinelComponent> seats, notnull array<ELIFE_BedComponent> beds)
	{
		array<float> weights = {};
		float total;
		foreach (SCR_AISmartActionSentinelComponent seat : seats)
		{
			weights.Insert(GetWeight(ELIFE_Seats.GetPosition(seat), 1));
		}

		foreach (ELIFE_BedComponent bed : beds)
		{
			weights.Insert(GetWeight(bed.GetOwner().GetOrigin(), BED_PREFERENCE));
		}

		foreach (float weight : weights)
		{
			total += weight;
		}

		float roll = Math.RandomFloat(0, total);
		foreach (int i, float weight : weights)
		{
			roll -= weight;
			if (roll <= 0)
				return i;
		}

		return weights.Count() - 1;
	}

	//------------------------------------------------------------------------------------------------
	protected float GetWeight(vector position, float preference)
	{
		return preference / (SEAT_DISTANCE_WEIGHT + vector.Distance(m_vHome, position));
	}

	//------------------------------------------------------------------------------------------------
	protected void WalkToSeat()
	{
		vector seatPoint = ELIFE_Seats.GetPosition(m_Seat);
		WalkTo(ELIFE_Seats.GetApproachPoint(m_Seat, -SEAT_WAYPOINT_PAST), ELIFE_ENpcRoutineState.TO_SEAT);
		m_vTarget = seatPoint;
	}

	//------------------------------------------------------------------------------------------------
	//! To the start on the bed's side nearer to him.
	protected void WalkToBed()
	{
		vector transform[4];
		m_Bed.GetStartTransformFrom(m_Character.GetOrigin(), transform);
		//! The start faces away from the bed.
		WalkTo(transform[3] - transform[2] * BED_WAYPOINT_PAST, ELIFE_ENpcRoutineState.TO_BED);
	}

	//------------------------------------------------------------------------------------------------
	protected void Wander()
	{
		float angle = Math.RandomFloat(0, Math.PI2);
		float distance = Math.RandomFloat(WANDER_RADIUS * 0.3, WANDER_RADIUS);
		vector target = m_vHome + Vector(Math.Cos(angle) * distance, 0, Math.Sin(angle) * distance);
		target[1] = GetGame().GetWorld().GetSurfaceY(target[0], target[2]);
		WalkTo(target, ELIFE_ENpcRoutineState.WANDERING);
	}

	//------------------------------------------------------------------------------------------------
	protected void Pause()
	{
		ReplaceWaypoint(null);
		m_fPauseSeconds = Math.RandomFloat(PAUSE_MIN_S, PAUSE_MAX_S);
		SetState(ELIFE_ENpcRoutineState.PAUSING);
	}

	//------------------------------------------------------------------------------------------------
	protected void LeaveSeat()
	{
		if (m_Seat)
			ELIFE_Seats.StandUp(m_Character, m_Seat);

		m_Seat = null;
	}

	//------------------------------------------------------------------------------------------------
	//! True if he lies (or lies down) there and now gets up; the get-up takes a while.
	protected bool LeaveBed()
	{
		m_Npc.SetSleeping(false);
		ELIFE_BedComponent bed = m_Bed;
		m_Bed = null;
		if (!bed || (!bed.IsLyingHere(m_Character) && bed.GetPhase(m_Character) == ELIFE_EBedPhase.NONE))
			return false;

		bed.GetUp(m_Character);
		m_Bed = bed;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void Tick()
	{
		CharacterControllerComponent controller = ChimeraCharacter.Cast(m_Character).GetCharacterController();
		if (!controller || controller.IsDead())
		{
			GetGame().GetCallqueue().Remove(Tick);
			return;
		}

		m_fStateSeconds += TICK_MS / 1000.0;

		switch (m_eState)
		{
			case ELIFE_ENpcRoutineState.BACKING_OFF:
			{
				TickBackingOff();
				return;
			}
			case ELIFE_ENpcRoutineState.TO_SEAT:
			{
				TickToSeat();
				return;
			}
			case ELIFE_ENpcRoutineState.SITTING:
			{
				TickResting(SIT_CHECK_S, SIT_GRACE_S);
				return;
			}
			case ELIFE_ENpcRoutineState.TO_BED:
			{
				TickToBed();
				return;
			}
			case ELIFE_ENpcRoutineState.LYING:
			{
				//! The bed's pose only counts once it has started (after the turn), so no extra check delay.
				TickResting(0, LIE_GRACE_S);
				return;
			}
			case ELIFE_ENpcRoutineState.BED_RETRY:
			{
				if (!m_Bed)
				{
					LookForRest();
					return;
				}

				bool risen = !m_Bed.IsLyingHere(m_Character) && m_Bed.GetPhase(m_Character) != ELIFE_EBedPhase.LYING_DOWN;
				if (!risen && m_fStateSeconds <= GET_UP_TIMEOUT_S)
					return;

				FinishStuckGetUp();
				if (m_iSitRetries > SIT_RETRIES)
					LookForRest(null, m_Bed);
				else
					WalkToBed();

				return;
			}
			case ELIFE_ENpcRoutineState.GETTING_UP:
			{
				bool up = !m_Bed || (!m_Bed.IsLyingHere(m_Character) && m_Bed.GetPhase(m_Character) != ELIFE_EBedPhase.LYING_DOWN);
				if (up || m_fStateSeconds > GET_UP_TIMEOUT_S)
				{
					FinishStuckGetUp();
					m_Bed = null;
					WalkTo(m_vHome, ELIFE_ENpcRoutineState.RETURNING);
				}

				return;
			}
			case ELIFE_ENpcRoutineState.WANDERING:
			{
				if (HasArrived(m_vTarget) || m_fStateSeconds > WALK_TIMEOUT_S)
					Pause();

				return;
			}
			case ELIFE_ENpcRoutineState.PAUSING:
			{
				if (m_fStateSeconds >= m_fPauseSeconds)
					LookForRest();

				return;
			}
			case ELIFE_ENpcRoutineState.RETURNING:
			{
				TickReturning();
				return;
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void TickToSeat()
	{
		if (!m_Seat || m_fStateSeconds > WALK_TIMEOUT_S)
		{
			LookForRest();
			return;
		}

		float distance = vector.Distance(m_Character.GetOrigin(), m_vTarget);
		if (distance > SIT_FROM_DISTANCE && !HasArrived(ELIFE_Seats.GetApproachPoint(m_Seat, -SEAT_WAYPOINT_PAST)))
			return;

		//! Stopped too far out: once more.
		if (distance > SIT_FROM_MAX_DISTANCE)
		{
			WalkTo(ELIFE_Seats.GetApproachPoint(m_Seat, -SEAT_WAYPOINT_PAST), ELIFE_ENpcRoutineState.TO_SEAT);
			return;
		}

		//! Without a waypoint the AI idles and leaves the pose alone. Taken meanwhile: SitDown says no.
		ReplaceWaypoint(null);
		if (!ELIFE_Seats.SitDown(m_Character, m_Seat))
		{
			m_Seat = null;
			LookForRest();
			return;
		}

		SetState(ELIFE_ENpcRoutineState.SITTING);
	}

	//------------------------------------------------------------------------------------------------
	protected void TickToBed()
	{
		if (!m_Bed || m_fStateSeconds > WALK_TIMEOUT_S)
		{
			LookForRest(null, m_Bed);
			return;
		}

		vector transform[4];
		m_Bed.GetStartTransformFrom(m_Character.GetOrigin(), transform);
		float distance = vector.DistanceXZ(m_Character.GetOrigin(), transform[3]);
		if (distance > LIE_FROM_DISTANCE && !HasArrived(m_vTarget))
			return;

		//! Stopped too far out: once more, then another place.
		if (distance > LIE_FROM_MAX_DISTANCE)
		{
			Print(string.Format("%1 | %2 stopped %3 m from the bed's start (try %4)", ClassName(), m_Character, distance, m_iSitRetries + 1), LogLevel.VERBOSE);
			if (m_iSitRetries >= SIT_RETRIES)
			{
				LookForRest(null, m_Bed);
				return;
			}

			m_iSitRetries++;
			WalkToBed();
			return;
		}

		//! Without a waypoint the AI idles and leaves the pose alone. Taken meanwhile: LieDown says no.
		ReplaceWaypoint(null);
		Print(string.Format("%1 | %2 lies down %3 m from the bed's start", ClassName(), m_Character, distance), LogLevel.VERBOSE);
		if (!m_Bed.LieDown(m_Character))
		{
			Print(string.Format("%1 | %2: bed taken or without animations", ClassName(), m_Character), LogLevel.VERBOSE);
			LookForRest(null, m_Bed);
			return;
		}

		m_Npc.SetSleeping(true);
		SetState(ELIFE_ENpcRoutineState.LYING);
	}

	//------------------------------------------------------------------------------------------------
	//! Sitting or lying. Pushed off the spot he takes it again (RetryRest). The pose broken some other way (unclear
	//! why): a fresh random pick, seat or bed.
	protected void TickResting(float checkSeconds, float graceSeconds)
	{
		if (!m_Seat && !m_Bed)
		{
			LookForRest();
			return;
		}

		bool inPose = IsInRestPose();
		float error = GetRestError();
		if (inPose && m_fStateSeconds >= checkSeconds && error > REST_TOLERANCE)
		{
			Print(string.Format("%1 | %2 pushed %3 m off his place", ClassName(), m_Character, error), LogLevel.VERBOSE);
			RetryRest();
			return;
		}

		if (!inPose && m_fStateSeconds >= graceSeconds && !IsRestStarting())
		{
			Print(string.Format("%1 | %2 lost the pose", ClassName(), m_Character), LogLevel.VERBOSE);
			LookForRest();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsInRestPose()
	{
		if (m_Seat)
			return ELIFE_Seats.IsSitting(m_Character, m_Seat);

		return m_Bed && m_Bed.IsLyingHere(m_Character);
	}

	//------------------------------------------------------------------------------------------------
	//! The bed's pose is still on its way (turning, held back by vanilla); a seat's starts at once.
	protected bool IsRestStarting()
	{
		return m_Bed && m_Bed.GetPhase(m_Character) != ELIFE_EBedPhase.NONE;
	}

	//------------------------------------------------------------------------------------------------
	//! How far, along the ground, he is from where the pose put him.
	protected float GetRestError()
	{
		if (m_Seat)
			return ELIFE_Seats.GetSitError(m_Character, m_Seat);

		if (m_Bed)
			return m_Bed.GetLieError(m_Character);

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! The same place again: up, a fresh approach, down; another place after SIT_RETRIES.
	protected void RetryRest()
	{
		m_iSitRetries++;
		if (m_Bed)
		{
			RetryBed();
			return;
		}

		if (m_iSitRetries > SIT_RETRIES)
		{
			LookForRest(m_Seat);
			return;
		}

		RetrySeat();
	}

	//------------------------------------------------------------------------------------------------
	//! Off a bed he always gets up first (a waypoint would cut the get-up short); BED_RETRY then walks back or picks
	//! another place.
	protected void RetryBed()
	{
		m_Npc.SetSleeping(false);
		ReplaceWaypoint(null);
		m_Bed.GetUp(m_Character);
		SetState(ELIFE_ENpcRoutineState.BED_RETRY);
	}

	//------------------------------------------------------------------------------------------------
	//! The get-up timed out but he still lies there: vanilla's stop can hang on a pushed server-owned character.
	protected void FinishStuckGetUp()
	{
		if (m_Bed && m_Bed.IsLyingHere(m_Character))
			ELIFE_Poses.FinishPose(m_Character);
	}

	//------------------------------------------------------------------------------------------------
	//! Up again and a fresh approach from a step back.
	protected void RetrySeat()
	{
		ELIFE_Seats.StandUp(m_Character, m_Seat);
		//! Sitting again from where he is would land the same; the walk in has to start afresh.
		WalkTo(ELIFE_Seats.GetApproachPoint(m_Seat, BACK_OFF_DISTANCE), ELIFE_ENpcRoutineState.BACKING_OFF);
	}

	//------------------------------------------------------------------------------------------------
	protected void TickBackingOff()
	{
		if (!m_Seat || m_fStateSeconds > WALK_TIMEOUT_S)
		{
			LookForRest();
			return;
		}

		if (!HasArrived(m_vTarget))
			return;

		WalkToSeat();
	}

	//------------------------------------------------------------------------------------------------
	protected void TickReturning()
	{
		float distance = vector.Distance(m_Character.GetOrigin(), m_vHome);
		if (distance > ARRIVED_DISTANCE && !HasArrived(m_vHome))
		{
			if (m_fStateSeconds > WALK_TIMEOUT_S)
				WalkTo(m_vHome, ELIFE_ENpcRoutineState.RETURNING);

			return;
		}

		if (distance > HOME_SNAP_DISTANCE)
		{
			WalkTo(m_vHome, ELIFE_ENpcRoutineState.RETURNING);
			return;
		}

		ReplaceWaypoint(null);
		SetAIActive(false);

		ChimeraCharacter character = ChimeraCharacter.Cast(m_Character);
		vector transform[4];
		character.GetWorldTransform(transform);
		transform[3] = m_vHome;
		character.Teleport(transform);

		GetGame().GetCallqueue().Remove(Tick);
		SetState(ELIFE_ENpcRoutineState.ON_DUTY);
		m_Npc.OnBackOnDuty();
	}

	//------------------------------------------------------------------------------------------------
	protected void WalkTo(vector position, ELIFE_ENpcRoutineState state)
	{
		vector transform[4];
		Math3D.MatrixIdentity4(transform);
		transform[3] = position;

		AIWaypoint waypoint = AIWaypoint.Cast(Spawn(MOVE_WAYPOINT_PREFAB, transform));
		if (waypoint)
			waypoint.SetCompletionRadius(WAYPOINT_RADIUS);

		ReplaceWaypoint(waypoint);
		m_vTarget = position;
		SetState(state);
	}

	//------------------------------------------------------------------------------------------------
	protected void ReplaceWaypoint(AIWaypoint waypoint)
	{
		if (m_Waypoint && m_Group)
		{
			m_Group.CompleteWaypoint(m_Waypoint);
			GetGame().GetCallqueue().CallLater(SCR_EntityHelper.DeleteEntityAndChildren, WAYPOINT_DELETE_DELAY_MS, false, m_Waypoint);
		}

		m_Waypoint = waypoint;
		if (m_Waypoint && m_Group)
			m_Group.AddWaypoint(m_Waypoint);
	}

	//------------------------------------------------------------------------------------------------
	//! On: in his own AI group, walking. Off: no AI, so nothing but the NPC component moves him.
	protected bool SetAIActive(bool active)
	{
		AIControlComponent control = AIControlComponent.Cast(m_Character.FindComponent(AIControlComponent));
		if (!control)
			return false;

		if (!active)
		{
			control.DeactivateAI();
			return true;
		}

		if (!m_Group)
		{
			vector transform[4];
			Math3D.MatrixIdentity4(transform);
			transform[3] = m_Character.GetOrigin();
			m_Group = SCR_AIGroup.Cast(Spawn(GROUP_PREFAB, transform));
			if (!m_Group || !m_Group.AddAIEntityToGroup(m_Character))
				return false;

			SCR_AIGroupSettingsComponent settings = SCR_AIGroupSettingsComponent.Cast(m_Group.FindComponent(SCR_AIGroupSettingsComponent));
			if (settings)
				settings.AddSetting(SCR_AIGroupCharactersMovementSpeedSetting.Create(SCR_EAISettingOrigin.SCENARIO, EMovementType.WALK), false, true);

			return true;
		}

		control.ActivateAI();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected IEntity Spawn(ResourceName prefab, vector transform[4])
	{
		Resource resource = Resource.Load(prefab);
		if (!resource || !resource.IsValid())
			return null;

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		params.Transform = transform;
		return GetGame().SpawnEntityPrefab(resource, m_Character.GetWorld(), params);
	}

	//------------------------------------------------------------------------------------------------
	protected bool HasArrived(vector target)
	{
		vector position = m_Character.GetOrigin();
		float step = vector.Distance(position, m_vLastPosition);
		m_vLastPosition = position;

		vector delta = target - position;
		delta[1] = 0;
		float distance = delta.Length();
		if (distance <= ARRIVED_DISTANCE)
			return true;

		return m_fStateSeconds >= 1 && step <= STOPPED_STEP && distance <= STOPPED_NEAR_DISTANCE;
	}

	//------------------------------------------------------------------------------------------------
	protected void SetState(ELIFE_ENpcRoutineState state)
	{
		m_eState = state;
		m_fStateSeconds = 0;
		Print(string.Format("%1 | %2: %3", ClassName(), m_Character, typename.EnumToString(ELIFE_ENpcRoutineState, state)), LogLevel.VERBOSE);
	}
}
