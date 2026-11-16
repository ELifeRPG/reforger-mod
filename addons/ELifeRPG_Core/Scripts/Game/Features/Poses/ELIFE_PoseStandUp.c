//------------------------------------------------------------------------------------------------
//! Stand-up from a seat/bed slot: plays ELIFE_Pose.agf's Out clip, then gets out by teleport (the engine's animated
//! get-out refuses non-vehicles). Runs where the character is owned.
class ELIFE_PoseStandUp
{
	//! Unused by vanilla but declared and synchronized in the character graph; ELIFE_Pose.agf reads it.
	protected static const string VARIABLE = "Trigger01";
	//! Set by ELIFE_Pose.agf once the Out clip ended.
	protected static const string STOOD_UP_TAG = "TagELIFEStoodUp";
	//! The longest Out clip (bed) is about 8.6 s; past this he gets out anyway.
	protected static const int TIMEOUT_MS = 15000;
	//! Back in the idle, the get-out is retried every frame for this long; then he stays seated.
	protected static const int GET_OUT_TRIES_MS = 2000;
	//! Trigger01 stays on at least this long after the get-out was accepted (the engine takes about 0.5 s).
	protected static const int LEAVE_HOLD_MS = 1000;

	//! Out clip playing, since when.
	protected static ref map<ChimeraCharacter, int> s_mSince = new map<ChimeraCharacter, int>();
	//! Clip done, getting out from the idle, since when.
	protected static ref map<ChimeraCharacter, int> s_mGettingOut = new map<ChimeraCharacter, int>();
	//! Get-out accepted; Trigger01 stays on until he is out, or the graph cuts back to the seated idle.
	protected static ref map<ChimeraCharacter, int> s_mLeaving = new map<ChimeraCharacter, int>();
	//! The yaw of the exit point he was sent to, in degrees.
	protected static ref map<ChimeraCharacter, float> s_mExitYaw = new map<ChimeraCharacter, float>();
	//! Local player: where his camera looked when he got out (yaw, pitch in degrees); the aim stays there.
	protected static ref map<ChimeraCharacter, vector> s_mExitLook = new map<ChimeraCharacter, vector>();
	protected static AnimationTagID s_iStoodUpTag = -1;
	//! On the seated/lying idles (ELIFE_Pose.agf); not during the sit-down/lie-down clip.
	protected static AnimationTagID s_iIdleTag = -1;
	//! Asked to stand up during the sit-down/lie-down (NPCs): starts once he sits or lies.
	protected static ref map<ChimeraCharacter, int> s_mPending = new map<ChimeraCharacter, int>();

	//------------------------------------------------------------------------------------------------
	static bool IsStandingUp(IEntity character)
	{
		ChimeraCharacter chimera = ChimeraCharacter.Cast(character);
		return s_mSince.Contains(chimera) || s_mGettingOut.Contains(chimera) || s_mLeaving.Contains(chimera) || s_mPending.Contains(chimera);
	}

	//------------------------------------------------------------------------------------------------
	//! He sits or lies still: the sit-down/lie-down clip is over.
	static bool IsSettled(ChimeraCharacter character)
	{
		if (s_iIdleTag == -1)
		{
			s_iIdleTag = GameAnimationUtils.RegisterAnimationTag("TagVehicleIdle");
			s_iStoodUpTag = GameAnimationUtils.RegisterAnimationTag(STOOD_UP_TAG);
		}

		CharacterAnimationComponent animation = GetAnimation(character);
		return animation && animation.IsPrimaryTag(s_iIdleTag) && !animation.IsPrimaryTag(s_iStoodUpTag);
	}

	//------------------------------------------------------------------------------------------------
	//! The slot is a seat/bed (its get-out is ours), not a vehicle seat.
	static bool IsPoseSlot(BaseCompartmentSlot slot)
	{
		return slot && ELIFE_StandUpAction.Cast(slot.GetGetOutAction());
	}

