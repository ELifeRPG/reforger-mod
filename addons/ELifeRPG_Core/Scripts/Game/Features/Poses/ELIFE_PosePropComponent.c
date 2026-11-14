//------------------------------------------------------------------------------------------------
class ELIFE_PosePropComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! Base of the props characters take a pose on (ELIFE_SeatPropComponent, ELIFE_BedComponent): who holds which of its
//! numbered spots. The server frees spots whose holder left the pose; clients get the taken spots and their holders.
//! Vanilla's action occupant isn't used: it stays set when someone walks out of the pose.
//! Other characters pass through a holder in the pose: the pose only moves the body, so his collision capsule stays
//! where the pose started, blocks that spot and lets others push him out of the pose.
class ELIFE_PosePropComponent : ScriptComponent
{
	protected static const int CHECK_INTERVAL_MS = 1000;

	protected static const int PASS_THROUGH_CHECK_MS = 250;
	//! A player pushed further than this off his place gets up (NPCs: ELIFE_NpcRoutine takes the place again).
	protected static const float PUSHED_DISTANCE = 0.15;
	//! Vanilla slides the character onto the place at the start of the pose; that isn't a push.
	protected static const int PUSHED_GRACE_MS = 2000;
	//! The stand-up of a pushed player should be over by then; if he is still in the pose, it is finished directly.
	protected static const int END_TIMEOUT_MS = 3000;

	[RplProp()]
	protected ref array<int> m_aTaken = {};
	//! The holders of m_aTaken, in the same order.
	[RplProp(onRplName: "OnHoldersReplicated")]
	protected ref array<RplId> m_aHolderIds = {};

	//! Every machine (physics is local): holders whose character collision is off, with their layer from before.
	protected ref map<IEntity, int> m_mPassThrough = new map<IEntity, int>();
	//! Every machine: since when each holder is in the pose.
	protected ref map<IEntity, int> m_mInPoseSince = new map<IEntity, int>();
	//! Where owned: pushed players getting up, since when.
	protected ref map<IEntity, int> m_mEndingSince = new map<IEntity, int>();

	//! Server.
	protected ref map<int, IEntity> m_mHolders = new map<int, IEntity>();
	protected ref map<int, int> m_mSince = new map<int, int>();

