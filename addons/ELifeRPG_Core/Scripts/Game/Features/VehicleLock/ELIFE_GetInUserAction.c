modded class SCR_GetInUserAction : SCR_CompartmentUserAction
{
	protected ELIFE_VehicleLockComponent m_pVehicleLockComponent;

	//------------------------------------------------------------------------------------------------
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		super.Init(pOwnerEntity, pManagerComponent);
		
		IEntity vehicle = SCR_EntityHelper.GetMainParent(pOwnerEntity, true);
		if (!vehicle)
			return;
		
		m_pVehicleLockComponent = ELIFE_VehicleLockComponent.Cast(vehicle.FindComponent(ELIFE_VehicleLockComponent));
	}
	
	//------------------------------------------------------------------------------------------------
	//! ELIFE | Seats/beds: their own get-in (a seat turns the character in place first).
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		BaseCompartmentSlot compartment = GetCompartmentSlot();
		ChimeraCharacter character = ChimeraCharacter.Cast(pUserEntity);
		ELIFE_PosePropComponent poseProp;
		if (compartment)
			poseProp = ELIFE_PosePropComponent.Get(compartment.GetOwner());

		if (!poseProp || !character)
		{
			super.PerformAction(pOwnerEntity, pUserEntity);
			return;
		}

		poseProp.GetIn(character, compartment, false);
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		// ELIFE | Lock vehicle with VehicleLockComponent
		if (m_pVehicleLockComponent && m_pVehicleLockComponent.IsVehicleLocked())
		{
			SetCannotPerformReason(m_pVehicleLockComponent.GetCannotPerformReason(user));
			return false;
		}
		
		BaseCompartmentSlot compartment = GetCompartmentSlot();
		if (!compartment)
			return false;
		
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(user);
		if (!character)
			return false;

		if (SCR_ScriptedUserAction.IsUserPlayingBlockingAnimation(character))
			return false;

		CompartmentAccessComponent compartmentAccess = character.GetCompartmentAccessComponent();
		if (!compartmentAccess)
			return false;
		
		IEntity owner = compartment.GetOwner();
		Vehicle vehicle = Vehicle.Cast(SCR_EntityHelper.GetMainParent(owner, true));
		if (vehicle)
		{			
			Faction characterFaction = character.GetFaction();
			Faction vehicleFaction = vehicle.GetFaction();
			if (characterFaction && vehicleFaction && characterFaction.IsFactionEnemy(vehicleFaction))
			{
				SetCannotPerformReason("#AR-UserAction_SeatHostile");
				return false;
			}
		}
		
		// ELIFE | Seats/beds: also taken via the other side of a single bed; NPC reservations don't block players.
		ELIFE_PosePropComponent poseProp = ELIFE_PosePropComponent.Get(owner);
		if (compartment.GetOccupant() || m_CompartmentManager.IsGetInAndOutBlockedByDoorUser(GetRelevantDoorIndex(user)) || (poseProp && poseProp.IsTakenByOther(compartment, user, false)))
		{
			SetCannotPerformReason("#AR-UserAction_SeatOccupied");
			return false;
		}
		
		// Check if the position isn't lock.
		if (m_pLockComp && m_pLockComp.IsLocked(user, compartment))
		{
			SetCannotPerformReason(m_pLockComp.GetCannotPerformReason(user));
			return false;
		}
		
		if (!poseProp && compartment.IsReserved() && !compartment.IsReservedBy(user))
		{
			SetCannotPerformReason("#AR-Campaign_Action_CannotEnterVehicle-UC");
			return false;
		}

		// ELIFE | Not for seats/beds: the furniture itself counts as obstruction there; IsEntryClear covers walls.
		if (!poseProp && !compartmentAccess.CanGetInVehicleViaDoor(owner, m_CompartmentManager, GetRelevantDoorIndex(user)))
		{
			SetCannotPerformReason("#AR-UserAction_SeatObstructed");
			return false;
		}
		
		return true;
	}	
	
	//------------------------------------------------------------------------------------------------+
	override bool CanBeShownScript(IEntity user)
	{
		// ELIFE | Lock vehicle with VehicleLockComponent
		if (m_pVehicleLockComponent && m_pVehicleLockComponent.IsVehicleLocked())
		{
			return false;
		}
		
		BaseCompartmentSlot compartment = GetCompartmentSlot();
		if (!compartment)
			return false;
		
		ChimeraCharacter character = ChimeraCharacter.Cast(user);
		if (!character || character.IsInVehicle())
			return false;
		
		CompartmentAccessComponent compartmentAccess = character.GetCompartmentAccessComponent();
		if (!compartmentAccess)
			return false;
		
		if (compartmentAccess.IsGettingIn() || compartmentAccess.IsGettingOut() || ELIFE_PosePropComponent.IsTurningToSit(user))
			return false;

		// ELIFE | Seats/beds: not behind a wall, and a single bed offers lying down on the side the user stands.
		ELIFE_PosePropComponent poseProp = ELIFE_PosePropComponent.Get(compartment.GetOwner());
		if (poseProp && !poseProp.ShowsGetIn(compartment, user))
			return false;

		return true;
	}	
};