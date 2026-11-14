//------------------------------------------------------------------------------------------------
[ComponentEditorProps(category: "ELifeRPG/Seats", description: "On a seating prop: who sits on which of its seats. Required for its sit actions.")]
class ELIFE_SeatPropComponentClass : ELIFE_PosePropComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! Spots are the seat numbers (ELIFE_Seats.TAG_n on the seat points); the pose is on the seat point, not the prop.
//! The seat points' smart action (SCR_AISmartActionSentinelComponent) only exists on the server, so the server
//! publishes what clients need per seat: its number, seat point and sit transform.
class ELIFE_SeatPropComponent : ELIFE_PosePropComponent
{
	//! The prop's children may not be there yet on the first try.
	protected static const int PUBLISH_RETRY_MS = 1000;
	protected static const int PUBLISH_TRIES = 10;

	[RplProp()]
	protected ref array<int> m_aSeatIndices = {};
	[RplProp()]
	protected ref array<RplId> m_aSeatPointIds = {};
	//! Four vectors per seat, in m_aSeatIndices order.
	[RplProp()]
	protected ref array<vector> m_aSitTransforms = {};

	//! Server. Seat points by number, built on use; rebuilt while one is missing.
	protected ref map<int, SCR_AISmartActionSentinelComponent> m_mSeats;
	protected int m_iPublishTries;

	//------------------------------------------------------------------------------------------------
	//! The seat point entity the pose is on, on every machine.
	IEntity GetSeatPoint(int index)
	{
		if (Replication.IsServer())
		{
			SCR_AISmartActionSentinelComponent seat = GetSeat(index);
			if (seat)
				return seat.GetOwner();

			return null;
		}

		int i = m_aSeatIndices.Find(index);
		if (i < 0)
			return null;

		return GetEntity(m_aSeatPointIds[i]);
	}

	//------------------------------------------------------------------------------------------------
	//! Where the sitting pose starts (ELIFE_Seats.GetSitTransform), on every machine. False if unknown here yet.
	bool GetSitTransform(int index, out vector transform[4])
	{
		if (Replication.IsServer())
		{
			SCR_AISmartActionSentinelComponent seat = GetSeat(index);
			if (!seat)
				return false;

			ELIFE_Seats.GetSitTransform(seat, transform);
			return true;
		}

		int i = m_aSeatIndices.Find(index);
		if (i < 0 || m_aSitTransforms.Count() < (i + 1) * 4)
			return false;

		for (int k = 0; k < 4; k++)
		{
			transform[k] = m_aSitTransforms[i * 4 + k];
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Server only (the smart action doesn't exist on clients).
	SCR_AISmartActionSentinelComponent GetSeat(int index)
	{
		if (!m_mSeats || !m_mSeats.Get(index))
		{
			m_mSeats = new map<int, SCR_AISmartActionSentinelComponent>();
			array<SCR_AISmartActionSentinelComponent> seats = {};
			ELIFE_Seats.GetSeats(GetOwner(), seats);
			foreach (SCR_AISmartActionSentinelComponent seat : seats)
			{
				int seatIndex = ELIFE_Seats.GetIndex(seat);
				if (seatIndex >= 0)
					m_mSeats.Set(seatIndex, seat);
			}
		}

		return m_mSeats.Get(index);
	}

	//------------------------------------------------------------------------------------------------
	override protected bool IsInPose(int spot, notnull IEntity character)
	{
		return ELIFE_Poses.IsInPoseOn(character, GetSeatPoint(spot));
	}

	//------------------------------------------------------------------------------------------------
	override protected float GetPoseError(int spot, notnull IEntity character)
	{
		vector transform[4];
		if (!GetSitTransform(spot, transform))
			return 0;

		return vector.DistanceXZ(character.GetOrigin(), transform[3]);
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (GetGame().InPlayMode())
			GetGame().GetCallqueue().CallLater(PublishSeats, 0);
	}

	//------------------------------------------------------------------------------------------------
	//! Server. Publishes the seats for clients, once the seat points are there.
	protected void PublishSeats()
	{
		if (Replication.IsClient() || !m_aSeatIndices.IsEmpty())
			return;

		//! Right after loading, replication may not run yet (Workbench host): then IsServer is false and the seat
		//! points' ids aren't valid. Published too early or not at all, clients got no sit action.
		if (!Replication.IsRunning() || !Replication.IsServer())
		{
			GetGame().GetCallqueue().CallLater(PublishSeats, PUBLISH_RETRY_MS);
			return;
		}

		array<SCR_AISmartActionSentinelComponent> seats = {};
		ELIFE_Seats.GetSeats(GetOwner(), seats);
		foreach (SCR_AISmartActionSentinelComponent seat : seats)
		{
			int index = ELIFE_Seats.GetIndex(seat);
			if (index < 0)
				continue;

			RplId seatPointId = SCR_EntityHelper.EntityToRplId(seat.GetOwner());
			if (seatPointId == RplId.Invalid())
				continue;

			vector transform[4];
			ELIFE_Seats.GetSitTransform(seat, transform);
			m_aSeatIndices.Insert(index);
			m_aSeatPointIds.Insert(seatPointId);
			for (int k = 0; k < 4; k++)
			{
				m_aSitTransforms.Insert(transform[k]);
			}
		}

		if (!m_aSeatIndices.IsEmpty())
		{
			Replication.BumpMe();
			return;
		}

		m_iPublishTries++;
		if (m_iPublishTries < PUBLISH_TRIES)
			GetGame().GetCallqueue().CallLater(PublishSeats, PUBLISH_RETRY_MS);
		else
			Print(string.Format("%1 | no seat points on %2", ClassName(), GetOwner()), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		super.OnDelete(owner);
		GetGame().GetCallqueue().Remove(PublishSeats);
	}

	//------------------------------------------------------------------------------------------------
	//! Keeps vanilla smart-action AI off a taken seat.
	override protected void OnTaken(int spot)
	{
		SCR_AISmartActionSentinelComponent seat = GetSeat(spot);
		if (seat)
			seat.SetActionAccessible(false);
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnFreed(int spot)
	{
		SCR_AISmartActionSentinelComponent seat = GetSeat(spot);
		if (seat)
			seat.SetActionAccessible(true);
	}
}
