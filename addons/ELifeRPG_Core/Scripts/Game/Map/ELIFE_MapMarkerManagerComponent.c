//------------------------------------------------------------------------------------------------
//! Vanilla caches the map entity at game mode init, which is stale when the game mode is created before the map
//! entity. Looked up again before each use.
modded class SCR_MapMarkerManagerComponent
{
	//------------------------------------------------------------------------------------------------
	protected bool ELIFE_RefreshMapEntity()
	{
		SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
		if (mapEntity)
			m_MapEntity = mapEntity;

		return m_MapEntity != null;
	}

	//------------------------------------------------------------------------------------------------
	override void Update(float timeSlice)
	{
		if (ELIFE_RefreshMapEntity())
			super.Update(timeSlice);
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnMapOpen(MapConfiguration mapConfig)
	{
		if (ELIFE_RefreshMapEntity())
			super.OnMapOpen(mapConfig);
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnMapPanEnd(float x, float y)
	{
		if (ELIFE_RefreshMapEntity())
			super.OnMapPanEnd(x, y);
	}
}
