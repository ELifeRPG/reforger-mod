//------------------------------------------------------------------------------------------------
[ComponentEditorProps(category: "ELifeRPG/Seats", description: "On a seating prop: who sits on which of its seats. Required for its sit actions.")]
class ELIFE_SeatPropComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! Sitters who lose the pose free their seat on the next check; clients only get the taken seat numbers.
class ELIFE_SeatPropComponent : ScriptComponent
{
	protected static const int CHECK_INTERVAL_MS = 1000;
	//! The pose starts a moment after it's asked for; until then a new sitter doesn't count as gone.
	protected static const int SIT_GRACE_MS = 2000;

	[RplProp()]
	protected ref array<int> m_aTaken = {};

	protected ref map<int, IEntity> m_mSitters = new map<int, IEntity>();
	protected ref map<int, int> m_mSince = new map<int, int>();
	//! Seat points by number, built on first use; a prop's children don't change.
	protected ref map<int, SCR_AISmartActionSentinelComponent> m_mSeats;

	//------------------------------------------------------------------------------------------------
	bool IsTaken(int index)
	{
		return m_aTaken.Contains(index);
	}

	//------------------------------------------------------------------------------------------------
	SCR_AISmartActionSentinelComponent GetSeat(int index)
	{
		if (!m_mSeats)
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
	//! Server. Who sits there, or null.
	IEntity GetSitter(int index)
	{
		if (!m_mSitters.Contains(index))
			return null;

		IEntity sitter = m_mSitters.Get(index);

		if (!IsGone(index, sitter, System.GetTickCount()))
			return sitter;

		Free(index);
		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsGone(int index, IEntity sitter, int now)
	{
		if (now - m_mSince.Get(index) < SIT_GRACE_MS)
			return false;

		SCR_AISmartActionSentinelComponent seat = GetSeat(index);
		return !seat || !sitter || !ELIFE_Seats.IsSitting(sitter, seat);
	}

	//------------------------------------------------------------------------------------------------
	//! Server.
	void Take(int index, notnull IEntity character)
	{
		m_mSitters.Set(index, character);
		m_mSince.Set(index, System.GetTickCount());

		//! Keeps vanilla smart-action AI off it too.
		SCR_AISmartActionSentinelComponent seat = GetSeat(index);
		if (seat)
			seat.SetActionAccessible(false);

		Update();
	}

	//------------------------------------------------------------------------------------------------
	//! Server. Only frees the seat if that character sits there.
	void Release(int index, notnull IEntity character)
	{
		if (m_mSitters.Get(index) != character)
			return;

		Free(index);
		Update();
	}

	//------------------------------------------------------------------------------------------------
	protected void Free(int index)
	{
		m_mSitters.Remove(index);
		m_mSince.Remove(index);

		SCR_AISmartActionSentinelComponent seat = GetSeat(index);
		if (seat)
			seat.SetActionAccessible(true);
	}

	//------------------------------------------------------------------------------------------------
	//! Server. Drops sitters who no longer sit there.
	protected void CleanUp()
	{
		int now = System.GetTickCount();
		array<int> gone = {};
		foreach (int index, IEntity sitter : m_mSitters)
		{
			if (IsGone(index, sitter, now))
				gone.Insert(index);
		}

		foreach (int index : gone)
		{
			Free(index);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Server. Tells the clients, and keeps checking while anyone sits here.
	protected void Update()
	{
		if (!Replication.IsServer())
			return;

		CleanUp();

		bool changed = m_aTaken.Count() != m_mSitters.Count();
		if (!changed)
		{
			foreach (int index : m_aTaken)
			{
				if (!m_mSitters.Contains(index))
				{
					changed = true;
					break;
				}
			}
		}

		if (changed)
		{
			m_aTaken.Clear();
			foreach (int index, IEntity sitter : m_mSitters)
			{
				m_aTaken.Insert(index);
			}

			Replication.BumpMe();
		}

		GetGame().GetCallqueue().Remove(Update);
		if (!m_mSitters.IsEmpty())
			GetGame().GetCallqueue().CallLater(Update, CHECK_INTERVAL_MS);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(Update);
	}
}
