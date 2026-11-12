//------------------------------------------------------------------------------------------------
/*!
	Identity id sent to the backend for a joining player (spec 5.2).
	Outside Workbench this is the Bohemia identity id. In Workbench, including PeerTool peers, real ids must not leave the machine,
	so the id is a valid low GUID `00000000-0000-HHHH-HHHH-PPPPPPPPPPPP`: HHHHHHHH is a hash of the computer name, stable per computer,
	and PPPPPPPPPPPP is the peer number, 0 for the Workbench player and 1..X for peers. The bridge parses it as System.Guid.
*/
class ELIFE_JoinIdentity
{
	protected static const string HEX_DIGITS = "0123456789abcdef";
	protected static const string EMPTY_MACHINE_HASH = "00000000";

	//------------------------------------------------------------------------------------------------
	//! Identity id for a server player. Workbench builds never return a Bohemia id.
	static string Resolve(int playerId)
	{
#ifdef WORKBENCH
		int peerNo;
		if (playerId == SCR_PlayerController.GetLocalPlayerId())
		{
			peerNo = 0;
		}
		else
		{
			// Workbench's own player is player 1, so peers start at 1 after the -1.
			// A peer that reconnects gets a new player id and therefore a new peer number.
			peerNo = playerId - 1;
			if (peerNo == 0)
				peerNo = playerId;
		}

		return BuildWorkbenchId(System.GetMachineName(), peerNo);
#else
		return SCR_PlayerIdentityUtils.GetPlayerIdentityId(playerId);
#endif
	}

	//------------------------------------------------------------------------------------------------
	/*!
		Optional platform for a server player, sent as `platform` (descriptive only). "" when unknown; the request then carries an empty `platform`, which the Bridge ignores.
		Workbench has no real platform, so it always gives "". Read per call, never cached: the caller stores it per session.
	*/
	static string ResolvePlatform(int playerId)
	{
#ifdef WORKBENCH
		return "";
#else
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return "";

		PlatformKind kind = playerManager.GetPlatformKind(playerId);
		string kindName = typename.EnumToString(PlatformKind, kind);
		string platform = MapPlatformKindName(kindName);
		if (platform.IsEmpty())
			Print(string.Format("ELIFE_Join | player=%1 platform enum [%2] not recognised, platform left empty", playerId, kindName), LogLevel.NORMAL);

		return platform;
#endif
	}

	//------------------------------------------------------------------------------------------------
	//! Maps an engine PlatformKind name to the wire value. Case-insensitive. Pure: no engine calls, so it can be tested.
	//! Xbox names are checked before PC, so a name carrying both gives "Xbox". Anything unrecognised, including "NONE" and "", gives "".
	static string MapPlatformKindName(string kindName)
	{
		string upper = kindName;
		upper.ToUpper();

		if (upper.Contains("XBOX") || upper.Contains("XBL"))
			return "Xbox";

		if (upper.Contains("PSN") || upper.Contains("PLAYSTATION") || upper.Contains("PS4") || upper.Contains("PS5"))
			return "PlayStation";

		if (upper.Contains("STEAM") || upper.Contains("PC") || upper.Contains("WINDOWS"))
			return "Pc";

		return "";
	}

	//------------------------------------------------------------------------------------------------
	//! `00000000-0000-HHHH-HHHH-PPPPPPPPPPPP`, all lowercase. Pure: no engine calls, so it can be tested.
	//! An empty machine name gives hash part `00000000`. A negative peer number counts as 0.
	static string BuildWorkbenchId(string machineName, int peerNo)
	{
		string machineHash = EMPTY_MACHINE_HASH;
		if (!machineName.IsEmpty())
			machineHash = ToHex8(Math.AbsInt(machineName.Hash()));

		if (peerNo < 0)
			peerNo = 0;

		string peerText = string.Format("%1", peerNo);
		while (peerText.Length() < 12)
			peerText = "0" + peerText;

		return "00000000-0000-" + machineHash.Substring(0, 4) + "-" + machineHash.Substring(4, 4) + "-" + peerText;
	}

	//------------------------------------------------------------------------------------------------
	//! Exactly 8 lowercase hex digits, leading zeros included.
	protected static string ToHex8(int value)
	{
		string hex = "";
		for (int i = 0; i < 8; i++)
		{
			int nibble = (value >> (i * 4)) & 15;
			hex = HEX_DIGITS.Substring(nibble, 1) + hex;
		}

		return hex;
	}
}
