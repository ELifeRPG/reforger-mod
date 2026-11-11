//------------------------------------------------------------------------------------------------
[EntityEditorProps(category: "ELifeRPG/Dev", description: "Spike: an AI civilian walks to the nearest free seat, sits there, and walks back. Dev world only.")]
class ELIFE_DevAiWalkSpikeComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
enum ELIFE_EDevAiSpikeState
{
	TO_FRONT,
	SITTING,
	WALK_TO_DESK,
	STANDING
}

//------------------------------------------------------------------------------------------------
//! Server-only: walks a spawned civilian to the nearest free seat via move waypoints, seats it with
//! ELIFE_Seats.SitDown, then back to the start, in a loop.
class ELIFE_DevAiWalkSpikeComponent : ScriptComponent
{
	[Attribute("{626874113CAE4387}Prefabs/Characters/Factions/CIV/Businessman/Character_CIV_Businessman_3.et", UIWidgets.ResourceNamePicker, "Who walks", "et")]
	protected ResourceName m_sCharacterPrefab;

	[Attribute("{000CD338713F2B5A}Prefabs/AI/Groups/Group_Base.et", UIWidgets.ResourceNamePicker, "His AI group", "et")]
	protected ResourceName m_sGroupPrefab;

	[Attribute("{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et", UIWidgets.ResourceNamePicker, "Move waypoint", "et")]
	protected ResourceName m_sMoveWaypointPrefab;

	[Attribute("30", UIWidgets.EditBox, "How far he looks for a free seat, in metres")]
	protected float m_fSeatRange;

	[Attribute("20", UIWidgets.EditBox, "Seconds he sits")]
	protected float m_fSeatSeconds;

	[Attribute("15", UIWidgets.EditBox, "Seconds he stands at the desk")]
	protected float m_fStandSeconds;

	protected static const int TICK_MS = 500;
	protected static const float ARRIVED_DISTANCE = 0.8;
	//! The AI stops short of a move waypoint, so standing still this close also counts as arrived.
	protected static const float STOPPED_NEAR_DISTANCE = 3;
	protected static const float STOPPED_STEP = 0.05;
	protected static const int WAYPOINT_DELETE_DELAY_MS = 2000;
	//! The AI stops ~0.85 m short of the waypoint, but sits down on the way once within SIT_FROM_DISTANCE.
	protected static const float FRONT_DISTANCE = -0.35;
	protected static const float SIT_FROM_DISTANCE = 0.5;
	protected static const float SIT_FROM_MAX_DISTANCE = 1.2;
	protected static const float SIT_FORWARD = 0.07;
	//! The pose takes a moment to start before losing it counts as being pushed off.
	protected static const float SIT_GRACE_S = 3;

	protected IEntity m_Character;
	protected SCR_AIGroup m_Group;
	protected AIWaypoint m_Waypoint;
	protected SCR_AISmartActionSentinelComponent m_Seat;
	protected ELIFE_EDevAiSpikeState m_eState;
	protected float m_fStateSeconds;
	protected vector m_vDeskTransform[4];
	protected vector m_vLastPosition;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		if (!GetGame().InPlayMode() || !Replication.IsServer())
			return;

