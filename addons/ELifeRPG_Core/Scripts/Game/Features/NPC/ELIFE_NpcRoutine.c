//------------------------------------------------------------------------------------------------
enum ELIFE_ENpcRoutineState
{
	//! At his spot, AI off; the NPC component serves.
	ON_DUTY,
	//! Stuck facing someone (in a doorway, say): a step back, and he waits for them to pass.
	GIVING_WAY,
	//! To the entry point of a seat/bed slot he reserved.
	TO_REST,
	//! Sitting or lying in the slot.
	RESTING,
	//! The stand-up plays; then he walks back (m_bUpForDuty) or picks another place.
	GETTING_UP,
	WANDERING,
	PAUSING,
	RETURNING
}

//------------------------------------------------------------------------------------------------
//! Server-only. Off duty an NPC sits on a free seat or lies on a free bed near his spot (compartment slots of
//! ELIFE_PosePropComponent props), or wanders, using vanilla AI; needs an AI world with a navmesh.
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

	//! Gets in from this close to the entry point; kept small since the get-in teleports him the rest.
	protected static const float REST_FROM_DISTANCE = 0.1;
	//! From this close on, the walk to the entry point is checked every frame: the tick (TICK_MS) is ~0.75 m of walking.
	protected static const float REST_WATCH_DISTANCE = 2;
	protected static const float REST_FROM_MAX_DISTANCE = 1;
	//! The waypoint lies this far past the entry point (vanilla AI stops ~0.85 m short of it), so he walks onto it.
	protected static const float REST_WAYPOINT_PAST = 0.85;
	//! Fresh approaches to the same place after stopping too far out, before he picks another.
	protected static const int SIT_RETRIES = 2;
	//! Out of his slot after this long (ejected, say): another place.
	protected static const float REST_CHECK_S = 2;
	//! The engine refused the get-out (he is seated again): asks again after this long, keeping his place.
	protected static const float GET_UP_RETRY_S = 5;

	protected static const float SEAT_RANGE = 20;
	//! Choice weight is 1 / (this + distance in m): a place 2 m away is about three times as likely as one 15 m away.
	protected static const float SEAT_DISTANCE_WEIGHT = 4;
	//! Breaks are mostly when NPCs sleep: a bed weighs this much more than a seat as far away.
	protected static const float BED_PREFERENCE = 1.5;
	//! The stand-up is over by then at the latest (the bed's takes about 9 s).
	protected static const float GET_UP_TIMEOUT_S = 20;
	protected static const float WANDER_RADIUS = 20;
	//! A walk that takes longer than this has got stuck; he picks something else.
	protected static const float WALK_TIMEOUT_S = 90;
	protected static const float PAUSE_MIN_S = 5;
	protected static const float PAUSE_MAX_S = 15;
	//! Back at his spot he is placed exactly on it if he stopped this close.
	protected static const float HOME_SNAP_DISTANCE = 1;
	//! Somebody else this close to his spot: he waits beside it instead of walking into them.
	protected static const float HOME_CLEAR_RADIUS = 0.6;
	//! Doors ahead are opened this early, so one swinging towards him is open before he gets there (vanilla AI opens
	//! them at the door and gets hit by the leaf).
	protected static const float DOOR_OPEN_RANGE = 2;
	//! A door opens if his planned route (AI path) passes this close to the doorway within DOOR_PATH_AHEAD of route.
	protected static const float DOOR_PATH_RADIUS = 0.6;
	protected static const float DOOR_PATH_AHEAD = 3;
	//! An open door whose doorway he walks this close to counts as his, so he closes it behind him.
	protected static const float DOOR_PASS_RANGE = 1.2;
	//! Without a route: only a door this far off his walking direction.
	protected static const float DOOR_AHEAD_ANGLE = 30;
	//! Doors he opened are closed again once he is this far away (more than the open range, so they don't flap).
	protected static const float DOOR_CLOSE_RANGE = 2.5;
	//! Nobody else may be this close to the door when it closes: the leaf swings through there.
	protected static const float DOOR_CLEAR_RADIUS = 1.5;
	//! Walking, he counts as stuck if he got no further than this in STUCK_S.
	protected static const float STUCK_STEP = 0.3;
	protected static const float STUCK_S = 2;
	//! Stuck, he gives way to somebody in front of him this close.
	protected static const float BLOCKER_RADIUS = 1.5;
	protected static const float GIVE_WAY_DISTANCE = 2;
	//! After this long he stops stepping back (the way back may be blocked too) and waits where he is.
	protected static const float GIVE_WAY_BACK_S = 4;
	//! He walks on once the other is this far from him and from where he got stuck, or after GIVE_WAY_MAX_S.
	protected static const float GIVE_WAY_CLEAR = 2;
	protected static const float GIVE_WAY_MAX_S = 10;
	//! Still blocked after giving way this often on one walk, he goes somewhere else (walking home, he stops giving way).
	protected static const int GIVE_WAY_TRIES = 2;

	protected ELIFE_NpcComponent m_Npc;
	protected IEntity m_Character;
	protected vector m_vHome;

	protected SCR_AIGroup m_Group;
	protected AIWaypoint m_Waypoint;
	//! The place he walks to or rests in, and its slot (reserved for him on the way).
	protected ELIFE_PosePropComponent m_RestProp;
	protected BaseCompartmentSlot m_RestSlot;
	protected vector m_vRestEntry;
	//! Getting up to go back on duty, not to pick another place.
	protected bool m_bUpForDuty;
	protected ELIFE_ENpcRoutineState m_eState = ELIFE_ENpcRoutineState.ON_DUTY;
	protected float m_fStateSeconds;
	protected float m_fPauseSeconds;
	protected vector m_vTarget;
	protected vector m_vLastPosition;
	protected int m_iSitRetries;
	protected vector m_vDoorCheckPosition;
	protected ref array<IEntity> m_aNearbyDoors = {};
	//! The doors he opened or walked through that are still to be closed.
	protected ref array<IEntity> m_aOpenedDoors = {};
	protected bool m_bCharacterNear;
	protected vector m_vStuckPosition;
	protected float m_fStuckSeconds;
	//! Giving way: to whom, where he got stuck, and the walk he goes on with afterwards.
	protected IEntity m_Blocker;
	protected vector m_vGiveWayFrom;
	protected ELIFE_ENpcRoutineState m_eResumeState;
	protected vector m_vResumeTarget;
	protected float m_fResumeSeconds;
	protected int m_iGiveWays;

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
		GetGame().GetCallqueue().Remove(WatchRestEntry);
	}

	//------------------------------------------------------------------------------------------------
	//! Deletes the AI group and waypoint it made; the NPC is going away.
	void Dispose()
	{
		GetGame().GetCallqueue().Remove(Tick);
		GetGame().GetCallqueue().Remove(WatchRestEntry);
		CloseOpenedDoors();
		ReleaseRest();

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
		if (m_eState == ELIFE_ENpcRoutineState.GIVING_WAY)
			return m_eResumeState == ELIFE_ENpcRoutineState.RETURNING;

		return m_eState == ELIFE_ENpcRoutineState.RETURNING || (m_eState == ELIFE_ENpcRoutineState.GETTING_UP && m_bUpForDuty);
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
		// still turning to sit: the turn ends in the slot anyway, the next schedule check gets him up
		if (IsOnDuty() || IsReturning() || ELIFE_PosePropComponent.IsTurningToSit(m_Character))
			return;

		if (IsInRest())
		{
			GetUp(true);
			return;
		}

		ReleaseRest();
		//! AI may still be off from the get-in (ejected, or out of the slot before TickGettingUp ran).
		SetAIActive(true);
		WalkTo(m_vHome, ELIFE_ENpcRoutineState.RETURNING);
	}

	//------------------------------------------------------------------------------------------------
	//! A free seat or bed near his spot, else he wanders. skip: a place not to pick this time (couldn't reach it).
	//! Still sitting or lying, he gets up first and looks once he is out.
	protected void LookForRest(ELIFE_PosePropComponent skip = null)
	{
		if (IsInRest())
		{
			GetUp(false);
			return;
		}

		ReleaseRest();
		array<ELIFE_PosePropComponent> props = {};
		array<BaseCompartmentSlot> slots = {};
		ELIFE_PosePropComponent.FindFree(m_vHome, SEAT_RANGE, true, true, m_Character, props, slots);
		for (int i = props.Count() - 1; i >= 0; i--)
		{
			if (props[i] == skip)
			{
				props.Remove(i);
				slots.Remove(i);
			}
		}

		if (props.IsEmpty())
		{
			Wander();
			return;
		}

		m_iSitRetries = 0;
		int pick = PickRest(props);
		m_RestProp = props[pick];
		m_RestSlot = slots[pick];
		ELIFE_PosePropComponent.Reserve(m_RestSlot, m_Character);
		WalkToRest();
	}

	//------------------------------------------------------------------------------------------------
	//! Random, but nearer places are likelier, so he doesn't always take the same one; beds a little more (BED_PREFERENCE).
	protected int PickRest(notnull array<ELIFE_PosePropComponent> props)
	{
		array<float> weights = {};
		float total;
		foreach (ELIFE_PosePropComponent prop : props)
		{
			float preference = 1;
			if (prop.IsBed())
				preference = BED_PREFERENCE;

			float weight = GetWeight(prop.GetOwner().GetOrigin(), preference);
			weights.Insert(weight);
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
	protected void WalkToRest()
	{
		vector entry[4];
		if (!m_RestSlot || !ELIFE_PosePropComponent.GetEntryTransform(m_RestSlot, entry))
		{
			LookForRest(m_RestProp);
			return;
		}

		m_vRestEntry = entry[3];
		vector past = m_vRestEntry - m_Character.GetOrigin();
		past[1] = 0;
		if (past.Length() > 0.01)
			past.Normalize();

		WalkTo(m_vRestEntry + past * REST_WAYPOINT_PAST, ELIFE_ENpcRoutineState.TO_REST);
	}

	//------------------------------------------------------------------------------------------------
	//! He sits or lies in his slot, or still stands up from it.
	protected bool IsInRest()
	{
		return ELIFE_Poses.GetPoseSlot(m_Character) || ELIFE_PoseStandUp.IsStandingUp(m_Character);
	}

	//------------------------------------------------------------------------------------------------
	//! Frees the slot he reserved (not the one he sits in) and forgets the place.
	protected void ReleaseRest()
	{
		if (m_RestSlot && m_RestSlot.IsReservedBy(m_Character))
			ELIFE_PosePropComponent.Reserve(m_RestSlot, null);

		m_RestProp = null;
		m_RestSlot = null;
	}

	//------------------------------------------------------------------------------------------------
	//! The stand-up; GETTING_UP then walks back (forDuty) or looks for another place once he is out.
	protected void GetUp(bool forDuty)
	{
		m_Npc.SetSleeping(false);
		ReplaceWaypoint(null);
		ELIFE_PoseStandUp.Start(ChimeraCharacter.Cast(m_Character), true);
		m_bUpForDuty = forDuty;
		SetState(ELIFE_ENpcRoutineState.GETTING_UP);
	}

	//------------------------------------------------------------------------------------------------
	protected void Tick()
	{
		CharacterControllerComponent controller = ChimeraCharacter.Cast(m_Character).GetCharacterController();
		if (!controller || controller.IsDead())
		{
			GetGame().GetCallqueue().Remove(Tick);
			CloseOpenedDoors();
			ReleaseRest();
			return;
		}

		m_fStateSeconds += TICK_MS / 1000.0;

		if (IsWalking())
		{
			OpenDoorsAhead();
			//! Without a waypoint he stands on purpose (waiting for his spot).
			if (m_eState != ELIFE_ENpcRoutineState.GIVING_WAY && m_Waypoint)
				CheckBlocked();
		}

		CloseDoorsBehind();

		//! Back on duty the tick only runs on until the doors behind him are closed.
		if (m_eState == ELIFE_ENpcRoutineState.ON_DUTY)
		{
			if (m_aOpenedDoors.IsEmpty())
				GetGame().GetCallqueue().Remove(Tick);

			return;
		}

		switch (m_eState)
		{
			case ELIFE_ENpcRoutineState.GIVING_WAY:
			{
				TickGivingWay();
				return;
			}
			case ELIFE_ENpcRoutineState.TO_REST:
			{
				TickToRest();
				return;
			}
			case ELIFE_ENpcRoutineState.RESTING:
			{
				TickResting();
				return;
			}
			case ELIFE_ENpcRoutineState.GETTING_UP:
			{
				TickGettingUp();
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
	protected bool IsWalking()
	{
		return m_eState == ELIFE_ENpcRoutineState.TO_REST || m_eState == ELIFE_ENpcRoutineState.WANDERING
			|| m_eState == ELIFE_ENpcRoutineState.RETURNING || m_eState == ELIFE_ENpcRoutineState.GIVING_WAY;
	}

	//------------------------------------------------------------------------------------------------
	bool IsGivingWay()
	{
		return m_eState == ELIFE_ENpcRoutineState.GIVING_WAY;
	}

	//------------------------------------------------------------------------------------------------
	//! Stuck with somebody in front of him, he gives way, unless that is an NPC giving way to him already: of two
	//! NPCs stuck in a doorway only the first to notice steps back.
	protected void CheckBlocked()
	{
		vector position = m_Character.GetOrigin();
		if (vector.Distance(position, m_vStuckPosition) > STUCK_STEP)
		{
			m_vStuckPosition = position;
			m_fStuckSeconds = 0;
			return;
		}

		m_fStuckSeconds += TICK_MS / 1000.0;
		if (m_fStuckSeconds < STUCK_S)
			return;

		m_Blocker = null;
		m_Character.GetWorld().QueryEntitiesBySphere(position, BLOCKER_RADIUS, CollectBlocker);
		if (!m_Blocker)
			return;

		ELIFE_NpcComponent other = ELIFE_NpcComponent.Cast(m_Blocker.FindComponent(ELIFE_NpcComponent));
		if (other && other.IsGivingWay())
			return;

		if (m_iGiveWays < GIVE_WAY_TRIES)
		{
			GiveWay();
			return;
		}

		if (m_eState == ELIFE_ENpcRoutineState.TO_REST)
			LookForRest(m_RestProp);
		else if (m_eState == ELIFE_ENpcRoutineState.WANDERING)
			Pause();
	}

	//------------------------------------------------------------------------------------------------
	//! A living character in front of him.
	protected bool CollectBlocker(IEntity entity)
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(entity);
		if (!character || character == m_Character)
			return true;

		CharacterControllerComponent controller = character.GetCharacterController();
		if (!controller || controller.IsDead())
			return true;

		vector toCharacter = character.GetOrigin() - m_Character.GetOrigin();
		if (vector.Dot(toCharacter, m_Character.GetTransformAxis(2)) <= 0)
			return true;

		m_Blocker = character;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! A step back, away from the blocker; the walk he was on is kept for afterwards.
	protected void GiveWay()
	{
		m_eResumeState = m_eState;
		m_vResumeTarget = m_vTarget;
		m_fResumeSeconds = m_fStateSeconds;
		m_vGiveWayFrom = m_Character.GetOrigin();
		m_iGiveWays++;

		vector away = m_vGiveWayFrom - m_Blocker.GetOrigin();
		away[1] = 0;
		away.Normalize();
		WalkTo(m_vGiveWayFrom + away * GIVE_WAY_DISTANCE, ELIFE_ENpcRoutineState.GIVING_WAY);
	}

	//------------------------------------------------------------------------------------------------
	protected void TickGivingWay()
	{
		bool clear = !m_Blocker;
		if (!clear)
		{
			vector blockerPosition = m_Blocker.GetOrigin();
			clear = vector.Distance(blockerPosition, m_vGiveWayFrom) > GIVE_WAY_CLEAR
				&& vector.Distance(blockerPosition, m_Character.GetOrigin()) > GIVE_WAY_CLEAR;
		}

		if (clear || m_fStateSeconds > GIVE_WAY_MAX_S)
		{
			ResumeWalk();
			return;
		}

		//! Without a waypoint he stands and waits.
		if (m_Waypoint && (m_fStateSeconds > GIVE_WAY_BACK_S || HasArrived(m_vTarget)))
			ReplaceWaypoint(null);
	}

	//------------------------------------------------------------------------------------------------
	//! The walk from before giving way. Its timeout counts the wait too, so he can't give way forever.
	protected void ResumeWalk()
	{
		float seconds = m_fResumeSeconds + m_fStateSeconds;
		int giveWays = m_iGiveWays;
		if (m_eResumeState == ELIFE_ENpcRoutineState.TO_REST)
		{
			if (m_RestSlot)
				WalkToRest();
			else
				LookForRest();
		}
		else
		{
			WalkTo(m_vResumeTarget, m_eResumeState);
		}

		if (m_eState == m_eResumeState)
		{
			m_fStateSeconds = seconds;
			m_iGiveWays = giveWays;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Fully opens closed doors on his floor that his route (or, without one, his heading) passes through.
	protected void OpenDoorsAhead()
	{
		vector position = m_Character.GetOrigin();
		vector heading = position - m_vDoorCheckPosition;
		heading[1] = 0;
		m_vDoorCheckPosition = position;

		array<vector> path = {};
		AIControlComponent control = AIControlComponent.Cast(m_Character.FindComponent(AIControlComponent));
		if (control && control.GetControlAIAgent() && control.GetControlAIAgent().GetMovementComponent())
			control.GetControlAIAgent().GetMovementComponent().GetCurrentPath(path);

		m_aNearbyDoors.Clear();
		m_Character.GetWorld().QueryEntitiesBySphere(position, DOOR_OPEN_RANGE, CollectDoor);
		foreach (IEntity doorEntity : m_aNearbyDoors)
		{
			DoorComponent door = DoorComponent.Cast(doorEntity.FindComponent(DoorComponent));
			//! The doorway's middle: the closed leaf's bounds; open, the hinge is near enough for this.
			vector mins;
			vector maxs;
			doorEntity.GetWorldBounds(mins, maxs);
			vector doorway = (mins + maxs) * 0.5;

			bool open = Math.AbsFloat(door.GetNormalizedDoorState()) > 0.01 || door.IsOpening();
			if (open && !m_aOpenedDoors.Contains(doorEntity) && vector.DistanceXZ(doorway, position) <= DOOR_PASS_RANGE)
				m_aOpenedDoors.Insert(doorEntity);

			if (Math.AbsFloat(door.GetNormalizedDoorState()) > 0.99 || door.IsOpening())
				continue;

			if (doorway[1] - position[1] < -1 || doorway[1] - position[1] > 2.5)
				continue;

			if (!IsOnRoute(path, position, heading, doorway))
				continue;

			//! Partly open, it opens further the way it already swings instead of through the doorway.
			float side = 1;
			if (door.GetNormalizedDoorState() < 0)
				side = -1;

			door.SetControlValue(side, RplId.Invalid());
			if (!m_aOpenedDoors.Contains(doorEntity))
				m_aOpenedDoors.Insert(doorEntity);
		}

		m_aNearbyDoors.Clear();
	}

	//------------------------------------------------------------------------------------------------
	//! The next DOOR_PATH_AHEAD of his route passes within DOOR_PATH_RADIUS of the point; without a route, it is ahead.
	protected bool IsOnRoute(notnull array<vector> path, vector position, vector heading, vector point)
	{
		if (path.IsEmpty())
		{
			vector toPoint = point - position;
			toPoint[1] = 0;
			if (heading.LengthSq() < 0.0001 || toPoint.LengthSq() < 0.0001)
				return false;

			return vector.Dot(toPoint.Normalized(), heading.Normalized()) >= Math.Cos(DOOR_AHEAD_ANGLE * Math.DEG2RAD);
		}

		vector from = position;
		float travelled = 0;
		foreach (vector to : path)
		{
			if (DistanceToSegmentXZ(point, from, to) <= DOOR_PATH_RADIUS)
				return true;

			travelled += vector.DistanceXZ(from, to);
			if (travelled >= DOOR_PATH_AHEAD)
				return false;

			from = to;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static float DistanceToSegmentXZ(vector point, vector a, vector b)
	{
		vector ab = b - a;
		ab[1] = 0;
		vector ap = point - a;
		ap[1] = 0;
		float lengthSq = ab.LengthSq();
		float t = 0;
		if (lengthSq > 0.0001)
			t = Math.Clamp(vector.Dot(ap, ab) / lengthSq, 0, 1);

		return (ap - ab * t).Length();
	}

	//------------------------------------------------------------------------------------------------
	//! Closes the doors he opened once he is out of range and nobody else is in the way; ones somebody else closed
	//! meanwhile are forgotten.
	protected void CloseDoorsBehind()
	{
		vector position = m_Character.GetOrigin();
		for (int i = m_aOpenedDoors.Count() - 1; i >= 0; i--)
		{
			IEntity doorEntity = m_aOpenedDoors[i];
			DoorComponent door;
			if (doorEntity)
				door = DoorComponent.Cast(doorEntity.FindComponent(DoorComponent));

			if (!door)
			{
				m_aOpenedDoors.Remove(i);
				continue;
			}

			vector doorPosition = doorEntity.GetOrigin();
			bool nearDoor = IsWalking() && vector.Distance(position, doorPosition) <= DOOR_CLOSE_RANGE;
			if (nearDoor || door.IsOpening())
				continue;

			if (Math.AbsFloat(door.GetNormalizedDoorState()) < 0.01)
			{
				m_aOpenedDoors.Remove(i);
				continue;
			}

			if (IsCharacterNear(doorPosition, DOOR_CLEAR_RADIUS))
				continue;

			door.SetControlValue(0, RplId.Invalid());
			m_aOpenedDoors.Remove(i);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsCharacterNear(vector position, float radius)
	{
		m_bCharacterNear = false;
		m_Character.GetWorld().QueryEntitiesBySphere(position, radius, CheckCharacter);
		return m_bCharacterNear;
	}

	//------------------------------------------------------------------------------------------------
	//! Resting, he counts himself (he'd block a door's leaf); on duty he doesn't, or the tick would never stop.
	protected bool CheckCharacter(IEntity entity)
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(entity);
		if (!character)
			return true;

		CharacterControllerComponent controller = character.GetCharacterController();
		if (!controller || controller.IsDead())
			return true;

		if (entity == m_Character && (IsWalking() || m_eState == ELIFE_ENpcRoutineState.ON_DUTY))
			return true;

		//! Sitting or lying: not in the way of a door leaf or of his spot.
		if (ELIFE_Poses.GetPoseSlot(character))
			return true;

		m_bCharacterNear = true;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! At once, whoever is in the way: he is dead or going away.
	protected void CloseOpenedDoors()
	{
		foreach (IEntity doorEntity : m_aOpenedDoors)
		{
			DoorComponent door;
			if (doorEntity)
				door = DoorComponent.Cast(doorEntity.FindComponent(DoorComponent));

			if (door)
				door.SetControlValue(0, RplId.Invalid());
		}

		m_aOpenedDoors.Clear();
	}

	//------------------------------------------------------------------------------------------------
	protected bool CollectDoor(IEntity entity)
	{
		if (entity.FindComponent(DoorComponent))
			m_aNearbyDoors.Insert(entity);

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void TickToRest()
	{
		if (!m_RestProp || !m_RestSlot || m_fStateSeconds > WALK_TIMEOUT_S || m_RestProp.IsTakenByOther(m_RestSlot, m_Character))
		{
			LookForRest(m_RestProp);
			return;
		}

		//! 3D on purpose: under a place on the floor above he isn't there.
		float distance = vector.Distance(m_Character.GetOrigin(), m_vRestEntry);
		if (distance <= REST_WATCH_DISTANCE && distance > REST_FROM_DISTANCE)
		{
			GetGame().GetCallqueue().Remove(WatchRestEntry);
			GetGame().GetCallqueue().CallLater(WatchRestEntry, 0, true);
		}

		if (distance > REST_FROM_DISTANCE && !HasArrived(m_vTarget))
			return;

		//! Stopped short: walk up again so the get-in doesn't jump him; after the retries, in from REST_FROM_MAX_DISTANCE.
		if (distance > REST_FROM_DISTANCE)
		{
			if (m_iSitRetries < SIT_RETRIES)
			{
				m_iSitRetries++;
				WalkToRest();
				return;
			}

			if (distance > REST_FROM_MAX_DISTANCE)
			{
				LookForRest(m_RestProp);
				return;
			}
		}

		ArriveAtRest();
	}

	//------------------------------------------------------------------------------------------------
	//! Every frame from REST_WATCH_DISTANCE on: stops him on the entry point (the tick alone overshoots it).
	protected void WatchRestEntry()
	{
		if (m_eState != ELIFE_ENpcRoutineState.TO_REST || !m_RestProp || !m_RestSlot)
		{
			GetGame().GetCallqueue().Remove(WatchRestEntry);
			return;
		}

		if (vector.Distance(m_Character.GetOrigin(), m_vRestEntry) > REST_FROM_DISTANCE)
			return;

		GetGame().GetCallqueue().Remove(WatchRestEntry);
		ArriveAtRest();
	}

	//------------------------------------------------------------------------------------------------
	//! AI off for the turn and the stay (it steers his heading); on again once he is out.
	protected void ArriveAtRest()
	{
		GetGame().GetCallqueue().Remove(WatchRestEntry);
		ReplaceWaypoint(null);
		SetAIActive(false);
		if (!m_RestProp.GetIn(ChimeraCharacter.Cast(m_Character), m_RestSlot))
		{
			SetAIActive(true);
			LookForRest(m_RestProp);
			return;
		}

		m_Npc.SetSleeping(m_RestProp.IsBed());
		SetState(ELIFE_ENpcRoutineState.RESTING);
	}

	//------------------------------------------------------------------------------------------------
	//! Nobody can push him out of the slot; out anyway (ejected, a vanilla reaction): another place.
	protected void TickResting()
	{
		if (m_fStateSeconds < REST_CHECK_S || ELIFE_PosePropComponent.IsTurningToSit(m_Character) || ELIFE_Poses.GetPoseSlot(m_Character) == m_RestSlot)
			return;

		m_Npc.SetSleeping(false);
		ReleaseRest();
		SetAIActive(true);
		LookForRest();
	}

	//------------------------------------------------------------------------------------------------
	protected void TickGettingUp()
	{
		if (ELIFE_PoseStandUp.IsStandingUp(m_Character) && m_fStateSeconds <= GET_UP_TIMEOUT_S)
			return;

		//! Still in his slot: the get-out was refused and he sits again. He keeps the place and asks again.
		if (ELIFE_Poses.GetPoseSlot(m_Character))
		{
			if (m_fStateSeconds >= GET_UP_RETRY_S)
				GetUp(m_bUpForDuty);

			return;
		}

		ReleaseRest();
		SetAIActive(true);
		if (m_bUpForDuty)
			WalkTo(m_vHome, ELIFE_ENpcRoutineState.RETURNING);
		else
			LookForRest();
	}

	//------------------------------------------------------------------------------------------------
	protected void TickReturning()
	{
		float distance = vector.Distance(m_Character.GetOrigin(), m_vHome);
		if (distance > ARRIVED_DISTANCE && !HasArrived(m_vHome))
		{
			if (m_fStateSeconds > WALK_TIMEOUT_S)
			{
				int giveWays = m_iGiveWays;
				WalkTo(m_vHome, ELIFE_ENpcRoutineState.RETURNING);
				m_iGiveWays = giveWays;
			}

			return;
		}

		if (IsCharacterNear(m_vHome, HOME_CLEAR_RADIUS))
		{
			ReplaceWaypoint(null);
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

		if (m_aOpenedDoors.IsEmpty())
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
		m_vStuckPosition = m_Character.GetOrigin();
		m_vDoorCheckPosition = m_vStuckPosition;
		m_fStuckSeconds = 0;
		if (state != ELIFE_ENpcRoutineState.GIVING_WAY)
			m_iGiveWays = 0;

		Print(string.Format("%1 | %2: %3", ClassName(), m_Character, typename.EnumToString(ELIFE_ENpcRoutineState, state)), LogLevel.VERBOSE);
	}
}
