//------------------------------------------------------------------------------------------------
//! Vanilla limits a character in a compartment to the vehicle's actions and no gadgets; on seats/beds he keeps his normal interactions and gadgets.
modded class SCR_InteractionHandlerComponent
{
	//------------------------------------------------------------------------------------------------
	protected override bool DoIntersectCheck(IEntity controlledEntity)
	{
		if (ELIFE_Poses.GetPoseSlot(controlledEntity))
			return false;

		return super.DoIntersectCheck(controlledEntity);
	}

	//------------------------------------------------------------------------------------------------
	override protected bool HandleVehicle(notnull ChimeraCharacter character)
	{
		if (ELIFE_Poses.GetPoseSlot(character))
			return false;

		return super.HandleVehicle(character);
	}
}

//------------------------------------------------------------------------------------------------
modded class SCR_GadgetManagerComponent
{
	//------------------------------------------------------------------------------------------------
	override protected bool CanChangeGadgetMode(IEntity gadget, SCR_GadgetComponent gadgetComp, EGadgetMode targetMode)
	{
		if (ELIFE_Poses.GetPoseSlot(GetOwner()))
			return true;

		return super.CanChangeGadgetMode(gadget, gadgetComp, targetMode);
	}
}
