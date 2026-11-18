//------------------------------------------------------------------------------------------------
class ELIFE_PosePropComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! A seat or bed: characters sit or lie in its compartment slots (get-out ELIFE_StandUpAction). Lists the props for NPCs.
class ELIFE_PosePropComponent : ScriptComponent
{
	[Attribute("0", UIWidgets.CheckBox, "A bed: NPCs lying here sleep, and prefer it a little over a seat.")]
	protected bool m_bBed;

	[Attribute("0", UIWidgets.CheckBox, "Only one occupant at a time although it has more slots (a single bed with a slot per side).")]
	protected bool m_bOneOccupant;

	//! Entry blocked if something is between it and the prop at one of these heights (low: furniture, high: walls).
	protected static const ref array<float> ENTRY_CHECK_HEIGHTS = {0.4, 0.9};
	//! Get-in offered this close (XZ) to the slot's entry point.
	protected static const float GET_IN_RANGE = 0.9;
	//! He turns in place to the slot's facing first; gets in this close to it, once settled, or after TURN_TIMEOUT_MS.
	protected static const float TURN_DONE_DEG = 5;
	//! The body stops ~30 deg behind the heading, so the heading overshoots the facing by this.
	protected static const float TURN_OVERSHOOT_DEG = 35;
	//! Settled: heading arrived this long ago and his facing moved less than this last frame.
	protected static const int TURN_SETTLE_MS = 150;
	protected static const float TURN_SETTLED_DEG = 0.5;
	//! Heading/aim sweep speed; setting the whole heading at once snaps him round.
	protected static const float AIM_DEG_PER_S = 240;
	protected static const int TURN_TIMEOUT_MS = 4000;

	//! Max heading lead over his facing: beyond ~115 deg the engine snaps him round, much less and he turns in bursts.
	protected static const float TURN_MAX_LEAD_DEG = 90;
	//! Settled short of the facing: push the heading this much further, at most this often.
	protected static const float TURN_EXTEND_DEG = 20;
	protected static const int TURN_EXTENDS = 3;
	//! Lets the world around the prop load before the entry check.
	protected static const int ENTRY_CHECK_DELAY_MS = 1000;

	protected static ref array<ELIFE_PosePropComponent> s_aProps = {};
	//! Turning before getting in: slot, its facing (yaw, deg), start time.
	protected static ref map<ChimeraCharacter, BaseCompartmentSlot> s_mTurnSlot = new map<ChimeraCharacter, BaseCompartmentSlot>();
	protected static ref map<ChimeraCharacter, float> s_mTurnYaw = new map<ChimeraCharacter, float>();
	protected static ref map<ChimeraCharacter, int> s_mTurnSince = new map<ChimeraCharacter, int>();
	protected static int s_iTurnTick;
	//! His facing last frame; when the heading arrived (0: not yet).
	protected static ref map<ChimeraCharacter, float> s_mTurnLastYaw = new map<ChimeraCharacter, float>();
	protected static ref map<ChimeraCharacter, int> s_mTurnArrived = new map<ChimeraCharacter, int>();
	//! Heading sweep, unwrapped radians (current and end) so it never wraps the wrong way round.
	protected static ref map<ChimeraCharacter, float> s_mTurnHeadingNow = new map<ChimeraCharacter, float>();
	protected static ref map<ChimeraCharacter, float> s_mTurnHeadingEnd = new map<ChimeraCharacter, float>();
	protected static ref map<ChimeraCharacter, int> s_mTurnExtends = new map<ChimeraCharacter, int>();

	//! Local player's camera axes at get-in, for ELIFE_PoseCamera's first view; kept LOOK_CARRY_MS as several cameras take it.
	protected static const int LOOK_CARRY_MS = 1000;
	protected static ChimeraCharacter s_LookCarryCharacter;
	protected static int s_iLookCarryAt;
	protected static vector s_vLookCarry0;
	protected static vector s_vLookCarry1;
	protected static vector s_vLookCarry2;

	//! Per slot (GetSlots order): entry reachable; empty until checked.
	protected ref array<bool> m_aEntryClear = {};

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		if (SCR_Global.IsEditMode(owner))
			return;

