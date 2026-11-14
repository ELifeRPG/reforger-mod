//------------------------------------------------------------------------------------------------
class ELIFE_SpawnPointClass : GenericEntityClass
{
}

//------------------------------------------------------------------------------------------------
/*!
	Spawn point of the join flow (replaces the vanilla SCR_SpawnPoint in the join spawn). Each point belongs to one spawn
	mode: RANDOM_FIRST_SPAWN points place the player at a random point of that mode on the first spawn, SELECT_PER_RESTART
	points are offered in the spawn selection on every restart. The player spawns within m_iRadius of the point.
	The server keeps the play-mode instances in a static list; edit-mode and client instances never join it, and the
	destructor always takes the point out.
*/
class ELIFE_SpawnPoint : GenericEntity
{
	//! Attempts made by FindPositionInRadius before it gives up.
	static const int RADIUS_TRIES = 10;

	[Attribute("0", UIWidgets.ComboBox, desc: "Spawn mode this point belongs to", enums: ParamEnumArray.FromEnum(ELIFE_ESpawnMode))]
	protected ELIFE_ESpawnMode m_eSpawnMode;

	[Attribute("", UIWidgets.EditBox, desc: "Name in the spawn selection")]
	protected string m_sName;

	[Attribute("", UIWidgets.ResourcePickerThumbnail, desc: "Image in the spawn selection", params: "edds imageset")]
	protected ResourceName m_sImage;

	[Attribute("5", UIWidgets.EditBox, desc: "Radius the player spawns in, metres", params: "0 inf")]
	protected int m_iRadius;

	protected static ref array<ELIFE_SpawnPoint> s_aPoints;

	//------------------------------------------------------------------------------------------------
	void ELIFE_SpawnPoint(IEntitySource src, IEntity parent)
	{
		if (!GetGame().InPlayMode() || !Replication.IsServer())
			return;

		if (!s_aPoints)
			s_aPoints = new array<ELIFE_SpawnPoint>();

		s_aPoints.Insert(this);
	}

	//------------------------------------------------------------------------------------------------
	void ~ELIFE_SpawnPoint()
	{
		if (s_aPoints)
			s_aPoints.RemoveItem(this);
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_ESpawnMode GetSpawnMode()
	{
		return m_eSpawnMode;
	}

	//------------------------------------------------------------------------------------------------
	string GetName()
	{
		return m_sName;
	}

	//------------------------------------------------------------------------------------------------
	ResourceName GetImage()
	{
		return m_sImage;
	}

	//------------------------------------------------------------------------------------------------
	//! Spawn radius in metres, never negative.
	int GetRadius()
	{
		if (m_iRadius < 0)
			return 0;

		return m_iRadius;
	}

	//------------------------------------------------------------------------------------------------
	//! Random registered point of the given mode, or null when none is registered.
	static ELIFE_SpawnPoint GetRandom(ELIFE_ESpawnMode mode)
	{
		if (!s_aPoints)
			return null;

		array<ELIFE_SpawnPoint> candidates = new array<ELIFE_SpawnPoint>();
		foreach (ELIFE_SpawnPoint point : s_aPoints)
		{
			if (point.GetSpawnMode() == mode)
				candidates.Insert(point);
		}

		if (candidates.Count() == 0)
			return null;

		return candidates[Math.RandomInt(0, candidates.Count())];
	}

	//------------------------------------------------------------------------------------------------
	//! Random registered point of any mode, or null when none is registered.
	static ELIFE_SpawnPoint GetRandomAny()
	{
		if (!s_aPoints || s_aPoints.Count() == 0)
			return null;

		return s_aPoints[Math.RandomInt(0, s_aPoints.Count())];
	}

	//------------------------------------------------------------------------------------------------
	//! Uniform random point in the disc of `radius` around `center` on the ground plane; y is `center[1]`. A radius of 0 or less gives `center`.
	static vector RandomPointInDisc(vector center, float radius)
	{
		if (radius <= 0)
			return center;

		float r = radius * Math.Sqrt(Math.RandomFloat01());
		float a = Math.RandomFloat(0, Math.PI2);
		return Vector(center[0] + r * Math.Cos(a), center[1], center[2] + r * Math.Sin(a));
	}

	//------------------------------------------------------------------------------------------------
	//! Finds an empty ground position inside the spawn radius. Returns false with `pos` set to the point origin when none is
	//! found. Caller logs the failure.
	bool FindPositionInRadius(out vector pos)
	{
		pos = GetOrigin();

		World world = GetGame().GetWorld();
		if (!world)
			return false;

		int tries = RADIUS_TRIES;
		if (GetRadius() <= 0)
			tries = 1;

		for (int i = 0; i < tries; i++)
		{
			vector candidate = RandomPointInDisc(GetOrigin(), GetRadius());
			candidate[1] = world.GetSurfaceY(candidate[0], candidate[2]);

			vector found;
			if (SCR_WorldTools.FindEmptyTerrainPosition(found, candidate, 1))
			{
				pos = found;
				return true;
			}
		}

		return false;
	}
}
