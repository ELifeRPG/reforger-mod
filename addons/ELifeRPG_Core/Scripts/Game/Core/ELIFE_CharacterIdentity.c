//------------------------------------------------------------------------------------------------
//! Character identity of players. The characterId is the one the player selected and the backend confirmed
//! (ELIFE_JoinSessionManagerComponent.GetConfirmedCharacterId). Server only; "" when no character is confirmed.
class ELIFE_CharacterIdentity
{
	//! Server-side. The confirmed characterId of this player, or "" when there is none yet, the player is gone, or this is not the server.
	//! Callers must treat "" as "no character": do not provision or act on it.
	static string GetCharacterId(int playerId)
	{
		if (!Replication.IsServer())
			return "";

		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (!manager)
			return "";

		return manager.GetConfirmedCharacterId(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-side. The player's profile name stands in for the character's name until characters exist.
	static string GetCharacterName(int playerId)
	{
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager || playerId == 0)
			return "";

		return playerManager.GetPlayerName(playerId);
	}
}
