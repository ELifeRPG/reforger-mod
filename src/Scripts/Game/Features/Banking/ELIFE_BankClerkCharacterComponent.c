//------------------------------------------------------------------------------------------------
[EntityEditorProps(category: "ELifeRPG/Banking", description: "Carries bank clerk requests to the server and its answers back to the owning client")]
class ELIFE_BankClerkCharacterComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! The clerk is server-owned, so the customer's traffic rides on the character the client owns. No money logic here.
class ELIFE_BankClerkCharacterComponent : ScriptComponent
{
	//! Client-side: the latest customer context, which may arrive before the grant. It counts only for the granted visit.
	protected RplId m_ContextClerkId = RplId.Invalid();
	protected int m_iContextVisitId;
	protected int m_iGrantedVisitId;
	protected ELIFE_EClerkAccountStatus m_eContextAccounts;
	protected bool m_bContextOpenedThisVisit;

	//------------------------------------------------------------------------------------------------
	//! Client-side. False if none arrived for this clerk during the visit.
	bool GetCustomerContext(notnull IEntity clerk, out ELIFE_EClerkAccountStatus accounts, out bool openedThisVisit)
	{
		RplId clerkId = GetClerkId(clerk);
		if (!clerkId.IsValid() || clerkId != m_ContextClerkId || m_iContextVisitId != m_iGrantedVisitId)
			return false;

		accounts = m_eContextAccounts;
		openedThisVisit = m_bContextOpenedThisVisit;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	void RequestOpen(notnull IEntity clerk)
	{
		RplId clerkId = GetClerkId(clerk);
		if (clerkId.IsValid())
			Rpc(RpcAsk_Open, clerkId);
	}

	//------------------------------------------------------------------------------------------------
	void RequestClose(notnull IEntity clerk)
	{
		RplId clerkId = GetClerkId(clerk);
		if (clerkId.IsValid())
			Rpc(RpcAsk_Close, clerkId);
	}

	//------------------------------------------------------------------------------------------------
	//! The dialogue moving to a step he has a line for. The server only says lines meant to be asked for.
	void RequestSay(notnull IEntity clerk, ELIFE_EClerkLine line)
	{
		RplId clerkId = GetClerkId(clerk);
		if (clerkId.IsValid())
			Rpc(RpcAsk_Say, clerkId, line);
	}

	//------------------------------------------------------------------------------------------------
	void RequestOpenAccount(notnull IEntity clerk)
	{
		RplId clerkId = GetClerkId(clerk);
		if (clerkId.IsValid())
			Rpc(RpcAsk_OpenAccount, clerkId);
	}

	//------------------------------------------------------------------------------------------------
	void RequestTransferAccounts(notnull IEntity clerk)
	{
		RplId clerkId = GetClerkId(clerk);
		if (clerkId.IsValid())
			Rpc(RpcAsk_TransferAccounts, clerkId);
	}

	//------------------------------------------------------------------------------------------------
	void RequestTransferQuote(notnull IEntity clerk, string fromNumber, string targetNumber, int amount)
	{
		RplId clerkId = GetClerkId(clerk);
		if (clerkId.IsValid())
			Rpc(RpcAsk_TransferQuote, clerkId, fromNumber, targetNumber, amount);
	}

	//------------------------------------------------------------------------------------------------
	void RequestTransferConfirm(notnull IEntity clerk, int quoteId)
	{
		RplId clerkId = GetClerkId(clerk);
		if (clerkId.IsValid())
			Rpc(RpcAsk_TransferConfirm, clerkId, quoteId);
	}

	//------------------------------------------------------------------------------------------------
	//! The answer to the clerk's question about a large transfer; NONE puts it away.
	void RequestTransferPurpose(notnull IEntity clerk, int quoteId, ELIFE_ETransferPurpose purpose)
	{
		RplId clerkId = GetClerkId(clerk);
		if (clerkId.IsValid())
			Rpc(RpcAsk_TransferPurpose, clerkId, quoteId, purpose);
	}

	//------------------------------------------------------------------------------------------------
	//! Taking the receipt or account papers he offered.
	void RequestPaper(notnull IEntity clerk, ELIFE_EBankPaperKind kind)
	{
		RplId clerkId = GetClerkId(clerk);
		if (clerkId.IsValid())
			Rpc(RpcAsk_Paper, clerkId, kind);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-side senders: on a client-owned character these route to the client, on a host's own they call directly.
	//! visitId only for a grant.
	void SendGrant(RplId clerkId, bool granted, int reason, int visitId = 0)
	{
		//! Granted visits are greeted when the visit starts.
		if (!granted)
			SayOutcome(clerkId, reason, ELIFE_EClerkLine.NONE);

		Rpc(RpcDo_Grant, clerkId, granted, reason, visitId);
	}

	//------------------------------------------------------------------------------------------------
	void SendCustomerContext(RplId clerkId, int visitId, ELIFE_EClerkAccountStatus accounts, bool openedThisVisit)
	{
		Rpc(RpcDo_CustomerContext, clerkId, visitId, accounts, openedThisVisit);
	}

	//------------------------------------------------------------------------------------------------
	void SendPaperResult(RplId clerkId, ELIFE_EBankPaperKind kind, int reason, bool onGround)
	{
		Rpc(RpcDo_PaperResult, clerkId, kind, reason, onGround);
	}

	//------------------------------------------------------------------------------------------------
	void SendAccountOpened(RplId clerkId, bool opened, int reason, string accountNumber)
	{
		SayOutcome(clerkId, reason, ELIFE_EClerkLine.ACCOUNT_OPENED);
		Rpc(RpcDo_AccountOpened, clerkId, opened, reason, accountNumber);
	}

	//------------------------------------------------------------------------------------------------
	void SendClerkAccounts(RplId clerkId, int reason, string json)
	{
		SayOutcome(clerkId, reason, ELIFE_EClerkLine.TRANSFER_FORM);
		Rpc(RpcDo_ClerkAccounts, clerkId, reason, json);
	}

	//------------------------------------------------------------------------------------------------
	void SendTransferQuote(RplId clerkId, int reason, string json)
	{
		//! An accepted slip was already announced as TRANSFER_PROCESSING.
		SayOutcome(clerkId, reason, ELIFE_EClerkLine.NONE);
		Rpc(RpcDo_TransferQuote, clerkId, reason, json);
	}

	//------------------------------------------------------------------------------------------------
	//! amount, fee and balance are the Bridge's display text.
	void SendTransferResult(RplId clerkId, int reason, string amount, string fee, string balance)
	{
		SayOutcome(clerkId, reason, ELIFE_EClerkLine.TRANSFER_DONE);
		Rpc(RpcDo_TransferResult, clerkId, reason, amount, fee, balance);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Open(RplId clerkId)
	{
		ELIFE_BankClerkComponent clerk = FindClerk(clerkId, "open");
		int playerId = GetPlayerId();
		if (clerk && playerId != 0)
			clerk.RequestOpen(playerId, GetOwner(), this);
	}

	//------------------------------------------------------------------------------------------------
	//! Not range-checked: walking away is one of the reasons to close.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Close(RplId clerkId)
	{
		ELIFE_BankClerkComponent clerk = FindClerk(clerkId, "close");
		if (clerk)
			clerk.RequestClose(GetPlayerId());
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Say(RplId clerkId, int line)
	{
		ELIFE_BankClerkComponent clerk = FindClerk(clerkId, "say");
		if (clerk)
			clerk.RequestSay(GetPlayerId(), GetOwner(), line);
	}

	//------------------------------------------------------------------------------------------------
	//! The clerk checks the customer and range itself and answers with a reason.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_OpenAccount(RplId clerkId)
	{
		ELIFE_BankClerkComponent clerk = FindClerk(clerkId, "open account");
		if (clerk)
			clerk.RequestOpenAccount(GetPlayerId(), GetOwner(), this);
	}

	//------------------------------------------------------------------------------------------------
	//! The clerk checks the customer and range itself and answers with a reason.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_TransferAccounts(RplId clerkId)
	{
		ELIFE_BankClerkComponent clerk = FindClerk(clerkId, "transfer accounts");
		if (clerk)
			clerk.RequestTransferAccounts(GetPlayerId(), GetOwner(), this);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_TransferQuote(RplId clerkId, string fromNumber, string targetNumber, int amount)
	{
		ELIFE_BankClerkComponent clerk = FindClerk(clerkId, "transfer quote");
		if (clerk)
			clerk.RequestTransferQuote(GetPlayerId(), GetOwner(), this, fromNumber, targetNumber, amount);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_TransferConfirm(RplId clerkId, int quoteId)
	{
		ELIFE_BankClerkComponent clerk = FindClerk(clerkId, "transfer confirm");
		if (clerk)
			clerk.RequestTransferConfirm(GetPlayerId(), GetOwner(), this, quoteId);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Paper(RplId clerkId, int kind)
	{
		ELIFE_BankClerkComponent clerk = FindClerk(clerkId, "paper");
		if (clerk)
			clerk.RequestPaper(GetPlayerId(), GetOwner(), this, kind);
	}

	//------------------------------------------------------------------------------------------------
	//! Where the paper went: into the inventory, or onto the counter when there was no room.
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_PaperResult(RplId clerkId, int kind, int reason, bool onGround)
	{
		ELIFE_BankClerkDialogueController.OnPaperResult(reason);
		if (reason != ELIFE_EClerkReason.NONE)
			return;

		string title = WidgetManager.Translate(ELIFE_BankPaper.GetTitle(kind));
		if (onGround)
			SCR_HintManagerComponent.ShowCustomHint(WidgetManager.Translate("#ELIFE-Paper_Hint_Ground", title), title);
		else
			SCR_HintManagerComponent.ShowCustomHint(WidgetManager.Translate("#ELIFE-Paper_Hint_Inventory", title), title);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_TransferPurpose(RplId clerkId, int quoteId, int purpose)
	{
		ELIFE_BankClerkComponent clerk = FindClerk(clerkId, "transfer purpose");
		if (clerk)
			clerk.RequestTransferPurpose(GetPlayerId(), GetOwner(), this, quoteId, purpose);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_ClerkAccounts(RplId clerkId, int reason, string json)
	{
		ELIFE_TransferSlipMenu slip = ELIFE_TransferSlipMenu.GetOpen();
		if (slip)
			slip.OnClerkAccountsAnswer(reason, json);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_TransferQuote(RplId clerkId, int reason, string json)
	{
		ELIFE_TransferSlipMenu slip = ELIFE_TransferSlipMenu.GetOpen();
		if (slip)
			slip.OnTransferQuoteAnswer(reason, json);
	}

	//------------------------------------------------------------------------------------------------
	//! If the slip is gone (the player walked off), the transfer still happened, so a hint says so.
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_TransferResult(RplId clerkId, int reason, string amount, string fee, string balance)
	{
		ELIFE_TransferSlipMenu slip = ELIFE_TransferSlipMenu.GetOpen();
		if (slip)
			slip.OnTransferResultAnswer(reason, amount, fee, balance);
		else if (ELIFE_BankClerkDialogueController.OnTransferResult(reason, amount, fee, balance))
			return;
		else if (reason == ELIFE_EClerkReason.NONE)
			SCR_HintManagerComponent.ShowCustomHint(WidgetManager.Translate("#ELIFE-Clerk_TransferDone_Hint", amount), "#ELIFE-Clerk_TransferDone_Title");
		else if (reason == ELIFE_EClerkReason.UNKNOWN_OUTCOME)
			SCR_HintManagerComponent.ShowCustomHint("#ELIFE-Clerk_Reason_UnknownOutcome", "#ELIFE-Clerk_Title");
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_CustomerContext(RplId clerkId, int visitId, int accounts, bool openedThisVisit)
	{
		m_ContextClerkId = clerkId;
		m_iContextVisitId = visitId;
		m_eContextAccounts = accounts;
		m_bContextOpenedThisVisit = openedThisVisit;
		ELIFE_BankClerkDialogueController.OnCustomerContext(FindEntity(clerkId));
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_AccountOpened(RplId clerkId, bool opened, int reason, string accountNumber)
	{
		ELIFE_BankClerkDialogueController.OnAccountOpened(opened, reason, accountNumber);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_Grant(RplId clerkId, bool granted, int reason, int visitId)
	{
		IEntity clerkEntity = FindEntity(clerkId);
		if (!clerkEntity)
			return;

		ELIFE_BankClerkComponent clerk = ELIFE_BankClerkComponent.Cast(clerkEntity.FindComponent(ELIFE_BankClerkComponent));
		if (!clerk)
			return;

		if (granted)
		{
			m_iGrantedVisitId = visitId;
			if (!ELIFE_BankClerkDialogueController.StartVisit(clerk, this))
				RequestClose(clerkEntity);

			return;
		}

		string message = "#ELIFE-Clerk_Unavailable";
		if (reason == ELIFE_EClerkReason.BUSY)
			message = "#ELIFE-Clerk_Busy";
		else if (reason == ELIFE_EClerkReason.OUT_OF_RANGE)
			message = "#ELIFE-Clerk_OutOfRange";

		SCR_HintManagerComponent.ShowCustomHint(message, "#ELIFE-Clerk_Title");
	}

	//------------------------------------------------------------------------------------------------
	//! Server-side.
	protected void SayOutcome(RplId clerkId, int reason, ELIFE_EClerkLine successLine)
	{
		IEntity entity = FindEntity(clerkId);
		if (!entity)
			return;

		ELIFE_BankClerkComponent clerk = ELIFE_BankClerkComponent.Cast(entity.FindComponent(ELIFE_BankClerkComponent));
		if (clerk)
			clerk.SayOutcome(reason, successLine);
	}

	//------------------------------------------------------------------------------------------------
	protected RplId GetClerkId(notnull IEntity clerk)
	{
		RplComponent rpl = RplComponent.Cast(clerk.FindComponent(RplComponent));
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
	protected IEntity FindEntity(RplId clerkId)
	{
		RplComponent rpl = RplComponent.Cast(Replication.FindItem(clerkId));
		if (!rpl)
			return null;

		return rpl.GetEntity();
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_BankClerkComponent FindClerk(RplId clerkId, string request)
	{
		IEntity entity = FindEntity(clerkId);
		ELIFE_BankClerkComponent clerk;
		if (entity)
			clerk = ELIFE_BankClerkComponent.Cast(entity.FindComponent(ELIFE_BankClerkComponent));

		if (!clerk)
			Print(string.Format("ELIFE_BankClerkCharacterComponent | %1 rejected, not a clerk", request), LogLevel.WARNING);

		return clerk;
	}
}
