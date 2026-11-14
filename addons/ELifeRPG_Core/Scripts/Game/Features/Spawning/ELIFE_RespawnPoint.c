//------------------------------------------------------------------------------------------------
class ELIFE_RespawnPointClass : GenericEntityClass
{
}

//------------------------------------------------------------------------------------------------
/*!
	Named point where a dead player comes back, picked at random among the registered points (spec 5.7). The server keeps the play-mode instances in a static list;
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
	//! Random registered respawn point, or null when none is registered.
	static ELIFE_RespawnPoint GetRandom()
	{
		if (!s_aPoints || s_aPoints.Count() == 0)
			return null;

		return s_aPoints[Math.RandomInt(0, s_aPoints.Count())];
	}
}
