//------------------------------------------------------------------------------------------------
/*!
	Finds where a player is put on land. First join (FindJoinPosition) uses a random ELIFE_SpawnPoint of the server's spawn mode. Death respawn (FindRespawnPosition) uses a random respawn point. Both fall back to FindRandomPosition (spec 5.7). Results are clear land positions, except a spawn point's origin when it has no clear spot in its radius. Yaw is random unless stated.
*/
class ELIFE_SpawnLocator
{
	protected static const int RANDOM_TRIES = 30;
	protected static const float CLEAR_RADIUS = 5;

	//------------------------------------------------------------------------------------------------
	//! A random clear land position in the world bounds. Falls back to a random ELIFE_SpawnPoint of any mode; false when neither exists.
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

		ELIFE_SpawnPoint point = ELIFE_SpawnPoint.GetRandomAny();
		if (!point)
			return false;

		pos = point.GetOrigin();
		angles = Vector(0, Math.RandomFloat(0, 360), 0);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! First-join position: a clear spot within the radius of a random spawn point of `mode`. If none is clear, the point's origin is used and a WARNING is logged.
	//! With no spawn point of `mode`, it uses FindRandomPosition and logs a WARNING. Yaw is the spawn point's.
	//! `who` is the session prefix for the WARNING, e.g. ELIFE_JoinSession.Describe().
	static bool FindJoinPosition(ELIFE_ESpawnMode mode, out vector pos, out vector angles, string who = "")
	{
		// TODO: SELECT_PER_RESTART shows the spawn selection screen on the first join after a server restart
		ELIFE_SpawnPoint point = ELIFE_SpawnPoint.GetRandom(mode);
		if (!point)
		{
			Print(string.Format("ELIFE_Join | %1 no spawn point for mode %2, using a random position", who, typename.EnumToString(ELIFE_ESpawnMode, mode)), LogLevel.WARNING);
			return FindRandomPosition(pos, angles);
		}

		// FindPositionInRadius sets pos to the point's origin when it finds no clear spot.
		if (!point.FindPositionInRadius(pos))
			Print(string.Format("ELIFE_Join | %1 no clear spot at spawn point %2, using its origin", who, point.GetDisplayName()), LogLevel.WARNING);

		// Face the way the spawn point faces (GetYawPitchRoll: index 0 is yaw).
		angles = Vector(0, point.GetYawPitchRoll()[0], 0);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Death respawn position: a clear land position within 5 m of a random respawn point. With no point, or none clear, it uses FindRandomPosition.
	//! `who` is the session prefix for the WARNING, e.g. ELIFE_JoinSession.Describe().
	static bool FindRespawnPosition(out vector pos, out vector angles, string who = "")
	{
		ELIFE_RespawnPoint point = ELIFE_RespawnPoint.GetRandom();
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
