//------------------------------------------------------------------------------------------------
//! Seats/beds aren't group vehicles: vanilla would log a missing SCR_AIVehicleUsageComponent on every get-in/out.
modded class SCR_AIGroupUtilityComponent
{
	//------------------------------------------------------------------------------------------------
	override protected void OnAgentCompartmentEntered(AIAgent agent, IEntity targetEntity, BaseCompartmentManagerComponent manager, int mgrID, int slotID, bool move)
	{
		if (ELIFE_PosePropComponent.Get(targetEntity))
			return;

		super.OnAgentCompartmentEntered(agent, targetEntity, manager, mgrID, slotID, move);
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnAgentCompartmentLeft(AIAgent agent, IEntity targetEntity, BaseCompartmentManagerComponent manager, int mgrID, int slotID, bool move)
	{
		if (ELIFE_PosePropComponent.Get(targetEntity))
			return;

		super.OnAgentCompartmentLeft(agent, targetEntity, manager, mgrID, slotID, move);
	}
}