		GetGame().GetCallqueue().CallLater(Begin, 3000);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(Begin);
		GetGame().GetCallqueue().Remove(Tick);
	}

	//------------------------------------------------------------------------------------------------
	protected void Begin()
	{
		GetOwner().GetWorldTransform(m_vDeskTransform);

		m_Character = Spawn(m_sCharacterPrefab, m_vDeskTransform);
		vector groupTransform[4];
		Math3D.MatrixIdentity4(groupTransform);
		groupTransform[3] = m_vDeskTransform[3];
		m_Group = SCR_AIGroup.Cast(Spawn(m_sGroupPrefab, groupTransform));
		if (!m_Character || !m_Group || !m_Group.AddAIEntityToGroup(m_Character))
		{
			Print("ELIFE_DevAiWalkSpike | spawn failed", LogLevel.ERROR);
			return;
		}

		SetWalking();
		Print("ELIFE_DevAiWalkSpike | started");
		GoToSeat();
		GetGame().GetCallqueue().CallLater(Tick, TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	//! AI runs by default. Speed is an AI setting on the group's settings component, applied to its members.
	protected void SetWalking()
	{
		SCR_AIGroupSettingsComponent settings = SCR_AIGroupSettingsComponent.Cast(m_Group.FindComponent(SCR_AIGroupSettingsComponent));
		if (settings)
			settings.AddSetting(SCR_AIGroupCharactersMovementSpeedSetting.Create(SCR_EAISettingOrigin.SCENARIO, EMovementType.WALK), false, true);
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
		return GetGame().SpawnEntityPrefab(resource, GetOwner().GetWorld(), params);
	}

	//------------------------------------------------------------------------------------------------
	//! Vanilla's move waypoint completes 5 m early.
	protected AIWaypoint SpawnMoveWaypoint(vector transform[4])
	{
		AIWaypoint waypoint = AIWaypoint.Cast(Spawn(m_sMoveWaypointPrefab, transform));
		if (waypoint)
			waypoint.SetCompletionRadius(0.5);

		return waypoint;
	}

	//------------------------------------------------------------------------------------------------
	protected void ReplaceWaypoint(AIWaypoint waypoint)
	{
		//! Completed rather than removed, and deleted later: the group's running activity still reads it this frame.
		if (m_Waypoint)
		{
			m_Group.CompleteWaypoint(m_Waypoint);
			GetGame().GetCallqueue().CallLater(SCR_EntityHelper.DeleteEntityAndChildren, WAYPOINT_DELETE_DELAY_MS, false, m_Waypoint);
		}

		m_Waypoint = waypoint;
		if (m_Waypoint)
			m_Group.AddWaypoint(m_Waypoint);
	}

	//------------------------------------------------------------------------------------------------
	protected void GoToSeat()
	{
		array<SCR_AISmartActionSentinelComponent> seats = {};
		ELIFE_Seats.FindFree(m_Character.GetOrigin(), m_fSeatRange, seats);
		if (seats.IsEmpty())
		{
			Print("ELIFE_DevAiWalkSpike | no free seat nearby, standing instead", LogLevel.WARNING);
			SetState(ELIFE_EDevAiSpikeState.STANDING);
			return;
		}

		m_Seat = seats[0];
		vector transform[4];
		Math3D.MatrixIdentity4(transform);
		transform[3] = ELIFE_Seats.GetApproachPoint(m_Seat, FRONT_DISTANCE);
		ReplaceWaypoint(SpawnMoveWaypoint(transform));
		Print(string.Format("ELIFE_DevAiWalkSpike | heading for seat %1 at %2 (%3 free)", ELIFE_Seats.GetIndex(m_Seat), ELIFE_Seats.GetPosition(m_Seat), seats.Count()));
		SetState(ELIFE_EDevAiSpikeState.TO_FRONT);
	}

	//------------------------------------------------------------------------------------------------
	protected void StepToFront()
	{
		if (!ELIFE_Seats.IsFree(m_Seat))
		{
			Print("ELIFE_DevAiWalkSpike | seat got taken on the way, looking for another");
			GoToSeat();
			return;
		}

		vector transform[4];
		Math3D.MatrixIdentity4(transform);
		transform[3] = ELIFE_Seats.GetApproachPoint(m_Seat, FRONT_DISTANCE);
		ReplaceWaypoint(SpawnMoveWaypoint(transform));
		SetState(ELIFE_EDevAiSpikeState.TO_FRONT);
	}

	//------------------------------------------------------------------------------------------------
	protected void WalkToDesk()
	{
		vector transform[4];
		Math3D.MatrixIdentity4(transform);
		transform[3] = m_vDeskTransform[3];
		ReplaceWaypoint(SpawnMoveWaypoint(transform));
		SetState(ELIFE_EDevAiSpikeState.WALK_TO_DESK);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetState(ELIFE_EDevAiSpikeState state)
	{
		m_eState = state;
		m_fStateSeconds = 0;
		Print(string.Format("ELIFE_DevAiWalkSpike | %1", typename.EnumToString(ELIFE_EDevAiSpikeState, state)));
	}

	//------------------------------------------------------------------------------------------------
	protected SCR_CharacterControllerComponent GetController()
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(m_Character);
		if (!character)
			return null;

		return SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
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
	protected void Tick()
	{
		SCR_CharacterControllerComponent controller = GetController();
		if (!controller || controller.IsDead())
		{
			GetGame().GetCallqueue().Remove(Tick);
			return;
		}

		m_fStateSeconds += TICK_MS / 1000.0;

		switch (m_eState)
		{
			case ELIFE_EDevAiSpikeState.TO_FRONT:
			{
				if (!m_Seat)
					return;

				//! Sits down on the way in; only a stop further out needs another go.
				float distance = vector.Distance(m_Character.GetOrigin(), ELIFE_Seats.GetPosition(m_Seat));
				if (distance > SIT_FROM_DISTANCE && !HasArrived(ELIFE_Seats.GetApproachPoint(m_Seat, FRONT_DISTANCE)))
					return;

				//! Stopped too early: one more go at the front point.
				if (distance > SIT_FROM_MAX_DISTANCE)
				{
					StepToFront();
					return;
				}

				//! Without a waypoint the AI idles and leaves the pose alone.
				ReplaceWaypoint(null);
				bool seated = ELIFE_Seats.SitDown(m_Character, m_Seat, SIT_FORWARD);
				Print(string.Format("ELIFE_DevAiWalkSpike | sitting down from %1 m, seated: %2", distance, seated));
				if (!seated)
				{
					GoToSeat();
					return;
				}

				SetState(ELIFE_EDevAiSpikeState.SITTING);
				return;
			}
			case ELIFE_EDevAiSpikeState.SITTING:
			{
				bool pushedOff = m_fStateSeconds >= SIT_GRACE_S && !ELIFE_Seats.IsSitting(m_Character, m_Seat);
				if (m_fStateSeconds < m_fSeatSeconds && !pushedOff)
					return;

				if (pushedOff)
					Print("ELIFE_DevAiWalkSpike | lost the pose, the seat is free again");

				ELIFE_Seats.StandUp(m_Character, m_Seat);
				m_Seat = null;
				WalkToDesk();
				return;
			}
			case ELIFE_EDevAiSpikeState.WALK_TO_DESK:
			{
				if (!HasArrived(m_vDeskTransform[3]))
					return;

				SetState(ELIFE_EDevAiSpikeState.STANDING);
				return;
			}
			case ELIFE_EDevAiSpikeState.STANDING:
			{
				if (m_fStateSeconds < m_fStandSeconds)
					return;

				GoToSeat();
				return;
			}
		}
	}
}
