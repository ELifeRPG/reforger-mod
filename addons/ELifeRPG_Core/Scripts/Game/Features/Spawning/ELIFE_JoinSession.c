//------------------------------------------------------------------------------------------------
/*!
	Per-player join record held by the server (spec 5.2).
	Timestamps are from System.GetTickCount(). 0 means "none" for the deadline, respawn and kick fields.
*/
class ELIFE_JoinSession
{
	// Identity and state
	string m_sSessionId;
	int m_iPlayerId;
	//! Snapshot of ELIFE_PlayerIdentityRegistry taken at session start (Register). Kept here so the session still has it after removal (the final PlayerDisconnected, logs, tests).
	string m_sIdentityId;
	//! Optional platform name sent with PlayerConnected ("Xbox", "PlayStation", "Pc"), or "" when unknown. Snapshot of ELIFE_PlayerIdentityRegistry taken at session start, never re-read.
	string m_sPlatform;
	ELIFE_EJoinState m_eState;
	int m_iStateEnteredAtMs;
	int m_iLastActivityMs;
	string m_sAccountId;
	ref array<ref ELIFE_JoinCharacterEntry> m_aCharacters;

	// Selection
	string m_sSelectedCharacterId;
	string m_sSelectedCharacterName;
	int m_iSelectedAppearance;
	ref map<string, int> m_mAppearanceByCharacter;

	// Requests and timing
	bool m_bRequestInFlight;
	int m_iRequestToken;
	int m_iRequestDeadlineMs;
	int m_iSpawnAttempts;
	vector m_vLastDeathPos;
	//! Last position read from the live character while IN_GAME (positive ids only); used when the death event has no live entity.
	vector m_vLastKnownPos;
	int m_iRespawnAtMs;
	int m_iKickAtMs;
	//! Set while a same-identity session's PlayerDisconnected is pending; the connect runs after its reply or timeout.
	bool m_bAwaitingDuplicateClear;
	//! Set while the connect waits for the backend client (ELIFE_Api) to exist. No request is in flight then; m_iRequestDeadlineMs is the wait's deadline.
	bool m_bAwaitingApi;
	//! Identity the backend still counts as connected for this player id, kept while m_bAwaitingApi. Cleared with the connect.
	string m_sLeftoverIdentity;
	//! Set while a recheck from LINKING is in flight: its failures go back to LINKING instead of REJECTED.
	bool m_bRechecking;
	//! Link code of the last LINKING screen, kept so a failed recheck can show that screen again. Never logged.
	string m_sLinkPin;
	//! Link URL from the last connect reply. A bearer credential for this identity while the PIN lives: never log it or print it.
	string m_sLinkUrl;
	//! Set when the player chose "Continue without linking" this session. The link screen is not shown again.
	bool m_bLinkSkipped;
	//! Time of the last recheck that was accepted (0 = none). A recheck within the cooldown of it is ignored.
	int m_iLastRecheckAtMs;
	//! Automatic connect retries sent so far while the backend answers unlinked. Reset with the session.
	int m_iUnlinkedRetries;
	//! When the next unlinked retry is sent (0 = none scheduled). Set only while the session waits in SESSION_PENDING with no request in flight.
	int m_iUnlinkedRetryAtMs;

#ifdef WORKBENCH
	// Self-test hooks: what Push last sent, and whether a kick was requested instead of performed (negative player ids).
	ELIFE_EJoinScreen m_eTestLastScreen;
	string m_sTestLastPayload;
	bool m_bTestKickRequested;
#endif

	//------------------------------------------------------------------------------------------------
	void ELIFE_JoinSession()
	{
		m_eState = ELIFE_EJoinState.NONE;
		m_iUnlinkedRetries = 0;
		m_iUnlinkedRetryAtMs = 0;
		m_aCharacters = new array<ref ELIFE_JoinCharacterEntry>;
		m_mAppearanceByCharacter = new map<string, int>;
	}

	//------------------------------------------------------------------------------------------------
	//! True when the move from `from` to `to` is part of the join state machine (spec 5.2).
	static bool IsTransitionAllowed(ELIFE_EJoinState from, ELIFE_EJoinState to)
	{
		if (from == ELIFE_EJoinState.ENDED)
			return false;
		if (to == ELIFE_EJoinState.ENDED)
			return true;

		switch (from)
		{
			case ELIFE_EJoinState.NONE:
				return to == ELIFE_EJoinState.SESSION_PENDING;
			case ELIFE_EJoinState.SESSION_PENDING:
				return to == ELIFE_EJoinState.REJECTED || to == ELIFE_EJoinState.CREATING || to == ELIFE_EJoinState.SELECTING || to == ELIFE_EJoinState.LINKING;
			case ELIFE_EJoinState.LINKING:
				// CREATING and SELECTING are for "Continue without linking", which goes straight to the character screen.
				return to == ELIFE_EJoinState.SESSION_PENDING || to == ELIFE_EJoinState.REJECTED || to == ELIFE_EJoinState.CREATING || to == ELIFE_EJoinState.SELECTING;
			case ELIFE_EJoinState.CREATING:
				return to == ELIFE_EJoinState.SELECTING || to == ELIFE_EJoinState.LOADING_CHARACTER || to == ELIFE_EJoinState.REJECTED;
			case ELIFE_EJoinState.SELECTING:
				return to == ELIFE_EJoinState.CREATING || to == ELIFE_EJoinState.LOADING_CHARACTER || to == ELIFE_EJoinState.REJECTED;
			case ELIFE_EJoinState.LOADING_CHARACTER:
				return to == ELIFE_EJoinState.SPAWNING || to == ELIFE_EJoinState.SELECTING || to == ELIFE_EJoinState.REJECTED;
			case ELIFE_EJoinState.SPAWNING:
				return to == ELIFE_EJoinState.IN_GAME || to == ELIFE_EJoinState.SPAWNING || to == ELIFE_EJoinState.REJECTED;
			case ELIFE_EJoinState.IN_GAME:
				return to == ELIFE_EJoinState.SPAWNING;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	bool HasCharacter(string id)
	{
		return !FindCharacterName(id).IsEmpty();
	}

	//------------------------------------------------------------------------------------------------
	//! Name of the cached character with this id, or "" when it is not in this session's list.
	string FindCharacterName(string id)
	{
		if (id.IsEmpty())
			return "";

		foreach (ELIFE_JoinCharacterEntry entry : m_aCharacters)
		{
			if (entry.id == id)
				return entry.name;
		}

		return "";
	}

	//------------------------------------------------------------------------------------------------
	string Describe()
	{
		return string.Format("session=%1 identity=%2 player=%3", m_sSessionId, m_sIdentityId, m_iPlayerId);
	}
}
