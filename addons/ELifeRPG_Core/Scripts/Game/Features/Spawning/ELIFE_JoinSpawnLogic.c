//------------------------------------------------------------------------------------------------
/*!
	Spawn logic for the join flow (spec 5.11). It never runs the vanilla initial load or spawn: the join session decides when a
	character spawns, through SpawnLoadedCharacter_S. Every hook forwards to ELIFE_JoinSessionManagerComponent.
*/
[BaseContainerProps(category: "Respawn")]
class ELIFE_JoinSpawnLogic : SCR_SpawnLogic
{
	protected static ELIFE_JoinSpawnLogic s_Instance;
	protected const string CIV_FACTION_KEY = "LIFE";
	protected const string CIV_LOADOUT_NAME = "LIFE Player";

	//------------------------------------------------------------------------------------------------
	static ELIFE_JoinSpawnLogic GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	override void OnInit(SCR_RespawnSystemComponent owner)
	{
		super.OnInit(owner);

		s_Instance = this;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerAuditSuccess_S(int playerId)
	{
		super.OnPlayerAuditSuccess_S(playerId);

		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (manager)
			manager.StartSession(playerId);
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerDisconnected_S(int playerId, KickCauseCode cause, int timeout)
	{
		super.OnPlayerDisconnected_S(playerId, cause, timeout);

		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (manager)
			manager.EndSession(playerId, "disconnect");

		// After EndSession, which still reads the identity for the backend disconnect. Kicked players come through here too.
		ELIFE_PlayerIdentityRegistry.Unregister(playerId);
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerSpawned_S(int playerId, IEntity entity)
	{
		// TODO: remove spawn diagnostics once the first-spawn failure is fixed
		Print(string.Format("ELIFE_Join | DIAG player=%1 OnPlayerSpawned_S entity=%2", playerId, entity), LogLevel.WARNING);

		super.OnPlayerSpawned_S(playerId, entity);

		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (manager)
			manager.OnSpawned(playerId, entity);
	}

	//------------------------------------------------------------------------------------------------
	protected override void OnPlayerSpawnFailed_S(int playerId)
	{
		// TODO: remove spawn diagnostics once the first-spawn failure is fixed
		Print(string.Format("ELIFE_Join | DIAG player=%1 OnPlayerSpawnFailed_S called (engine or own path)", playerId), LogLevel.WARNING);

		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (manager)
			manager.OnSpawnFailed(playerId);
	}

	//------------------------------------------------------------------------------------------------
	protected override void OnPlayerEntityLost_S(int playerId)
	{
		super.OnPlayerEntityLost_S(playerId);

		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (manager)
			manager.OnEntityLost(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Defensive: the vanilla initial load or spawn must never run here. The join session decides spawns, so this only logs.
	protected override void ExcuteInitialLoadOrSpawn_S(int playerId)
	{
		Print(string.Format("ELIFE_Join | ExcuteInitialLoadOrSpawn_S blocked for player=%1 — the join session decides spawns", playerId), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	//! Spawns the player's loaded character. Called by the join session once the character is chosen; the body is Task 6.
	void SpawnLoadedCharacter_S(int playerId)
	{
		DoSpawn_S(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Spawns the selected character: CIV faction and loadout, then the position and prefab from the manager. Any failure goes to OnPlayerSpawnFailed_S.
	protected override void DoSpawn_S(int playerId)
	{
		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		ELIFE_JoinSession s = null;
		if (manager)
			s = manager.GetSession(playerId);

		if (!s)
		{
			Print(string.Format("ELIFE_Join | session= identity= player=%1 DoSpawn_S dropped, no session", playerId), LogLevel.WARNING);
			return;
		}

		if (!RequestCivilian_S(playerId, s.Describe()))
		{
			OnPlayerSpawnFailed_S(playerId);
			return;
		}

		ResourceName prefab;
		vector pos;
		vector angles;
		if (!manager.BuildSpawnRequest(playerId, prefab, pos, angles))
		{
			Print(string.Format("ELIFE_Join | %1 no spawn position found", s.Describe()), LogLevel.WARNING);
			OnPlayerSpawnFailed_S(playerId);
			return;
		}

		SCR_RespawnComponent respawn = GetPlayerRespawnComponent_S(playerId);

		// TODO: remove spawn diagnostics once the first-spawn failure is fixed
		SCR_FreeSpawnData data = new SCR_FreeSpawnData(prefab, pos, angles);
		Resource res = Resource.Load(prefab);
		bool prefabOk = res && res.IsValid();
		bool hasRespawn = respawn != null;
		Print(string.Format("ELIFE_Join | DIAG %1 prefab=%2 prefabOk=%3 pos=%4 angles=%5 hasRespawn=%6 dataValid=%7", s.Describe(), prefab, prefabOk, pos, angles, hasRespawn, data.IsValid()), LogLevel.WARNING);

		// TODO: remove spawn diagnostics once the first-spawn failure is fixed
		bool requested = false;
		if (respawn)
			requested = respawn.RequestSpawn(data);
		Print(string.Format("ELIFE_Join | DIAG RequestSpawn returned %1", requested), LogLevel.WARNING);

		if (!requested)
		{
			Print(string.Format("ELIFE_Join | %1 RequestSpawn refused", s.Describe()), LogLevel.WARNING);
			OnPlayerSpawnFailed_S(playerId);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Asks for the CIV faction and the "LIFE Player" loadout, as SCR_AutoSpawnLogic does. False when a component, faction or loadout is missing.
	protected bool RequestCivilian_S(int playerId, string describe)
	{
		SCR_PlayerFactionAffiliationComponent factionComponent = GetPlayerFactionComponent_S(playerId);
		FactionManager factionManager = GetGame().GetFactionManager();
		Faction faction = null;
		if (factionManager)
			faction = factionManager.GetFactionByKey(CIV_FACTION_KEY);

		if (!factionComponent || !faction)
		{
			Print(string.Format("ELIFE_Join | %1 faction request impossible (component=%2 faction=%3)", describe, factionComponent != null, faction != null), LogLevel.WARNING);
			return false;
		}

		// RequestFaction and RequestLoadout both return bool (api_search: SCR_PlayerFactionAffiliationComponent, SCR_PlayerLoadoutComponent).
		if (!factionComponent.RequestFaction(faction))
		{
			Print(string.Format("ELIFE_Join | %1 faction request refused", describe), LogLevel.WARNING);
			return false;
		}

		SCR_PlayerLoadoutComponent loadoutComponent = GetPlayerLoadoutComponent_S(playerId);
		SCR_LoadoutManager loadoutManager = GetGame().GetLoadoutManager();
		SCR_BasePlayerLoadout loadout = null;
		if (loadoutManager)
			loadout = loadoutManager.GetLoadoutByName(CIV_LOADOUT_NAME, CIV_FACTION_KEY);

		if (!loadoutComponent || !loadout)
		{
			Print(string.Format("ELIFE_Join | %1 loadout request impossible (component=%2 loadout=%3)", describe, loadoutComponent != null, loadout != null), LogLevel.WARNING);
			return false;
		}

		if (!loadoutComponent.RequestLoadout(loadout))
		{
			Print(string.Format("ELIFE_Join | %1 loadout request refused", describe), LogLevel.WARNING);
			return false;
		}

		// TODO: remove spawn diagnostics once the first-spawn failure is fixed
		Print("ELIFE_Join | DIAG faction and loadout requested ok", LogLevel.WARNING);
		return true;
	}
}
