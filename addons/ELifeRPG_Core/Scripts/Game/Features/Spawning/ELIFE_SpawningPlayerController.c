//------------------------------------------------------------------------------------------------
//! Join-flow RPC transport on the player controller. Client asks (RpcAsk_*) go to the server, server answers (RpcDo_*) go to the owner.
//! The RpcAsk_* handlers cap-check and forward to ELIFE_JoinSessionManagerComponent, which owns the state.
modded class ELIFE_PlayerController
{
	//! Names and ids are capped at this many characters; anything longer is rejected before processing.
	protected static const int JOIN_STRING_MAX = 64;

	//! Set by the first join screen the server sends. Once any screen has arrived, the local Connecting fallback stays quiet.
	protected bool m_bJoinScreenReceived;
	protected bool m_bInGameReceived;
	protected bool m_bJoinStartChecked;

	//! Last applied push, used to drop the second copy a host gets (owner RPC plus the direct call).
	protected static const int JOIN_DUPLICATE_WINDOW_MS = 250;
	protected bool m_bHasLastShown;
	protected ELIFE_EJoinScreen m_eLastShownScreen;
	protected string m_sLastShownPayload;
	protected int m_iLastShownTick;

	//------------------------------------------------------------------------------------------------
	//! Server-only. Shows a join screen on the owner's client. A listen-server host is its own owner, so the RPC doesn't self-deliver there and the local copy is applied directly.
	void ELIFE_ShowSession(ELIFE_EJoinScreen screen, string payloadJson)
	{
		Rpc(RpcDo_ShowSession, screen, payloadJson);

		if (Replication.IsServer() && GetPlayerId() == SCR_PlayerController.GetLocalPlayerId())
			RpcDo_ShowSession(screen, payloadJson);
	}

	//------------------------------------------------------------------------------------------------
	//! Client asks the server to create a character.
	void ELIFE_RequestCreate(string firstName, string lastName, int appearanceIndex)
	{
		Rpc(RpcAsk_RequestCreate, firstName, lastName, appearanceIndex);
	}

	//------------------------------------------------------------------------------------------------
	//! Client asks the server to spawn one of the characters it was offered.
	void ELIFE_RequestSelect(string characterId)
	{
		Rpc(RpcAsk_RequestSelect, characterId);
	}

	//------------------------------------------------------------------------------------------------
	//! Client asks the server to open the create screen.
	void ELIFE_RequestOpenCreate()
	{
		Rpc(RpcAsk_RequestOpenCreate);
	}

	//------------------------------------------------------------------------------------------------
	//! Client asks the server to go back to the select screen.
	void ELIFE_RequestBackToSelect()
	{
		Rpc(RpcAsk_RequestBackToSelect);
	}

	//------------------------------------------------------------------------------------------------
	//! Client asks the server to check the link again, from the link screen ("Check again").
	void ELIFE_RequestRecheck()
	{
		Rpc(RpcAsk_RequestRecheck);
	}

	//------------------------------------------------------------------------------------------------
	//! Client asks the server to continue without linking the web account, from the link screen ("Continue without linking").
	void ELIFE_RequestSkipLink()
	{
		Rpc(RpcAsk_RequestSkipLink);
	}

	//------------------------------------------------------------------------------------------------
	//! Client has closed a rejected or error screen; the server can kick without waiting for the timeout.
	void ELIFE_AckClosed()
	{
		Rpc(RpcAsk_AckClosed);
	}

	//------------------------------------------------------------------------------------------------
	//! Opens Connecting once on the local controller, when there is no character yet and the server hasn't sent a screen.
	override void OnUpdate(float timeSlice)
	{
		super.OnUpdate(timeSlice);

		if (m_bJoinStartChecked)
			return;

		m_bJoinStartChecked = true;

		if (GetPlayerId() != SCR_PlayerController.GetLocalPlayerId())
			return;

		if (m_bJoinScreenReceived || m_bInGameReceived || SCR_PlayerController.GetLocalControlledEntity())
			return;

		ELIFE_JoinMenu.ShowFromServer(ELIFE_EJoinScreen.CONNECTING, "{}");
	}

	//------------------------------------------------------------------------------------------------
	//! Server answer, owner only. Shows the join menu screen; IN_GAME closes it.
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_ShowSession(int screen, string payloadJson)
	{
		//! The same push can arrive twice on a host; a repeat within the window is dropped without a log line.
		int now = System.GetTickCount();
		if (m_bHasLastShown && screen == m_eLastShownScreen && payloadJson == m_sLastShownPayload && now - m_iLastShownTick < JOIN_DUPLICATE_WINDOW_MS)
			return;

		m_bHasLastShown = true;
		m_eLastShownScreen = screen;
		m_sLastShownPayload = payloadJson;
		m_iLastShownTick = now;

		Print(string.Format("ELIFE_Join | client screen=%1", typename.EnumToString(ELIFE_EJoinScreen, screen)), LogLevel.NORMAL);

		m_bJoinScreenReceived = true;
		if (screen == ELIFE_EJoinScreen.IN_GAME)
			m_bInGameReceived = true;

		ELIFE_JoinMenu.ShowFromServer(screen, payloadJson);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_RequestCreate(string firstName, string lastName, int appearanceIndex)
	{
		if (!IsJoinStringWithinCap(firstName, "RpcAsk_RequestCreate") || !IsJoinStringWithinCap(lastName, "RpcAsk_RequestCreate"))
			return;

		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (manager)
			manager.HandleRequestCreate(GetPlayerId(), firstName, lastName, appearanceIndex);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_RequestSelect(string characterId)
	{
		if (!IsJoinStringWithinCap(characterId, "RpcAsk_RequestSelect"))
			return;

		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (manager)
			manager.HandleRequestSelect(GetPlayerId(), characterId);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_RequestOpenCreate()
	{
		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (manager)
			manager.HandleRequestOpenCreate(GetPlayerId());
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_RequestBackToSelect()
	{
		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (manager)
			manager.HandleRequestBackToSelect(GetPlayerId());
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_RequestRecheck()
	{
		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (manager)
			manager.HandleRequestRecheck(GetPlayerId());
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_RequestSkipLink()
	{
		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (manager)
			manager.HandleRequestSkipLink(GetPlayerId());
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_AckClosed()
	{
		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (manager)
			manager.HandleAckClosed(GetPlayerId());
	}

	//------------------------------------------------------------------------------------------------
	//! Logs and returns false when a string is over JOIN_STRING_MAX characters.
	protected bool IsJoinStringWithinCap(string value, string rpcName)
	{
		if (value.Length() <= JOIN_STRING_MAX)
			return true;

		Print(string.Format("ELIFE_Join | rpc %1 rejected: string over %2 characters from player=%3", rpcName, JOIN_STRING_MAX, GetPlayerId()), LogLevel.WARNING);
		return false;
	}
}
