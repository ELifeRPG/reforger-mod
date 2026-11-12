//------------------------------------------------------------------------------------------------
/*!
	Identity registry: the playerId -> gameIdentityId and platform mapping (spec 5.2).
	Server only. Filled once per join by Register (the only reader of the engine, through ELIFE_JoinIdentity), so every later
	reader uses the stored value through GetIdentityId, GetPlatform, Has or FindPlayerId and never asks the engine again.
	An entry is removed after the disconnect hook (ELIFE_JoinSpawnLogic.OnPlayerDisconnected_S), not at session removal:
	a restart re-registers the same player id anyway. The join session keeps its own snapshot of both values.
	Nothing is logged here beyond what the join flow already logs.
*/
class ELIFE_PlayerIdentityRegistry
{
	protected static ref map<int, string> s_mIdentities;
	protected static ref map<int, string> s_mPlatforms;

	//------------------------------------------------------------------------------------------------
	//! Resolves the identity and platform of a joining player from the engine, stores both and returns the identity.
	//! Overwrites an existing entry for this player id. An empty identity stores nothing: an old entry is removed and "" returned.
	static string Register(int playerId)
	{
		string identityId = ELIFE_JoinIdentity.Resolve(playerId);
		if (identityId.IsEmpty())
		{
			Unregister(playerId);
			return "";
		}

		string platform = ELIFE_JoinIdentity.ResolvePlatform(playerId);
		EnsureMaps();
		s_mIdentities.Set(playerId, identityId);
		s_mPlatforms.Set(playerId, platform);
		return identityId;
	}

	//------------------------------------------------------------------------------------------------
	//! Stored identity of the player, or "" when none is registered. Never calls the engine.
	static string GetIdentityId(int playerId)
	{
		if (!s_mIdentities || !s_mIdentities.Contains(playerId))
			return "";

		return s_mIdentities.Get(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Stored platform of the player ("Xbox", "PlayStation", "Pc"), or "" when unknown or none is registered. Never calls the engine.
	static string GetPlatform(int playerId)
	{
		if (!s_mPlatforms || !s_mPlatforms.Contains(playerId))
			return "";

		return s_mPlatforms.Get(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! True when an identity is registered for this player id.
	static bool Has(int playerId)
	{
		if (!s_mIdentities)
			return false;

		return s_mIdentities.Contains(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Removes the identity and platform of the player. Called after the disconnect hook has run EndSession.
	static void Unregister(int playerId)
	{
		if (s_mIdentities && s_mIdentities.Contains(playerId))
			s_mIdentities.Remove(playerId);

		if (s_mPlatforms && s_mPlatforms.Contains(playerId))
			s_mPlatforms.Remove(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Reverse lookup: the player id registered with this identity, or 0 when none is. 0 is never a real player id.
	static int FindPlayerId(string identityId)
	{
		if (identityId.IsEmpty() || !s_mIdentities)
			return 0;

		foreach (int playerId, string storedId : s_mIdentities)
		{
			if (storedId == identityId)
				return playerId;
		}

		return 0;
	}

#ifdef WORKBENCH
	//------------------------------------------------------------------------------------------------
	//! Self-tests only: stores an entry without the engine. Only negative player ids are accepted, so no real player is touched.
	//! Tests must Unregister every id they set. There is deliberately no clear: it would wipe real players in play mode.
	static void SetForTests(int playerId, string identityId, string platform)
	{
		if (playerId >= 0)
			return;

		EnsureMaps();
		s_mIdentities.Set(playerId, identityId);
		s_mPlatforms.Set(playerId, platform);
	}
#endif

	//------------------------------------------------------------------------------------------------
	protected static void EnsureMaps()
	{
		if (!s_mIdentities)
			s_mIdentities = new map<int, string>();

		if (!s_mPlatforms)
			s_mPlatforms = new map<int, string>();
	}
}
