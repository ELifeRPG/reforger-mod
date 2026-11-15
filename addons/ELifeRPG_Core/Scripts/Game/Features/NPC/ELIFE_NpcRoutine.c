//------------------------------------------------------------------------------------------------
enum ELIFE_ENpcRoutineState
{
	//! At his spot, AI off; the NPC component serves.
	ON_DUTY,
	//! After a bad landing: a step back in front of the seat before walking in again.
	BACKING_OFF,
	//! Stuck facing someone (in a doorway, say): a step back, and he waits for them to pass.
	GIVING_WAY,
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
	//! Doors this close are opened whichever way he is heading.
	protected static const float DOOR_OPEN_NEAR = 1.5;
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
	protected SCR_AISmartActionSentinelComponent m_Seat;
	protected ELIFE_BedComponent m_Bed;
	//! The start WalkToBed picked: on a double bed maybe the far side, not the one he is on now.
	protected vector m_vBedStart;
	protected ELIFE_ENpcRoutineState m_eState = ELIFE_ENpcRoutineState.ON_DUTY;
	protected float m_fStateSeconds;
	protected float m_fPauseSeconds;
	protected vector m_vTarget;
	protected vector m_vLastPosition;
	protected int m_iSitRetries;
	protected vector m_vDoorCheckPosition;
	protected ref array<IEntity> m_aNearbyDoors = {};
	//! The doors he opened that are still to be closed.
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
	}

	//------------------------------------------------------------------------------------------------
	//! Deletes the AI group and waypoint it made; the NPC is going away.
	void Dispose()
	{
		GetGame().GetCallqueue().Remove(Tick);
		CloseOpenedDoors();

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
	//! To the start on a free side of the bed, the nearer one first (double beds take one sleeper per side).
	protected void WalkToBed()
	{
		vector transform[4];
		if (!m_Bed.GetFreeStartTransform(m_Character, transform))
		{
			LookForRest(null, m_Bed);
			return;
		}

		m_vBedStart = transform[3];
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
			CloseOpenedDoors();
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
			case ELIFE_ENpcRoutineState.BACKING_OFF:
			{
				TickBackingOff();
				return;
			}
			case ELIFE_ENpcRoutineState.GIVING_WAY:
			{
				TickGivingWay();
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
	protected bool IsWalking()
	{
		return m_eState == ELIFE_ENpcRoutineState.TO_SEAT || m_eState == ELIFE_ENpcRoutineState.TO_BED
			|| m_eState == ELIFE_ENpcRoutineState.BACKING_OFF || m_eState == ELIFE_ENpcRoutineState.WANDERING
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

		if (m_eState == ELIFE_ENpcRoutineState.TO_SEAT || m_eState == ELIFE_ENpcRoutineState.BACKING_OFF)
			LookForRest(m_Seat);
		else if (m_eState == ELIFE_ENpcRoutineState.TO_BED)
			LookForRest(null, m_Bed);
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
		if (m_eResumeState == ELIFE_ENpcRoutineState.TO_SEAT)
		{
			if (m_Seat)
				WalkToSeat();
			else
				LookForRest();
		}
		else if (m_eResumeState == ELIFE_ENpcRoutineState.TO_BED)
		{
			if (m_Bed)
				WalkToBed();
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
	//! Opens closed doors on his floor ahead of him (or right next to him) fully, like vanilla's SCR_DoorOpenerEntity.
	protected void OpenDoorsAhead()
	{
		vector position = m_Character.GetOrigin();
		vector heading = position - m_vDoorCheckPosition;
		heading[1] = 0;
		m_vDoorCheckPosition = position;

		m_aNearbyDoors.Clear();
		m_Character.GetWorld().QueryEntitiesBySphere(position, DOOR_OPEN_RANGE, CollectDoor);
		foreach (IEntity doorEntity : m_aNearbyDoors)
		{
			DoorComponent door = DoorComponent.Cast(doorEntity.FindComponent(DoorComponent));
			if (Math.AbsFloat(door.GetNormalizedDoorState()) > 0.99 || door.IsOpening())
				continue;

			vector toDoor = doorEntity.GetOrigin() - position;
			if (toDoor[1] < -1 || toDoor[1] > 2)
				continue;

			toDoor[1] = 0;
			if (toDoor.Length() > DOOR_OPEN_NEAR && vector.Dot(toDoor, heading) <= 0)
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

		//! Stopped too far out (someone in the way, say): once more, then another place.
		if (distance > SIT_FROM_MAX_DISTANCE)
		{
			if (m_iSitRetries >= SIT_RETRIES)
			{
				LookForRest(m_Seat);
				return;
			}

			m_iSitRetries++;
			WalkToSeat();
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

		//! Within LIE_FROM_MAX_DISTANCE of it he is on its side, so LieDown takes that side's spot.
		float distance = vector.DistanceXZ(m_Character.GetOrigin(), m_vBedStart);
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
