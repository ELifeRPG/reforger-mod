//------------------------------------------------------------------------------------------------
//! "Lie down" and "Get up" on a bed with ELIFE_BedComponent. The server checks first (one sleeper per bed or side), then the
//! action runs on every machine: the server takes the bed, the user's owner plays the pose.
//! Double beds have one action per half (m_iSpot), like the sofa's seats: each with its own state, offered only from
//! that half's side.
class ELIFE_LieUserAction : ScriptedUserAction
{
	[Attribute("-1", UIWidgets.EditBox, "The bed spot this action is for: on a double bed 0 for the half at -X (-Z with the long side along X), 1 for the other. -1: one action for the whole bed.")]
	protected int m_iSpot;

	//------------------------------------------------------------------------------------------------
	protected ELIFE_BedComponent GetBed()
	{
		return ELIFE_BedComponent.Cast(GetOwner().FindComponent(ELIFE_BedComponent));
	}

	//------------------------------------------------------------------------------------------------
	//! The user stands, or lies, on this action's side (always, for a whole-bed action).
	protected bool IsOnMySide(notnull ELIFE_BedComponent bed, notnull IEntity user)
	{
		return m_iSpot < 0 || bed.GetSpotFor(user.GetOrigin()) == m_iSpot;
	}

	//------------------------------------------------------------------------------------------------
	//! Lying on this action's half. Lying, he stays on his side's start (the clips only turn him).
	protected bool IsLyingHere(notnull ELIFE_BedComponent bed, IEntity user)
	{
		return user && bed.IsLyingHere(user) && IsOnMySide(bed, user);
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		ELIFE_BedComponent bed = GetBed();
		if (bed && IsLyingHere(bed, SCR_PlayerController.GetLocalControlledEntity()))
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
		if (IsLyingHere(bed, user))
			return !ELIFE_Poses.IsOwned(user) || bed.GetPhase(user) == ELIFE_EBedPhase.LYING;

		//! Lying on the other half, or turning to the start: not this action.
		if (bed.IsLyingHere(user) || bed.GetPhase(user) != ELIFE_EBedPhase.NONE || !IsOnMySide(bed, user))
			return false;

		if (bed.IsTakenByOther(bed.GetSpotFor(user.GetOrigin()), user))
		{
			SetCannotPerformReason("#ELIFE-Bed_Taken");
			return false;
		}

		//! Also checked here: the server may not ask CanBeShown.
		return bed.IsAtStart(user);
	}

	//------------------------------------------------------------------------------------------------
	//! Vanilla hides actions while a pose plays; this one stays, to get up again. Lying down is only offered from the
	//! action's side.
	override bool CanBeShownScript(IEntity user)
	{
		ELIFE_BedComponent bed = GetBed();
		if (!bed || !user)
			return false;

		if (IsLyingHere(bed, user))
			return true;

		return !bed.IsLyingHere(user) && IsOnMySide(bed, user) && ELIFE_Poses.CanStartPose(user) && bed.IsAtStart(user);
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
