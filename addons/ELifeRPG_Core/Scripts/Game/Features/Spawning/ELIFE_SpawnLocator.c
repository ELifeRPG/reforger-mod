//------------------------------------------------------------------------------------------------
/*!
	Finds where a player is put on land. First join (FindJoinPosition) uses a LIFE spawn point. Death respawn (FindRespawnPosition) uses the nearest respawn point. Both fall back to FindRandomPosition (spec 5.7). Results are clear land positions, except a LIFE spawn point's origin when it has no clear spot nearby. Yaw is random unless stated.
*/
class ELIFE_SpawnLocator
{
	protected static const int RANDOM_TRIES = 30;
	protected static const float CLEAR_RADIUS = 5;

	//------------------------------------------------------------------------------------------------
	//! A random clear land position in the world bounds. Falls back to a random LIFE spawn point; false when neither exists.
	static bool FindRandomPosition(out vector pos, out vector angles)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return false;

		vector mins;
		vector maxs;
		world.GetBoundBox(mins, maxs);
		float oceanY = world.GetOceanBaseHeight();

		for (int i = 0; i < RANDOM_TRIES; i++)
		{
			float x = Math.RandomFloat(mins[0], maxs[0]);
			float z = Math.RandomFloat(mins[2], maxs[2]);
			float y = world.GetSurfaceY(x, z);
			if (y <= oceanY + 1)
				continue;

			vector found;
			if (!SCR_WorldTools.FindEmptyTerrainPosition(found, Vector(x, y, z), CLEAR_RADIUS))
				continue;

			pos = found;
			angles = Vector(0, Math.RandomFloat(0, 360), 0);
			return true;
		}

		SCR_SpawnPoint spawnPoint = SCR_SpawnPoint.GetRandomSpawnPointForFaction("LIFE");
		if (!spawnPoint)
			return false;

		pos = spawnPoint.GetOrigin();
		angles = Vector(0, Math.RandomFloat(0, 360), 0);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! First-join position: a clear spot within 5 m of a random LIFE spawn point. If none is clear, the spawn point's origin is used and a WARNING is logged.
	//! With no LIFE spawn point, it uses FindRandomPosition and logs a WARNING. Yaw is the spawn point's.
	//! `who` is the session prefix for the WARNING, e.g. ELIFE_JoinSession.Describe().
	static bool FindJoinPosition(out vector pos, out vector angles, string who = "")
	{
		SCR_SpawnPoint point = SCR_SpawnPoint.GetRandomSpawnPointForFaction("LIFE");
		if (!point)
		{
			Print(string.Format("ELIFE_Join | %1 no LIFE spawn point, using a random position", who), LogLevel.WARNING);
			return FindRandomPosition(pos, angles);
		}

		vector found;
		if (SCR_WorldTools.FindEmptyTerrainPosition(found, point.GetOrigin(), CLEAR_RADIUS))
			pos = found;
		else
		{
			Print(string.Format("ELIFE_Join | %1 no clear spot at LIFE spawn point, using its origin", who), LogLevel.WARNING);
			pos = point.GetOrigin();
		}

		// Face the way the spawn point faces (GetYawPitchRoll: index 0 is yaw).
		angles = Vector(0, point.GetYawPitchRoll()[0], 0);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! A clear land position within 5 m of the respawn point nearest `near`. With no point, or none clear, it uses FindRandomPosition.
	//! `who` is the session prefix for the WARNING, e.g. ELIFE_JoinSession.Describe().
	static bool FindRespawnPosition(vector near, out vector pos, out vector angles, string who = "")
	{
		ELIFE_RespawnPoint point = ELIFE_RespawnPoint.FindNearest(near);
		if (point)
		{
			vector found;
			if (SCR_WorldTools.FindEmptyTerrainPosition(found, point.GetOrigin(), CLEAR_RADIUS))
			{
				pos = found;
				angles = Vector(0, Math.RandomFloat(0, 360), 0);
				return true;
			}

			Print(string.Format("ELIFE_Join | %1 no clear spot at respawn point %2, using a random position", who, point.m_sName), LogLevel.WARNING);
		}
		else
		{
			Print(string.Format("ELIFE_Join | %1 no respawn point registered, using a random position", who), LogLevel.WARNING);
		}

		return FindRandomPosition(pos, angles);
	}
}
