//------------------------------------------------------------------------------------------------
class ELIFE_RespawnPointClass : GenericEntityClass
{
}

//------------------------------------------------------------------------------------------------
/*!
	Named point where a dead player comes back (spec 5.7). The server keeps the play-mode instances in a static list;
	edit-mode and client instances never join it, and the destructor always takes the point out.
*/
class ELIFE_RespawnPoint : GenericEntity
{
	[Attribute("", UIWidgets.EditBox, desc: "Respawn point name")]
	string m_sName;

	protected static ref array<ELIFE_RespawnPoint> s_aPoints;

	//------------------------------------------------------------------------------------------------
	void ELIFE_RespawnPoint(IEntitySource src, IEntity parent)
	{
		if (!GetGame().InPlayMode() || !Replication.IsServer())
			return;

		if (!s_aPoints)
			s_aPoints = new array<ELIFE_RespawnPoint>();

		s_aPoints.Insert(this);
	}

	//------------------------------------------------------------------------------------------------
	void ~ELIFE_RespawnPoint()
	{
		if (s_aPoints)
			s_aPoints.RemoveItem(this);
	}

	//------------------------------------------------------------------------------------------------
	//! Closest registered respawn point to `pos`, or null when none is registered.
	static ELIFE_RespawnPoint FindNearest(vector pos)
	{
		if (!s_aPoints)
			return null;

		ELIFE_RespawnPoint best = null;
		float bestDist = 0;
		foreach (ELIFE_RespawnPoint point : s_aPoints)
		{
			float dist = vector.Distance(point.GetOrigin(), pos);
			if (!best || dist < bestDist)
			{
				bestDist = dist;
				best = point;
			}
		}

		return best;
	}
}
