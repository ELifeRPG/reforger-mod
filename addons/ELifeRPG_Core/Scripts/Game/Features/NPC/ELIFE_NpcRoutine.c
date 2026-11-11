//------------------------------------------------------------------------------------------------
enum ELIFE_ENpcRoutineState
{
	//! At his spot, AI off; the NPC component serves.
	ON_DUTY,
	TO_SEAT,
	SITTING,
	WANDERING,
	PAUSING,
	RETURNING
}

//------------------------------------------------------------------------------------------------
//! Server-only. Off duty an NPC sits on free seats near his spot or wanders, using vanilla AI; needs an AI world
//! with a navmesh.
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

	protected static const float SEAT_RANGE = 20;
	//! Seat choice weight is 1 / (this + distance in m): a seat 2 m away is about three times as likely as one 15 m away.
	protected static const float SEAT_DISTANCE_WEIGHT = 4;
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
	protected ELIFE_ENpcRoutineState m_eState = ELIFE_ENpcRoutineState.ON_DUTY;
	protected float m_fStateSeconds;
	protected float m_fPauseSeconds;
	protected vector m_vTarget;
	protected vector m_vLastPosition;

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
		return m_eState == ELIFE_ENpcRoutineState.RETURNING;
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

		LookForSeat();
		GetGame().GetCallqueue().Remove(Tick);
		GetGame().GetCallqueue().CallLater(Tick, TICK_MS, true);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! He gets up if he sits and walks back to his spot; the NPC component is told once he is there.
	void ReturnToDuty()
	{
		if (IsOnDuty() || IsReturning())
			return;

		LeaveSeat();
		WalkTo(m_vHome, ELIFE_ENpcRoutineState.RETURNING);
	}

	//------------------------------------------------------------------------------------------------
	protected void LookForSeat()
	{
		LeaveSeat();

		array<SCR_AISmartActionSentinelComponent> seats = {};
		ELIFE_Seats.FindFree(m_vHome, SEAT_RANGE, seats);
		if (seats.IsEmpty())
		{
			Wander();
			return;
		}

		m_Seat = PickSeat(seats);
		vector seatPoint = ELIFE_Seats.GetPosition(m_Seat);
		WalkTo(ELIFE_Seats.GetApproachPoint(m_Seat, -SEAT_WAYPOINT_PAST), ELIFE_ENpcRoutineState.TO_SEAT);
		m_vTarget = seatPoint;
	}

	//------------------------------------------------------------------------------------------------
	//! Random, but nearer seats are likelier, so he doesn't always take the same one.
	protected SCR_AISmartActionSentinelComponent PickSeat(notnull array<SCR_AISmartActionSentinelComponent> seats)
	{
		array<float> weights = {};
		float total;
		foreach (SCR_AISmartActionSentinelComponent seat : seats)
		{
			float weight = 1 / (SEAT_DISTANCE_WEIGHT + vector.Distance(m_vHome, ELIFE_Seats.GetPosition(seat)));
			weights.Insert(weight);
			total += weight;
		}

		float roll = Math.RandomFloat(0, total);
		foreach (int i, float weight : weights)
		{
			roll -= weight;
			if (roll <= 0)
				return seats[i];
		}

		return seats[seats.Count() - 1];
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
			case ELIFE_ENpcRoutineState.TO_SEAT:
			{
				TickToSeat();
				return;
			}
			case ELIFE_ENpcRoutineState.SITTING:
			{
				if (m_fStateSeconds >= SIT_GRACE_S && !ELIFE_Seats.IsSitting(m_Character, m_Seat))
					LookForSeat();

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
					LookForSeat();

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
			LookForSeat();
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
			LookForSeat();
			return;
		}

		SetState(ELIFE_ENpcRoutineState.SITTING);
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
