//------------------------------------------------------------------------------------------------
/*!
	One backend request for one join session. The manager keeps the latest one per player and applies a reply only while
	its session id and token still match (spec 5.3). The backend callbacks call the handlers below, which forward to the
	manager's result method of the same name.
*/
class ELIFE_JoinRequestContext
{
	int m_iPlayerId;
	string m_sSessionId;
	//! 0 marks a fire-and-forget request (EndSession's disconnect), which the manager does not track.
	int m_iToken;
	//! Only for fire-and-forget requests: when the manager stops holding the context (see ELIFE_JoinSessionManagerComponent).
	int m_iDeadlineMs;
	//! The callback that carries this request. Not ref: ELIFE_BaseRestCallback keeps itself alive until it fires.
	ELIFE_BaseRestCallback m_Callback;
	//! Create only: the names and appearance the player typed, kept so an error screen can echo them back.
	string m_sFirstName;
	string m_sLastName;
	int m_iAppearanceIndex;
	protected ELIFE_JoinSessionManagerComponent m_Manager;

	//------------------------------------------------------------------------------------------------
	void ELIFE_JoinRequestContext(ELIFE_JoinSessionManagerComponent manager)
	{
		m_Manager = manager;
	}

	//------------------------------------------------------------------------------------------------
	void OnPlayerConnected(ELIFE_EApiStatusCode status, JsonApiStruct data)
	{
		if (m_Manager)
			m_Manager.OnPlayerConnectedResult(this, status, data);
	}

	//------------------------------------------------------------------------------------------------
	void OnCreateCharacter(ELIFE_EApiStatusCode status, JsonApiStruct data)
	{
		if (m_Manager)
			m_Manager.OnCreateCharacterResult(this, status, data);
	}

	//------------------------------------------------------------------------------------------------
	void OnCharacterSelected(ELIFE_EApiStatusCode status, JsonApiStruct data)
	{
		if (m_Manager)
			m_Manager.OnCharacterSelectedResult(this, status, data);
	}

	//------------------------------------------------------------------------------------------------
	void OnPlayerDisconnected(ELIFE_EApiStatusCode status, JsonApiStruct data)
	{
		if (m_Manager)
			m_Manager.OnPlayerDisconnectedResult(this, status, data);
	}
}

//------------------------------------------------------------------------------------------------
/*!
	Platform profanity check of a new character's full name. The manager holds it until it answers, then forwards the
	filtered text with the context that asked for it.
*/
class ELIFE_JoinNameFilterRequest : ELIFE_ProfanityFilterRequest
{
	protected ELIFE_JoinSessionManagerComponent m_Manager;
	ref ELIFE_JoinRequestContext m_Context;
	string m_sFullName;

	//------------------------------------------------------------------------------------------------
	void ELIFE_JoinNameFilterRequest(ELIFE_JoinSessionManagerComponent manager)
	{
		m_Manager = manager;
	}

	//------------------------------------------------------------------------------------------------
	protected override void OnFiltered(string text)
	{
		if (m_Manager)
			m_Manager.OnNameFiltered(this, text);
	}
}

//------------------------------------------------------------------------------------------------
/*!
	Only the status string of a player-connect reply. The handler reads it when the generated enum comes back Unknown.
*/
class ELIFE_JoinStatusRaw : JsonApiStruct
{
	// TODO: generated ELIFE_PlayerConnectedResponse.status should be a string
	string status;

	//------------------------------------------------------------------------------------------------
	void ELIFE_JoinStatusRaw()
	{
		RegV("status");
	}
}