	//------------------------------------------------------------------------------------------------
	//! Where owned. Dead or unconscious: out of the slot in place so he ragdolls from the pose (eject would teleport him).
	static void Drop(ChimeraCharacter character)
	{
		if (!character || !ELIFE_Poses.IsOwned(character))
			return;

		CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
		if (!access || !IsPoseSlot(access.GetCompartment()))
			return;

		s_mSince.Remove(character);
		s_mGettingOut.Remove(character);
		s_mLeaving.Remove(character);
		s_mPending.Remove(character);
		s_mExitYaw.Remove(character);
		s_mExitLook.Remove(character);
		CharacterAnimationComponent animation = GetAnimation(character);
		if (animation)
			SetStandUp(animation, false);

		vector at[4];
		character.GetWorldTransform(at);
		access.GetOutVehicle_NoDoor(at, true, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Where owned. False if not on a seat/bed or already getting up; during the sit-down clip, queue (NPCs) defers it.
	static bool Start(ChimeraCharacter character, bool queue = false)
	{
		CharacterControllerComponent controller;
		if (character)
			controller = character.GetCharacterController();

		if (!controller || IsStandingUp(character) || controller.IsDead() || controller.IsUnconscious())
			return false;

		CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
		if (!access || access.IsGettingIn() || access.IsGettingOut() || !IsPoseSlot(access.GetCompartment()))
			return false;

		CharacterAnimationComponent animation = GetAnimation(character);
		if (!animation)
			return false;

		if (!IsSettled(character))
		{
			if (!queue)
				return false;

			s_mPending.Set(character, System.GetTickCount());
			GetGame().GetCallqueue().Remove(Check);
			GetGame().GetCallqueue().CallLater(Check, 0, true);
			return true;
		}

		SetStandUp(animation, true);
		s_mSince.Set(character, System.GetTickCount());
		GetGame().GetCallqueue().Remove(Check);
		GetGame().GetCallqueue().CallLater(Check, 0, true);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Every frame while anyone stands up.
	protected static void Check()
	{
		if (s_iStoodUpTag == -1)
			s_iStoodUpTag = GameAnimationUtils.RegisterAnimationTag(STOOD_UP_TAG);

		if (s_iIdleTag == -1)
			s_iIdleTag = GameAnimationUtils.RegisterAnimationTag("TagVehicleIdle");

		int now = System.GetTickCount();
		array<ChimeraCharacter> done = {};
		foreach (ChimeraCharacter character, int since : s_mPending)
		{
			if (!character || !ELIFE_Poses.GetPoseSlot(character) || now - since >= TIMEOUT_MS)
				done.Insert(character);
			else if (IsSettled(character))
				done.Insert(character);
		}

		foreach (ChimeraCharacter character : done)
		{
			s_mPending.Remove(character);
			if (character && ELIFE_Poses.GetPoseSlot(character))
				Start(character);
		}

		done.Clear();
		foreach (ChimeraCharacter character, int since : s_mSince)
		{
			CharacterAnimationComponent animation = GetAnimation(character);
			if (!animation || animation.IsPrimaryTag(s_iStoodUpTag) || now - since >= TIMEOUT_MS)
				done.Insert(character);
		}

		foreach (ChimeraCharacter character : done)
		{
			s_mSince.Remove(character);
			CharacterAnimationComponent animation = GetAnimation(character);
			if (!animation)
				continue;

			// If the engine refuses from the stood-up pose, back to the idle and retry from there.
			if (TryGetOut(character))
			{
				s_mLeaving.Set(character, now);
				continue;
			}

			SetStandUp(animation, false);
			s_mGettingOut.Set(character, now);
		}

		done.Clear();
		foreach (ChimeraCharacter character, int since : s_mGettingOut)
		{
			CharacterAnimationComponent animation = GetAnimation(character);
			// The cut back to the idle shows up a frame later.
			if (animation && animation.IsPrimaryTag(s_iStoodUpTag) && now - since < GET_OUT_TRIES_MS)
				continue;

			if (!animation || TryGetOut(character))
			{
				done.Insert(character);
				if (animation)
					s_mLeaving.Set(character, now);

				continue;
			}

			if (now - since >= GET_OUT_TRIES_MS)
			{
				Print(string.Format("ELIFE_PoseStandUp: get-out of %1 failed", character), LogLevel.WARNING);
				done.Insert(character);
			}
		}

		foreach (ChimeraCharacter character : done)
		{
			s_mGettingOut.Remove(character);
		}

		done.Clear();
		foreach (ChimeraCharacter character, int since : s_mLeaving)
		{
			CompartmentAccessComponent access;
			if (character)
				access = character.GetCompartmentAccessComponent();

			// Otherwise he turns back to where he faced before sitting down.
			if (character && s_mExitYaw.Contains(character))
				FaceYaw(character, s_mExitYaw.Get(character), s_mExitLook.Contains(character), s_mExitLook.Get(character));

			// Off again before the next get-in, which would stand him up at once otherwise.
			if (!access || (!access.IsInCompartment() && now - since >= LEAVE_HOLD_MS) || now - since >= GET_OUT_TRIES_MS)
				done.Insert(character);
		}

		foreach (ChimeraCharacter character : done)
		{
			s_mLeaving.Remove(character);
			s_mExitYaw.Remove(character);
			s_mExitLook.Remove(character);
			CharacterAnimationComponent animation = GetAnimation(character);
			if (animation)
				SetStandUp(animation, false);
		}

		if (s_mSince.IsEmpty() && s_mGettingOut.IsEmpty() && s_mLeaving.IsEmpty() && s_mPending.IsEmpty())
			GetGame().GetCallqueue().Remove(Check);
	}

	//------------------------------------------------------------------------------------------------
	//! The door has GetOutTeleport 1: vanilla's get-out call is the instant get-out at its exit point.
	protected static bool TryGetOut(ChimeraCharacter character)
	{
		CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
		if (!access || !access.GetCompartment())
			return true;

		array<int> doors = {};
		access.GetCompartment().GetAvailableDoorIndices(doors);
		if (doors.IsEmpty())
			return true;

		// The clip ends at the slot's origin facing the slot's way; the door get-outs would face the entry point.
		float yaw = character.GetYawPitchRoll()[0];
		vector exit[4];
		Math3D.AnglesToMatrix(Vector(yaw, 0, 0), exit);
		exit[3] = character.GetOrigin();
		bool ok = access.GetOutVehicle_NoDoor(exit, false, false)
			|| access.GetOutVehicle(EGetOutType.ANIMATED, doors[0], ECloseDoorAfterActions.RETURN_TO_PREVIOUS_STATE, false)
			|| access.GetOutVehicle(EGetOutType.TELEPORT, doors[0], ECloseDoorAfterActions.INVALID, false);
		if (ok)
		{
			s_mExitYaw.Set(character, yaw);
			CameraBase camera;
			if (GetGame().GetCameraManager())
				camera = GetGame().GetCameraManager().CurrentCamera();

			if (camera && character == SCR_PlayerController.GetLocalControlledEntity())
			{
				vector cameraMat[4];
				camera.GetWorldTransform(cameraMat);
				s_mExitLook.Set(character, Math3D.MatrixToAngles(cameraMat));
			}
		}

		return ok;
	}

	//------------------------------------------------------------------------------------------------
	//! Heading and aim face yaw, or (players) the camera's look so the view doesn't jump and W goes where he looks.
	protected static void FaceYaw(notnull ChimeraCharacter character, float yaw, bool keepLook, vector look)
	{
		CharacterControllerComponent controller = character.GetCharacterController();
		if (!controller)
			return;

		if (keepLook)
			yaw = look[0];

		controller.SetHeadingAngle(yaw * Math.DEG2RAD, !keepLook);
		if (!controller.GetInputContext())
			return;

		if (keepLook)
			controller.GetInputContext().SetAimingAngles(Vector(look[0] * Math.DEG2RAD, look[1] * Math.DEG2RAD, 0));
		else
			controller.GetInputContext().SetAimingAngles(Vector(yaw * Math.DEG2RAD, 0, 0));
	}

	//------------------------------------------------------------------------------------------------
	protected static void SetStandUp(notnull CharacterAnimationComponent animation, bool standUp)
	{
		animation.SetVariableBool(animation.BindVariableBool(VARIABLE), standUp);
	}

	//------------------------------------------------------------------------------------------------
	protected static CharacterAnimationComponent GetAnimation(ChimeraCharacter character)
	{
		if (!character || !character.GetCharacterController())
			return null;

		return character.GetCharacterController().GetAnimationComponent();
	}
}

//------------------------------------------------------------------------------------------------
//! Get-out action of seat/bed slots.
class ELIFE_StandUpAction : SCR_GetOutAction
{
	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		ELIFE_PoseStandUp.Start(ChimeraCharacter.Cast(pUserEntity));
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(user);
		return character && !ELIFE_PoseStandUp.IsStandingUp(character) && ELIFE_PoseStandUp.IsSettled(character) && super.CanBePerformedScript(user);
	}

	//------------------------------------------------------------------------------------------------
	static void PerformFor(ChimeraCharacter character)
	{
		if (!character || !character.GetCompartmentAccessComponent())
			return;

		BaseCompartmentSlot slot = character.GetCompartmentAccessComponent().GetCompartment();
		if (!slot)
			return;

		ELIFE_StandUpAction action = ELIFE_StandUpAction.Cast(slot.GetGetOutAction());
		if (action && action.CanBePerformed(character))
			action.PerformAction(slot.GetOwner(), character);
	}
}

//------------------------------------------------------------------------------------------------
modded class SCR_CharacterControllerComponent
{
	//! A stand-up key was down last frame: only a new press stands up, not one still held from before.
	protected bool m_bElifeStandUpKeyDown;

	//------------------------------------------------------------------------------------------------
	override void OnLifeStateChanged(ECharacterLifeState previousLifeState, ECharacterLifeState newLifeState, bool isJIP)
	{
		super.OnLifeStateChanged(previousLifeState, newLifeState, isJIP);

		if (newLifeState != ECharacterLifeState.ALIVE)
			ELIFE_PoseStandUp.Drop(ChimeraCharacter.Cast(GetOwner()));
	}

	//------------------------------------------------------------------------------------------------
	//! Seats/beds: left click and jump stand up too.
	override void OnPrepareControls(IEntity owner, ActionManager am, float dt, bool player)
	{
		super.OnPrepareControls(owner, am, dt, player);

		if (!player)
			return;

		ChimeraCharacter character = ChimeraCharacter.Cast(owner);
		ELIFE_PosePropComponent.ApplyTurnInput(character);
		bool seated = character && character.GetCompartmentAccessComponent() && ELIFE_PoseStandUp.IsPoseSlot(character.GetCompartmentAccessComponent().GetCompartment());
		if (seated)
		{
			// Vanilla keeps these off in compartments; movement holds the mouse look, general the inventory and action keys.
			am.ActivateContext("CharacterWeaponContext");
			am.ActivateContext("CharacterMovementContext");
			am.ActivateContext("CharacterGeneralContext");
		}

		//! shortcut: with a weapon in hand left click also fires; take it out if seated shooting matters.
		bool keyDown = am.GetActionTriggered("CharacterFire") || am.GetActionTriggered("CharacterJump");
		if (seated && keyDown && !m_bElifeStandUpKeyDown)
			ELIFE_StandUpAction.PerformFor(character);

		m_bElifeStandUpKeyDown = keyDown;
	}
}
