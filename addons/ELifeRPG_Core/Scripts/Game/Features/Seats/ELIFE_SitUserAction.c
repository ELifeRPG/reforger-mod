//------------------------------------------------------------------------------------------------
//! m_iSmartActionId is the seat number. Occupancy lives in ELIFE_SeatPropComponent: vanilla's occupant stays set
//! when someone walks out of the pose.
class ELIFE_SitUserAction : SCR_LoiterUserAction
{
	//! Vanilla only slides the player onto the seat from close in front; elsewhere the pose starts where they stand.
	protected static const float REACH = 1.0;
	//! cos of the widest angle off the seat's facing, 60 degrees.
	protected static const float FRONT_COS = 0.5;

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
	protected bool IsInFront(notnull IEntity user, notnull SCR_AISmartActionSentinelComponent seat)
	{
		vector transform[4];
		ELIFE_Seats.GetSitTransform(seat, transform);

		vector toUser = user.GetOrigin() - transform[3];
		toUser[1] = 0;
		float distance = toUser.Length();
		if (distance > REACH)
			return false;

		//! Right on the spot there's no direction to check.
		if (distance < 0.01)
			return true;

		vector facing = transform[2] * -1;
		facing[1] = 0;
		return vector.Dot(toUser.Normalized(), facing.Normalized()) >= FRONT_COS;
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

		//! Also checked here: the server may not ask CanBeShown.
		SCR_AISmartActionSentinelComponent seat = GetSeat();
		return seat && IsInFront(user, seat);
	}

	//------------------------------------------------------------------------------------------------
	//! Vanilla hides actions while a pose plays; this one stays, to stand up again. Sitting is only offered from the front.
	override bool CanBeShownScript(IEntity user)
	{
		if (IsSittingHere(user))
			return true;

		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(user);
		if (!character || SCR_ScriptedUserAction.IsUserPlayingBlockingAnimation(character))
			return false;

		SCR_AISmartActionSentinelComponent seat = GetSeat();
		return seat && IsInFront(user, seat);
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
