//------------------------------------------------------------------------------------------------
[EntityEditorProps(category: "ELifeRPG/NPC", description: "Carries NPC requests to the server and their answers back to the owning client")]
class ELIFE_NpcCharacterComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! NPCs are server-owned, so a customer's traffic rides on the character the client owns. It knows no NPC:
//! opening, closing and lines are generic, and anything else is a request or message (kind, a, b, text) that
//! the NPC's ELIFE_NpcComponent subclass reads.
class ELIFE_NpcCharacterComponent : ScriptComponent
{
	//! Client-side: the visit the server granted last, and with whom.
	protected RplId m_GrantedNpcId = RplId.Invalid();
	protected int m_iGrantedVisitId;

	//------------------------------------------------------------------------------------------------
	//! Client-side. The id of the visit granted with this NPC, 0 if none.
	int GetGrantedVisitId(notnull IEntity npc)
	{
		RplId npcId = GetNpcId(npc);
		if (!npcId.IsValid() || npcId != m_GrantedNpcId)
			return 0;

		return m_iGrantedVisitId;
	}

	//------------------------------------------------------------------------------------------------
	void RequestOpen(notnull IEntity npc)
	{
		RplId npcId = GetNpcId(npc);
		if (npcId.IsValid())
			Rpc(RpcAsk_Open, npcId);
	}

	//------------------------------------------------------------------------------------------------
	void RequestClose(notnull IEntity npc)
	{
		RplId npcId = GetNpcId(npc);
		if (npcId.IsValid())
			Rpc(RpcAsk_Close, npcId);
	}

	//------------------------------------------------------------------------------------------------
	//! The dialogue moving to a step he has a line for. The server only says lines marked askable.
	void RequestSay(notnull IEntity npc, string lineName)
	{
		RplId npcId = GetNpcId(npc);
		if (npcId.IsValid())
			Rpc(RpcAsk_Say, npcId, lineName);
	}

	//------------------------------------------------------------------------------------------------
	//! Anything the NPC offers; it arrives in ELIFE_NpcComponent.OnRequest on the server.
	void Request(notnull IEntity npc, int kind, int a = 0, int b = 0, string text = "")
	{
		RplId npcId = GetNpcId(npc);
		if (npcId.IsValid())
			Rpc(RpcAsk_Request, npcId, kind, a, b, text);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-side senders: on a client-owned character these route to the client, on a host's own they call directly.
	//! visitId only for a grant.
	void SendGrant(RplId npcId, bool granted, int reason, int visitId = 0)
	{
		Rpc(RpcDo_Grant, npcId, granted, reason, visitId);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-side. Use ELIFE_NpcComponent.Send.
	void SendMessage(RplId npcId, int visitId, int kind, int a, int b, string text)
	{
		Rpc(RpcDo_Message, npcId, visitId, kind, a, b, text);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Open(RplId npcId)
	{
		ELIFE_NpcComponent npc = FindNpc(npcId, "open");
		int playerId = GetPlayerId();
		if (npc && playerId != 0)
			npc.RequestOpen(playerId, GetOwner(), this);
	}

	//------------------------------------------------------------------------------------------------
	//! Not range-checked: walking away is one of the reasons to close.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Close(RplId npcId)
	{
		ELIFE_NpcComponent npc = FindNpc(npcId, "close");
		if (npc)
			npc.RequestClose(GetPlayerId());
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Say(RplId npcId, string lineName)
	{
		ELIFE_NpcComponent npc = FindNpc(npcId, "say");
		if (npc)
			npc.RequestSay(GetPlayerId(), GetOwner(), lineName);
	}

	//------------------------------------------------------------------------------------------------
	//! The NPC checks the customer and range itself and answers with a reason.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Request(RplId npcId, int kind, int a, int b, string text)
	{
		ELIFE_NpcComponent npc = FindNpc(npcId, "request");
		int playerId = GetPlayerId();
		if (npc && playerId != 0)
			npc.OnRequest(playerId, GetOwner(), this, kind, a, b, text);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_Message(RplId npcId, int visitId, int kind, int a, int b, string text)
	{
		IEntity entity = FindEntity(npcId);
		if (!entity)
			return;

		ELIFE_NpcComponent npc = ELIFE_NpcComponent.Cast(entity.FindComponent(ELIFE_NpcComponent));
		if (npc)
			npc.OnMessage(this, visitId, kind, a, b, text);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_Grant(RplId npcId, bool granted, int reason, int visitId)
	{
		IEntity entity = FindEntity(npcId);
		if (!entity)
			return;

		ELIFE_NpcComponent npc = ELIFE_NpcComponent.Cast(entity.FindComponent(ELIFE_NpcComponent));
		if (!npc)
			return;

		if (granted)
		{
			m_GrantedNpcId = npcId;
			m_iGrantedVisitId = visitId;
			if (!npc.OnGranted(this))
				RequestClose(entity);

			return;
		}

		string title = npc.GetDisplayName();
		SCR_HintManagerComponent.ShowCustomHint(npc.GetRefusalText(reason), title);
	}

	//------------------------------------------------------------------------------------------------
	protected RplId GetNpcId(notnull IEntity npc)
	{
		RplComponent rpl = RplComponent.Cast(npc.FindComponent(RplComponent));
		if (!rpl)
			return RplId.Invalid();

		return rpl.Id();
	}

	//------------------------------------------------------------------------------------------------
	//! Derived from the character on the server, so a client can't speak for anyone else.
	protected int GetPlayerId()
	{
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return 0;

		return playerManager.GetPlayerIdFromControlledEntity(GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	protected IEntity FindEntity(RplId npcId)
	{
		RplComponent rpl = RplComponent.Cast(Replication.FindItem(npcId));
		if (!rpl)
			return null;

		return rpl.GetEntity();
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_NpcComponent FindNpc(RplId npcId, string request)
	{
		IEntity entity = FindEntity(npcId);
		ELIFE_NpcComponent npc;
		if (entity)
			npc = ELIFE_NpcComponent.Cast(entity.FindComponent(ELIFE_NpcComponent));

		if (!npc)
			Print(string.Format("ELIFE_NpcCharacterComponent | %1 rejected, not an NPC", request), LogLevel.WARNING);

		return npc;
	}
}