	//------------------------------------------------------------------------------------------------
	//! Server. Who holds the spot, or null.
	IEntity GetHolder(int spot)
	{
		if (!m_mHolders.Contains(spot))
			return null;

		IEntity holder = m_mHolders.Get(spot);
		if (!IsGone(spot, holder, System.GetTickCount()))
			return holder;

		Free(spot);
		Update();
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! The server checks its live holders, so two characters can't take a spot at once; clients the replicated state.
	bool IsTakenByOther(int spot, IEntity character)
	{
		if (!Replication.IsServer())
		{
			int i = m_aTaken.Find(spot);
			return i >= 0 && (i >= m_aHolderIds.Count() || GetEntity(m_aHolderIds[i]) != character);
		}

		IEntity holder = GetHolder(spot);
		return holder && holder != character;
	}

	//------------------------------------------------------------------------------------------------
	//! Server.
	void Take(int spot, notnull IEntity character)
	{
		m_mHolders.Set(spot, character);
		m_mSince.Set(spot, System.GetTickCount());
		OnTaken(spot);
		Update();
	}

	//------------------------------------------------------------------------------------------------
	//! Server. Only frees the spot if that character holds it.
	void Release(int spot, notnull IEntity character)
	{
		if (m_mHolders.Get(spot) != character)
			return;

		Free(spot);
		Update();
	}

	//------------------------------------------------------------------------------------------------
	//! The character holds the pose on that spot.
	protected bool IsInPose(int spot, notnull IEntity character)
	{
		return ELIFE_Poses.IsInPoseOn(character, GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	//! A new holder only counts as gone after this: the pose starts a moment after it's asked for.
	protected int GetGraceMs()
	{
		return 2000;
	}

	//------------------------------------------------------------------------------------------------
	//! How far, along the ground, the holder is from where the pose put him. 0 if unknown.
	protected float GetPoseError(int spot, notnull IEntity character)
	{
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! How long getting up of a pushed player may take before the pose is finished directly.
	protected int GetEndTimeoutMs()
	{
		return END_TIMEOUT_MS;
	}

	//------------------------------------------------------------------------------------------------
	//! Where the character is owned, pushed off his place: the normal way up, with its animation.
	protected void EndPose(int spot, notnull IEntity character)
	{
		SCR_CharacterControllerComponent controller = ELIFE_Poses.GetController(character);
		if (controller)
			controller.StopLoitering(false);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnTaken(int spot)
	{
	}

	//------------------------------------------------------------------------------------------------
	protected void OnFreed(int spot)
	{
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsGone(int spot, IEntity holder, int now)
	{
		if (now - m_mSince.Get(spot) < GetGraceMs())
			return false;

		return !holder || !IsInPose(spot, holder);
	}

	//------------------------------------------------------------------------------------------------
	protected void Free(int spot)
	{
		m_mHolders.Remove(spot);
		m_mSince.Remove(spot);
		OnFreed(spot);
	}

	//------------------------------------------------------------------------------------------------
	//! Server. Drops holders who left the pose, tells the clients, and keeps checking while any spot is held.
	protected void Update()
	{
		if (!Replication.IsServer())
			return;

		int now = System.GetTickCount();
		array<int> gone = {};
		foreach (int spot, IEntity holder : m_mHolders)
		{
			if (IsGone(spot, holder, now))
				gone.Insert(spot);
		}

		foreach (int spot : gone)
		{
			Free(spot);
		}

		array<int> taken = {};
		array<RplId> holderIds = {};
		foreach (int spot, IEntity holder : m_mHolders)
		{
			taken.Insert(spot);
			holderIds.Insert(SCR_EntityHelper.EntityToRplId(holder));
		}

		bool changed = taken.Count() != m_aTaken.Count();
		foreach (int i, int spot : taken)
		{
			if (changed || spot != m_aTaken[i] || holderIds[i] != m_aHolderIds[i])
				changed = true;
		}

		if (changed)
		{
			m_aTaken.Copy(taken);
			m_aHolderIds.Copy(holderIds);
			Replication.BumpMe();
		}

		GetGame().GetCallqueue().Remove(Update);
		if (!m_mHolders.IsEmpty())
			GetGame().GetCallqueue().CallLater(Update, CHECK_INTERVAL_MS);

		CheckPassThrough();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnHoldersReplicated()
	{
		CheckPassThrough();
	}

	//------------------------------------------------------------------------------------------------
	//! Every machine. Turns character collision off for holders in the pose and back on once they left it, and gets
	//! a player up who was pushed off his place (on his own machine); keeps checking while anyone holds a spot here.
	protected void CheckPassThrough()
	{
		int now = System.GetTickCount();
		array<IEntity> inPose = {};
		foreach (int i, RplId id : m_aHolderIds)
		{
			IEntity holder = GetEntity(id);
			if (!holder || i >= m_aTaken.Count() || !IsInPose(m_aTaken[i], holder))
				continue;

			inPose.Insert(holder);
			if (!m_mInPoseSince.Contains(holder))
				m_mInPoseSince.Set(holder, now);

			if (!IsOwnPlayer(holder))
				continue;

			if (m_mEndingSince.Contains(holder))
			{
				//! Vanilla's stand-up never ended on a host being pushed, and a stop in progress ignores every further
				//! one (jump, action): finish the pose directly.
				if (now - m_mEndingSince.Get(holder) >= GetEndTimeoutMs())
					ELIFE_Poses.FinishPose(holder);
			}
			else if (now - m_mInPoseSince.Get(holder) >= PUSHED_GRACE_MS && GetPoseError(m_aTaken[i], holder) > PUSHED_DISTANCE)
			{
				m_mEndingSince.Set(holder, now);
				EndPose(m_aTaken[i], holder);
			}
		}

		array<IEntity> stood = {};
		foreach (IEntity holder, int since : m_mInPoseSince)
		{
			if (!inPose.Contains(holder))
				stood.Insert(holder);
		}

		foreach (IEntity holder : stood)
		{
			m_mInPoseSince.Remove(holder);
			m_mEndingSince.Remove(holder);
		}

		foreach (IEntity holder : inPose)
		{
			Physics physics = holder.GetPhysics();
			//! Still off from another prop he just left: wait until that one restored his layer, or we'd save the wrong one.
			if (physics && !m_mPassThrough.Contains(holder) && physics.GetInteractionLayer() != EPhysicsLayerPresets.CharNoCollide)
			{
				m_mPassThrough.Set(holder, physics.GetInteractionLayer());
				physics.SetInteractionLayer(EPhysicsLayerPresets.CharNoCollide);
			}
		}

		array<IEntity> left = {};
		foreach (IEntity holder, int layer : m_mPassThrough)
		{
			if (!inPose.Contains(holder))
				left.Insert(holder);
		}

		foreach (IEntity holder : left)
		{
			RestoreCollision(holder);
		}

		GetGame().GetCallqueue().Remove(CheckPassThrough);
		if (!m_aHolderIds.IsEmpty() || !m_mPassThrough.IsEmpty())
			GetGame().GetCallqueue().CallLater(CheckPassThrough, PASS_THROUGH_CHECK_MS);
	}

	//------------------------------------------------------------------------------------------------
	protected void RestoreCollision(IEntity holder)
	{
		if (holder && holder.GetPhysics())
			holder.GetPhysics().SetInteractionLayer(m_mPassThrough.Get(holder));

		m_mPassThrough.Remove(holder);
	}

	//------------------------------------------------------------------------------------------------
	//! The local player's own character (NPCs are handled by their routine).
	protected static bool IsOwnPlayer(IEntity character)
	{
		return character && character == SCR_PlayerController.GetLocalControlledEntity();
	}

	//------------------------------------------------------------------------------------------------
	protected static IEntity GetEntity(RplId id)
	{
		RplComponent rpl = RplComponent.Cast(Replication.FindItem(id));
		if (!rpl)
			return null;

		return rpl.GetEntity();
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(Update);
		GetGame().GetCallqueue().Remove(CheckPassThrough);

		array<IEntity> passing = {};
		foreach (IEntity holder, int layer : m_mPassThrough)
		{
			passing.Insert(holder);
		}

		foreach (IEntity holder : passing)
		{
			RestoreCollision(holder);
		}
	}
}
