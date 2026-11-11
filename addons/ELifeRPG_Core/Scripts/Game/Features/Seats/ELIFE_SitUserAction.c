//------------------------------------------------------------------------------------------------
//! m_iSmartActionId is the seat number. Occupancy lives in ELIFE_SeatPropComponent: vanilla's occupant stays set
//! when someone walks out of the pose.
class ELIFE_SitUserAction : SCR_LoiterUserAction
{
	//------------------------------------------------------------------------------------------------
	protected SCR_AISmartActionSentinelComponent GetSeat()
	{
		return ELIFE_Seats.GetSeat(GetOwner(), m_iSmartActionId);
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_SeatPropComponent GetProp()
	{
		return ELIFE_SeatPropComponent.Cast(GetOwner().FindComponent(ELIFE_SeatPropComponent));
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsSittingHere(IEntity user)
	{
		SCR_AISmartActionSentinelComponent seat = GetSeat();
		return user && seat && ELIFE_Seats.IsSitting(user, seat);
	}

	//------------------------------------------------------------------------------------------------
	//! The server checks first and uses its live sitters, so two players can't take the same seat at once.
	protected bool IsTakenByOther(notnull ELIFE_SeatPropComponent prop, IEntity user)
	{
		if (!Replication.IsServer())
			return prop.IsTaken(m_iSmartActionId);

		IEntity sitter = prop.GetSitter(m_iSmartActionId);
		return sitter && sitter != user;
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		if (IsSittingHere(SCR_PlayerController.GetLocalControlledEntity()))
			outName = "#ELIFE-Seat_StandUp";
		else
			outName = "#ELIFE-Seat_Sit";

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		if (IsSittingHere(user))
			return true;

		ELIFE_SeatPropComponent prop = GetProp();
		if (prop && IsTakenByOther(prop, user))
		{
			SetCannotPerformReason("#ELIFE-Seat_Taken");
			return false;
		}

		return GetSeat() != null;
	}

	//------------------------------------------------------------------------------------------------
	//! Vanilla hides actions while a pose plays; this one stays, to stand up again.
	override bool CanBeShownScript(IEntity user)
	{
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(user);
		if (!character || !SCR_ScriptedUserAction.IsUserPlayingBlockingAnimation(character))
			return true;

		return IsSittingHere(user);
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		ELIFE_SeatPropComponent prop = GetProp();
		bool isServer = Replication.IsServer();

		if (IsSittingHere(pUserEntity))
		{
			ReleaseOccupant();
			StopAction(pUserEntity);
			if (isServer && prop)
				prop.Release(m_iSmartActionId, pUserEntity);

			return;
		}

		m_Occupant = pUserEntity;
		m_OccupantRplId = SCR_EntityHelper.EntityToRplId(pUserEntity);
		StartAction(pUserEntity);
		if (isServer && prop)
			prop.Take(m_iSmartActionId, pUserEntity);
	}

	//------------------------------------------------------------------------------------------------
	//! Vanilla's, with the pose on the seat point instead of the action's owner.
	override void StartAction(IEntity pUserEntity)
	{
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(pUserEntity);
		SCR_AISmartActionSentinelComponent seat = GetSeat();
		if (!character || !seat || !m_isValid)
			return;

		SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
		if (!controller)
			return;

		//! Consumed, like vanilla: each use carries its own data from the server.
		m_isValid = false;
		controller.StartLoitering(seat.GetOwner(), m_eLoiteringType, m_bHolsterWeapon, true, true, m_targetPosition);
	}

	//------------------------------------------------------------------------------------------------
	//! Server. The leading false is vanilla's "no occupant" flag that OnLoadActionData reads.
	override protected bool OnSaveActionData(ScriptBitWriter writer)
	{
		SCR_AISmartActionSentinelComponent seat = GetSeat();
		m_isValid = seat != null;
		if (!m_isValid)
		{
			Print(string.Format("%1 | no seat point %2 on %3", ClassName(), m_iSmartActionId, GetOwner()), LogLevel.WARNING);
			return false;
		}

		ELIFE_Seats.GetSitTransform(seat, m_targetPosition);
		m_eLoiteringType = seat.GetLoiterAnimation();
		m_bHolsterWeapon = seat.GetHolsterWeapon();

		writer.WriteBool(false);
		writer.WriteInt((int)m_eLoiteringType);
		writer.WriteBool(m_bHolsterWeapon);
		writer.WriteVector(m_targetPosition[0]);
		writer.WriteVector(m_targetPosition[1]);
		writer.WriteVector(m_targetPosition[2]);
		writer.WriteVector(m_targetPosition[3]);
		return true;
	}
}