		s_aProps.Insert(this);
		GetGame().GetCallqueue().CallLater(CheckEntries, ENTRY_CHECK_DELAY_MS);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		s_aProps.RemoveItem(this);
		GetGame().GetCallqueue().Remove(CheckEntries);
	}

	//------------------------------------------------------------------------------------------------
	//! Once per machine (props don't move): which entry points aren't behind a wall.
	protected void CheckEntries()
	{
		array<BaseCompartmentSlot> slots = {};
		GetSlots(slots);
		m_aEntryClear.Clear();
		foreach (BaseCompartmentSlot slot : slots)
		{
			vector entry[4];
			bool clear = GetEntryTransform(slot, entry);
			foreach (float height : ENTRY_CHECK_HEIGHTS)
			{
				if (!clear)
					break;

				vector up = Vector(0, height, 0);
				TraceParam trace = new TraceParam();
				trace.Start = GetOwner().GetOrigin() + up;
				trace.End = entry[3] + up;
				trace.Exclude = GetOwner();
				trace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
				trace.LayerMask = EPhysicsLayerPresets.Character;
				clear = GetOwner().GetWorld().TraceMove(trace, IgnoreCharacters) >= 1;
			}

			m_aEntryClear.Insert(clear);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IgnoreCharacters(notnull IEntity e, vector start = "0 0 0", vector dir = "0 0 0")
	{
		return !ChimeraCharacter.Cast(e);
	}

	//------------------------------------------------------------------------------------------------
	//! Not behind a wall (true until checked).
	bool IsEntryClear(notnull BaseCompartmentSlot slot)
	{
		array<BaseCompartmentSlot> slots = {};
		GetSlots(slots);
		int i = slots.Find(slot);
		return i < 0 || i >= m_aEntryClear.Count() || m_aEntryClear[i];
	}

	//------------------------------------------------------------------------------------------------
	//! Reachable, user near its entry point, and on one-occupant props only the reachable slot nearest to him.
	bool ShowsGetIn(notnull BaseCompartmentSlot slot, notnull IEntity user)
	{
		vector entry[4];
		if (!IsEntryClear(slot) || !GetEntryTransform(slot, entry) || vector.DistanceXZ(entry[3], user.GetOrigin()) > GET_IN_RANGE)
			return false;

		if (!m_bOneOccupant)
			return true;

		array<BaseCompartmentSlot> slots = {};
		GetSlots(slots);
		BaseCompartmentSlot nearest;
		float nearestSq = float.MAX;
		foreach (BaseCompartmentSlot other : slots)
		{
			vector otherEntry[4];
			if (!IsEntryClear(other) || !GetEntryTransform(other, otherEntry))
				continue;

			float distanceSq = vector.DistanceSq(otherEntry[3], user.GetOrigin());
			if (distanceSq < nearestSq)
			{
				nearestSq = distanceSq;
				nearest = other;
			}
		}

		return nearest == slot;
	}

	//------------------------------------------------------------------------------------------------
	bool IsBed()
	{
		return m_bBed;
	}

	//------------------------------------------------------------------------------------------------
	static ELIFE_PosePropComponent Get(IEntity prop)
	{
		if (!prop)
			return null;

		return ELIFE_PosePropComponent.Cast(prop.FindComponent(ELIFE_PosePropComponent));
	}

	//------------------------------------------------------------------------------------------------
	void GetSlots(notnull array<BaseCompartmentSlot> slots)
	{
		BaseCompartmentManagerComponent manager = BaseCompartmentManagerComponent.Cast(GetOwner().FindComponent(BaseCompartmentManagerComponent));
		if (manager)
			manager.GetCompartments(slots);
	}

	//------------------------------------------------------------------------------------------------
	//! Someone else is in or (countReservations) reserved it, or on one-occupant props any other slot.
	bool IsTakenByOther(notnull BaseCompartmentSlot slot, IEntity character, bool countReservations = true)
	{
		if (slot.GetOccupant() && slot.GetOccupant() != character)
			return true;

		if (countReservations && slot.IsReserved() && !slot.IsReservedBy(character))
			return true;

		if (!m_bOneOccupant)
			return false;

		array<BaseCompartmentSlot> slots = {};
		GetSlots(slots);
		foreach (BaseCompartmentSlot other : slots)
		{
			if (other == slot)
				continue;

			if (other.GetOccupant() && other.GetOccupant() != character)
				return true;

			if (countReservations && other.IsReserved() && !other.IsReservedBy(character))
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! The slot's door entry point (also where the stand-up ends).
	static bool GetEntryTransform(notnull BaseCompartmentSlot slot, out vector transform[4])
	{
		array<int> doors = {};
		slot.GetAvailableDoorIndices(doors);
		if (doors.IsEmpty() || !slot.GetManager() || !slot.GetManager().GetDoorInfo(doors[0]))
			return false;

		slot.GetManager().GetDoorInfo(doors[0]).GetEntryPointInfo().GetWorldTransform(transform);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! The slot's door exit point, which is the slot's own transform.
	static bool GetExitTransform(notnull BaseCompartmentSlot slot, out vector transform[4])
	{
		array<int> doors = {};
		slot.GetAvailableDoorIndices(doors);
		if (doors.IsEmpty() || !slot.GetManager() || !slot.GetManager().GetDoorInfo(doors[0]))
			return false;

		slot.GetManager().GetDoorInfo(doors[0]).GetExitPointInfo().GetWorldTransform(transform);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Free slots within range, with their props at the same index.
	static void FindFree(vector position, float range, bool seats, bool beds, IEntity character, notnull array<ELIFE_PosePropComponent> props, notnull array<BaseCompartmentSlot> slots)
	{
		float rangeSq = range * range;
		array<BaseCompartmentSlot> propSlots = {};
		foreach (ELIFE_PosePropComponent prop : s_aProps)
		{
			if (!prop || (prop.IsBed() && !beds) || (!prop.IsBed() && !seats))
				continue;

			if (vector.DistanceSq(prop.GetOwner().GetOrigin(), position) > rangeSq)
				continue;

			propSlots.Clear();
			prop.GetSlots(propSlots);
			foreach (BaseCompartmentSlot slot : propSlots)
			{
				if (!prop.IsTakenByOther(slot, character) && prop.IsEntryClear(slot))
				{
					props.Insert(prop);
					slots.Insert(slot);
				}
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Server, NPCs: holds the slot while he walks there; null frees it.
	static void Reserve(BaseCompartmentSlot slot, IEntity character)
	{
		if (slot)
			slot.SetReserved(character);
	}

	//------------------------------------------------------------------------------------------------
	//! Where owned: turns him in place to the slot's facing, then gets in. False if taken or already in one.
	bool GetIn(notnull ChimeraCharacter character, notnull BaseCompartmentSlot slot, bool countReservations = true)
	{
		CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
		if (!access || !character.GetCharacterController() || access.IsInCompartment() || s_mTurnSlot.Contains(character) || IsTakenByOther(slot, character, countReservations))
			return false;

		vector exit[4];
		if (!GetExitTransform(slot, exit))
			return EnterSlot(character, slot);

		s_mTurnSlot.Set(character, slot);
		float facing = Math3D.MatrixToAngles(exit)[0];
		s_mTurnYaw.Set(character, facing);
		float start = character.GetYawPitchRoll()[0];
		CharacterInputContext input = character.GetCharacterController().GetInputContext();
		if (input)
			start = input.GetHeadingAngle() * Math.RAD2DEG;

		// from the heading, not the body yaw: the sweep starts at the heading
		float turn = facing - start;
		while (turn > 180)
			turn -= 360;

		while (turn < -180)
			turn += 360;

		float sweep = turn + TURN_OVERSHOOT_DEG;
		if (turn < 0)
			sweep = turn - TURN_OVERSHOOT_DEG;

		s_mTurnHeadingNow.Set(character, start * Math.DEG2RAD);
		s_mTurnHeadingEnd.Set(character, (start + sweep) * Math.DEG2RAD);
		s_mTurnSince.Set(character, System.GetTickCount());
		if (s_mTurnSlot.Count() == 1)
		{
			s_iTurnTick = System.GetTickCount();
			GetGame().GetCallqueue().CallLater(TickTurns, 0, true);
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Players, from OnPrepareControls: otherwise the camera writes its direction back into the input and he never turns.
	static void ApplyTurnInput(ChimeraCharacter character)
	{
		// arrived: left alone like in TickTurns, else the heading flips near +-180
		if (!character || !s_mTurnHeadingNow.Contains(character) || s_mTurnArrived.Get(character) != 0 || !character.GetCharacterController())
			return;

		CharacterInputContext input = character.GetCharacterController().GetInputContext();
		if (!input)
			return;

		float heading = s_mTurnHeadingNow.Get(character);
		input.SetHeadingAngle(heading);
		vector aim = input.GetAimingAngles();
		aim[0] = heading;
		input.SetAimingAngles(aim);
	}

	//------------------------------------------------------------------------------------------------
	//! He is turning to sit down (not in the slot yet).
	static bool IsTurningToSit(IEntity character)
	{
		return s_mTurnSlot.Contains(ChimeraCharacter.Cast(character));
	}

	//------------------------------------------------------------------------------------------------
	//! Every frame while anyone turns: sweeps heading/aim to the slot's facing, then gets in.
	protected static void TickTurns()
	{
		int now = System.GetTickCount();
		float step = AIM_DEG_PER_S * Math.DEG2RAD * (now - s_iTurnTick) / 1000.0;

		s_iTurnTick = now;

		array<ChimeraCharacter> done = {};
		foreach (ChimeraCharacter character, BaseCompartmentSlot slot : s_mTurnSlot)
		{
			if (!character || !slot || !character.GetCharacterController() || character.GetCharacterController().IsDead())
			{
				done.Insert(character);
				continue;
			}

			float yaw = character.GetYawPitchRoll()[0];
			float diff = s_mTurnYaw.Get(character) - yaw;
			while (diff > 180)
				diff -= 360;

			while (diff < -180)
				diff += 360;

			bool settled = s_mTurnArrived.Get(character) != 0 && now - s_mTurnArrived.Get(character) >= TURN_SETTLE_MS
				&& Math.AbsFloat(yaw - s_mTurnLastYaw.Get(character)) < TURN_SETTLED_DEG;
			s_mTurnLastYaw.Set(character, yaw);
			if (settled && Math.AbsFloat(diff) > TURN_DONE_DEG && s_mTurnExtends.Get(character) < TURN_EXTENDS)
			{
				float extend = TURN_EXTEND_DEG * Math.DEG2RAD;
				if (diff < 0)
					extend = -extend;

				s_mTurnHeadingEnd.Set(character, s_mTurnHeadingEnd.Get(character) + extend);
				s_mTurnExtends.Set(character, s_mTurnExtends.Get(character) + 1);
				s_mTurnArrived.Set(character, 0);
				settled = false;
			}

			if (Math.AbsFloat(diff) <= TURN_DONE_DEG || settled || now - s_mTurnSince.Get(character) >= TURN_TIMEOUT_MS)
			{
				done.Insert(character);
				// walked off during the turn: no teleport back into the seat
				vector entry[4];
				ELIFE_PosePropComponent prop = Get(slot.GetOwner());
				if (prop && (!GetEntryTransform(slot, entry) || vector.DistanceXZ(entry[3], character.GetOrigin()) <= GET_IN_RANGE * 2))
					prop.EnterSlot(character, slot);

				continue;
			}

			CharacterInputContext input = character.GetCharacterController().GetInputContext();
			if (!input)
				continue;

			// arrived: left alone, re-setting a wrapped heading near +-180 flips it every frame
			if (s_mTurnArrived.Get(character) != 0)
				continue;

			float heading = s_mTurnHeadingNow.Get(character);
			float headingDiff = s_mTurnHeadingEnd.Get(character) - heading;
			float lead = heading * Math.RAD2DEG - yaw;
			while (lead > 180)
				lead -= 360;

			while (lead < -180)
				lead += 360;

			// waits for his body once the heading leads by TURN_MAX_LEAD_DEG
			float advance = step;
			if (lead * headingDiff > 0 && Math.AbsFloat(lead) >= TURN_MAX_LEAD_DEG)
				advance = 0;

			heading += Math.Clamp(headingDiff, -advance, advance);
			s_mTurnHeadingNow.Set(character, heading);
			input.SetHeadingAngle(heading);
			if (Math.AbsFloat(headingDiff) <= advance)
				s_mTurnArrived.Set(character, now);

			vector aim = input.GetAimingAngles();
			aim[0] = heading;
			input.SetAimingAngles(aim);
		}

		foreach (ChimeraCharacter character : done)
		{
			s_mTurnSlot.Remove(character);
			s_mTurnYaw.Remove(character);
			s_mTurnSince.Remove(character);
			s_mTurnLastYaw.Remove(character);
			s_mTurnArrived.Remove(character);
			s_mTurnHeadingNow.Remove(character);
			s_mTurnHeadingEnd.Remove(character);
			s_mTurnExtends.Remove(character);
		}

		if (s_mTurnSlot.IsEmpty())
			GetGame().GetCallqueue().Remove(TickTurns);
	}

	//------------------------------------------------------------------------------------------------
	static bool TakeLookCarry(IEntity character, out vector axes[3])
	{
		if (!character || character != s_LookCarryCharacter || System.GetTickCount() - s_iLookCarryAt > LOOK_CARRY_MS)
			return false;

		axes[0] = s_vLookCarry0;
		axes[1] = s_vLookCarry1;
		axes[2] = s_vLookCarry2;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Teleport get-in by the slot's door.
	protected bool EnterSlot(notnull ChimeraCharacter character, notnull BaseCompartmentSlot slot)
	{
		CameraBase camera;
		if (character == SCR_PlayerController.GetLocalControlledEntity() && GetGame().GetCameraManager())
			camera = GetGame().GetCameraManager().CurrentCamera();

		if (camera)
		{
			vector cameraMat[4];
			camera.GetWorldTransform(cameraMat);
			s_vLookCarry0 = cameraMat[0];
			s_vLookCarry1 = cameraMat[1];
			s_vLookCarry2 = cameraMat[2];
			s_LookCarryCharacter = character;
			s_iLookCarryAt = System.GetTickCount();
		}

		CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
		if (!access || access.IsInCompartment() || slot.GetOccupant())
			return false;

		array<int> doors = {};
		slot.GetAvailableDoorIndices(doors);
		int door = -1;
		if (!doors.IsEmpty())
			door = doors[0];

		bool ok = access.GetInVehicle(GetOwner(), slot, true, door, ECloseDoorAfterActions.INVALID, false);
		if (slot.IsReservedBy(character))
			Reserve(slot, null);

		return ok;
	}
}
