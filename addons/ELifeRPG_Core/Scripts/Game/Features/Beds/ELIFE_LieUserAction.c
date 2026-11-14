//------------------------------------------------------------------------------------------------
//! "Lie down" and "Get up" on a bed with ELIFE_BedComponent. The server checks first (one sleeper per bed), then the
//! action runs on every machine: the server takes the bed, the user's owner plays the pose.
class ELIFE_LieUserAction : ScriptedUserAction
{
	//------------------------------------------------------------------------------------------------
	protected ELIFE_BedComponent GetBed()
	{
		return ELIFE_BedComponent.Cast(GetOwner().FindComponent(ELIFE_BedComponent));
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		ELIFE_BedComponent bed = GetBed();
		if (bed && bed.IsLyingHere(SCR_PlayerController.GetLocalControlledEntity()))
			outName = "#ELIFE-Bed_GetUp";
		else
			outName = "#ELIFE-Bed_LieDown";

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		ELIFE_BedComponent bed = GetBed();
		if (!bed || !user)
			return false;

		//! Getting up only once lying still; the lie-down and the get-up can't be cut short. Only the owner knows the
		//! phase: a get-up the server lets through during the lie-down waits for it.
		if (bed.IsLyingHere(user))
			return !ELIFE_Poses.IsOwned(user) || bed.GetPhase(user) == ELIFE_EBedPhase.LYING;

		//! Turning to the start: already on the way, not taken by someone else.
		if (bed.GetPhase(user) != ELIFE_EBedPhase.NONE)
			return false;

		if (bed.IsTakenByOther(ELIFE_BedComponent.SPOT, user))
		{
			SetCannotPerformReason("#ELIFE-Bed_Taken");
			return false;
		}

		//! Also checked here: the server may not ask CanBeShown.
		return bed.IsAtStart(user);
	}

	//------------------------------------------------------------------------------------------------
	//! Vanilla hides actions while a pose plays; this one stays, to get up again.
	override bool CanBeShownScript(IEntity user)
	{
		ELIFE_BedComponent bed = GetBed();
		if (!bed || !user)
			return false;

		if (bed.IsLyingHere(user))
			return true;

		return ELIFE_Poses.CanStartPose(user) && bed.IsAtStart(user);
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		ELIFE_BedComponent bed = GetBed();
		if (!bed || !pUserEntity)
			return;

		if (bed.IsLyingHere(pUserEntity))
			bed.GetUp(pUserEntity);
		else
			bed.LieDown(pUserEntity);
	}

	//------------------------------------------------------------------------------------------------
	override bool CheckOnServerFirstScript()
	{
		return true;
	}
}
