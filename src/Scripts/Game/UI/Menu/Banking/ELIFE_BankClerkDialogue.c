//------------------------------------------------------------------------------------------------
//! The bank clerk's part of the generic dialogue (ELIFE_DialogueMenu): his lines, actions, details and controller.
//! The tree is ELIFE_BankClerkComponent's dialogue config.

//------------------------------------------------------------------------------------------------
//! Only lines ELIFE_BankClerkComponent.IsAskableLine() allows are said; outcomes are the server's to say.
[BaseContainerProps(), SCR_BaseContainerCustomTitleEnum(ELIFE_EClerkLine, "m_eLine")]
class ELIFE_ClerkDialogueLine : ELIFE_DialogueLine
{
	[Attribute("0", UIWidgets.ComboBox, "The clerk's line", "", ParamEnumArray.FromEnum(ELIFE_EClerkLine))]
	ELIFE_EClerkLine m_eLine;

	//------------------------------------------------------------------------------------------------
	override void Speak(notnull ELIFE_DialogueController controller)
	{
		ELIFE_BankClerkDialogueController clerk = ELIFE_BankClerkDialogueController.Cast(controller);
		if (clerk)
			clerk.RequestSay(m_eLine);
	}
}

//------------------------------------------------------------------------------------------------
//! Opens the paper slip on top; the conversation continues when it closes.
[BaseContainerProps()]
class ELIFE_ClerkDialogueAction_TransferSlip : ELIFE_DialogueAction
{
	[Attribute("transfer_done", UIWidgets.EditBox, "Node after a booked transfer; its details get the receipt")]
	string m_sDoneNode;

	[Attribute("services_again", UIWidgets.EditBox, "Node after the slip is put away")]
	string m_sCancelNode;

	//------------------------------------------------------------------------------------------------
	override void Run(notnull ELIFE_DialogueMenu menu)
	{
		ELIFE_BankClerkDialogueController clerk = ELIFE_BankClerkDialogueController.Cast(menu.GetController());
		if (clerk)
			clerk.OpenTransferSlip(m_sDoneNode, m_sCancelNode);
	}
}

//------------------------------------------------------------------------------------------------
//! Asks the server for a personal account and waits for the answer.
[BaseContainerProps()]
class ELIFE_ClerkDialogueAction_OpenAccount : ELIFE_DialogueAction
{
	[Attribute("account_opened", UIWidgets.EditBox, "Node once the account is open; its details get the number")]
	string m_sOpenedNode;

	//------------------------------------------------------------------------------------------------
	override void Run(notnull ELIFE_DialogueMenu menu)
	{
		ELIFE_BankClerkDialogueController clerk = ELIFE_BankClerkDialogueController.Cast(menu.GetController());
		if (clerk)
			clerk.OpenAccount(m_sOpenedNode);
	}
}

//------------------------------------------------------------------------------------------------
//! This bank's booking fee terms.
[BaseContainerProps()]
class ELIFE_ClerkDialogueDetails_FeeTerms : ELIFE_DialogueDetails
{
	//------------------------------------------------------------------------------------------------
	override string GetText(notnull ELIFE_DialogueMenu menu)
	{
		ELIFE_BankClerkDialogueController clerk = ELIFE_BankClerkDialogueController.Cast(menu.GetController());
		if (!clerk || !clerk.GetClerk())
			return "";

		string terms = clerk.GetClerk().GetFeeTerms();
		if (terms == "")
			return "";

		return FormatLine("#ELIFE-Clerk_Label_BookingFee", terms);
	}
}

//------------------------------------------------------------------------------------------------
//! The account just opened; the node's data is its number.
[BaseContainerProps()]
class ELIFE_ClerkDialogueDetails_NewAccount : ELIFE_DialogueDetails
{
	//------------------------------------------------------------------------------------------------
	override string GetText(notnull ELIFE_DialogueMenu menu)
	{
		return FormatLine("#ELIFE-Clerk_Transfer_Number", ELIFE_PhoneBankingService.GroupIban(menu.GetNodeData())) + "\n" + WidgetManager.Translate("#ELIFE-Clerk_Opened_Closing");
	}

	//------------------------------------------------------------------------------------------------
	override void OnShown(notnull ELIFE_DialogueMenu menu)
	{
		ELIFE_BankClerkDialogueController.PlayPaper(menu);
	}
}

//------------------------------------------------------------------------------------------------
//! The transfer just booked; the node's data is a packed ELIFE_ClerkTransferReceipt.
[BaseContainerProps()]
class ELIFE_ClerkDialogueDetails_TransferReceipt : ELIFE_DialogueDetails
{
	//------------------------------------------------------------------------------------------------
	override string GetText(notnull ELIFE_DialogueMenu menu)
	{
		ELIFE_ClerkTransferReceipt receipt = new ELIFE_ClerkTransferReceipt();
		receipt.ExpandFromRAW(menu.GetNodeData());
		string target = ELIFE_PhoneBankingService.GroupIban(receipt.targetNumber);
		if (receipt.targetBankName != "")
			target += " (" + receipt.targetBankName + ")";

		string text = FormatLine("#ELIFE-Clerk_Transfer_To", target);
		text += "\n" + FormatLine("#ELIFE-Clerk_Transfer_Amount", receipt.amount);
		text += "\n" + FormatLine("#ELIFE-Clerk_Label_Fee", receipt.fee);
		text += "\n" + FormatLine("#ELIFE-Clerk_Label_NewBalance", receipt.balance);
		return text;
	}

	//------------------------------------------------------------------------------------------------
	override void OnShown(notnull ELIFE_DialogueMenu menu)
	{
		ELIFE_BankClerkDialogueController.PlayPaper(menu);
	}
}

//------------------------------------------------------------------------------------------------
//! The customer has a personal account at any bank, so a transfer can start from it. Unknown counts as yes:
//! the slip then says so itself.
[BaseContainerProps()]
class ELIFE_ClerkDialogueCondition_HasAccount : ELIFE_DialogueCondition
{
	//------------------------------------------------------------------------------------------------
	override protected bool IsMet(notnull ELIFE_DialogueMenu menu)
	{
		ELIFE_BankClerkDialogueController clerk = ELIFE_BankClerkDialogueController.Cast(menu.GetController());
		return !clerk || clerk.GetAccountStatus() != ELIFE_EClerkAccountStatus.NO_ACCOUNT;
	}
}

//------------------------------------------------------------------------------------------------
//! An account was opened at this visit; the server allows one per visit.
[BaseContainerProps()]
class ELIFE_ClerkDialogueCondition_OpenedThisVisit : ELIFE_DialogueCondition
{
	//------------------------------------------------------------------------------------------------
	override protected bool IsMet(notnull ELIFE_DialogueMenu menu)
	{
		ELIFE_BankClerkDialogueController clerk = ELIFE_BankClerkDialogueController.Cast(menu.GetController());
		return clerk && clerk.HasOpenedThisVisit();
	}
}

//------------------------------------------------------------------------------------------------
//! What the receipt shows after a transfer.
class ELIFE_ClerkTransferReceipt : JsonApiStruct
{
	string targetNumber;
	string targetBankName;
	//! The Bridge's display text.
	string amount;
	string fee;
	string balance;

	//------------------------------------------------------------------------------------------------
	void ELIFE_ClerkTransferReceipt()
	{
		RegV("targetNumber");
		RegV("targetBankName");
		RegV("amount");
		RegV("fee");
		RegV("balance");
	}
}

//------------------------------------------------------------------------------------------------
//! One visit at the counter. Ending the conversation, however it happens, says goodbye and frees the clerk.
class ELIFE_BankClerkDialogueController : ELIFE_DialogueController
{
	//! The greeting can be heard just before the conversation opens; a line this recent is still shown.
	protected static const int RECENT_LINE_MS = 5000;

	protected ELIFE_BankClerkComponent m_Clerk;
	protected ELIFE_BankClerkCharacterComponent m_Link;
	//! The conversation opens before the replicated customer id may have arrived, so a mismatch only counts once it matched.
	protected bool m_bSeenAsCustomer;
	protected bool m_bOpeningAccount;
	protected string m_sOpenedNode;
	protected bool m_bSlipOpen;
	//! From the server's lookup at the start of the visit; see ELIFE_BankClerkComponent.OnCustomerAccounts.
	protected ELIFE_EClerkAccountStatus m_eAccounts = ELIFE_EClerkAccountStatus.UNKNOWN;
	protected bool m_bOpenedThisVisit;
	protected string m_sSlipDoneNode;
	protected string m_sSlipCancelNode;

	//------------------------------------------------------------------------------------------------
	//! After the server's grant. False if nothing opened.
	static bool StartVisit(ELIFE_BankClerkComponent clerk, ELIFE_BankClerkCharacterComponent link)
	{
		if (!clerk || !link)
			return false;

		ELIFE_BankClerkDialogueController controller = new ELIFE_BankClerkDialogueController(clerk, link);
		return ELIFE_DialogueMenu.Open(controller);
	}

	//------------------------------------------------------------------------------------------------
	//! The open conversation if it is with a clerk.
	static ELIFE_BankClerkDialogueController GetActive()
	{
		ELIFE_DialogueMenu menu = ELIFE_DialogueMenu.GetOpen();
		if (!menu)
			return null;

		return ELIFE_BankClerkDialogueController.Cast(menu.GetController());
	}

	//------------------------------------------------------------------------------------------------
	//! A server answer. With no conversation open (the customer walked off meanwhile), a hint still tells them.
	static void OnAccountOpened(bool opened, int reason, string accountNumber)
	{
		ELIFE_BankClerkDialogueController active = GetActive();
		if (active && active.m_bOpeningAccount)
		{
			active.OnAccountOpenedAnswer(opened, reason, accountNumber);
			return;
		}

		if (opened)
			SCR_HintManagerComponent.ShowCustomHint(WidgetManager.Translate("#ELIFE-Clerk_Opened_Hint", ELIFE_PhoneBankingService.GroupIban(accountNumber)), "#ELIFE-Clerk_Opened_Title");
	}

	//------------------------------------------------------------------------------------------------
	//! The server's lookup about the customer arrived; the replies adapt.
	static void OnCustomerContext(IEntity clerkEntity)
	{
		ELIFE_BankClerkDialogueController active = GetActive();
		if (active && clerkEntity && active.GetSpeaker() == clerkEntity)
			active.ReadContext();
	}

	//------------------------------------------------------------------------------------------------
	//! Paper changing hands at his desk, heard only by the customer.
	static void PlayPaper(notnull ELIFE_DialogueMenu menu)
	{
		ELIFE_BankClerkDialogueController clerk = ELIFE_BankClerkDialogueController.Cast(menu.GetController());
		if (clerk && clerk.GetClerk())
			clerk.GetClerk().PlaySfx(ELIFE_BankClerkComponent.SFX_PAPER);
	}

	//------------------------------------------------------------------------------------------------
	//! The sentence for a refusal from the server.
	static string GetReasonText(int reason)
	{
		switch (reason)
		{
			case ELIFE_EClerkReason.OUT_OF_RANGE:
				return "#ELIFE-Clerk_OutOfRange";
			case ELIFE_EClerkReason.INVALID_AMOUNT:
				return "#ELIFE-Clerk_Reason_InvalidAmount";
			case ELIFE_EClerkReason.NOT_ENOUGH_MONEY:
				return "#ELIFE-Clerk_Reason_NotEnoughMoney";
			case ELIFE_EClerkReason.INVALID_TARGET:
				return "#ELIFE-Clerk_Reason_InvalidTarget";
			case ELIFE_EClerkReason.TARGET_NOT_FOUND:
				return "#ELIFE-Clerk_Reason_TargetNotFound";
			case ELIFE_EClerkReason.SAME_ACCOUNT:
				return "#ELIFE-Clerk_Reason_SameAccount";
			case ELIFE_EClerkReason.QUOTE_EXPIRED:
				return "#ELIFE-Clerk_Reason_QuoteExpired";
			case ELIFE_EClerkReason.ACCOUNT_BUSY:
				return "#ELIFE-Clerk_Reason_AccountBusy";
			case ELIFE_EClerkReason.FAILED:
				return "#ELIFE-Clerk_Reason_TransferFailed";
			case ELIFE_EClerkReason.UNKNOWN_OUTCOME:
				return "#ELIFE-Clerk_Reason_UnknownOutcome";
		}

		return "#ELIFE-Clerk_Unavailable";
	}

	//------------------------------------------------------------------------------------------------
	void ELIFE_BankClerkDialogueController(notnull ELIFE_BankClerkComponent clerk, notnull ELIFE_BankClerkCharacterComponent link)
	{
		m_Clerk = clerk;
		m_Link = link;
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_BankClerkComponent GetClerk()
	{
		return m_Clerk;
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_EClerkAccountStatus GetAccountStatus()
	{
		return m_eAccounts;
	}

	//------------------------------------------------------------------------------------------------
	bool HasOpenedThisVisit()
	{
		return m_bOpenedThisVisit;
	}

	//------------------------------------------------------------------------------------------------
	protected void ReadContext()
	{
		ELIFE_EClerkAccountStatus accounts;
		bool opened;
		if (!m_Link.GetCustomerContext(m_Clerk.GetOwner(), accounts, opened))
			return;

		m_eAccounts = accounts;
		m_bOpenedThisVisit = opened;
		if (m_Menu)
			m_Menu.UpdateReplies();
	}

	//------------------------------------------------------------------------------------------------
	override ResourceName GetConfig()
	{
		return m_Clerk.GetDialogueConfig();
	}

	//------------------------------------------------------------------------------------------------
	override IEntity GetSpeaker()
	{
		if (!m_Clerk)
			return null;

		return m_Clerk.GetOwner();
	}

	//------------------------------------------------------------------------------------------------
	//! The bank's name is replicated and may arrive after the grant; until then he is just "Bank clerk".
	override string GetSpeakerName()
	{
		if (!m_Clerk || m_Clerk.GetBankName() == "")
			return "#ELIFE-Clerk_Title";

		return WidgetManager.Translate("#ELIFE-ClerkDialogue_Speaker", m_Clerk.GetBankName());
	}

	//------------------------------------------------------------------------------------------------
	//! The server releases the clerk on its own when the customer walks off, dies or idles; the conversation follows.
	override bool IsAvailable()
	{
		if (!m_Clerk || !m_Link || !m_Clerk.IsUsableBy(SCR_PlayerController.GetLocalControlledEntity()))
			return false;

		if (m_Clerk.GetCustomerPlayerId() == SCR_PlayerController.GetLocalPlayerId())
			m_bSeenAsCustomer = true;
		else if (m_bSeenAsCustomer)
			return false;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(notnull ELIFE_DialogueMenu menu)
	{
		super.OnStart(menu);

		int voice, line, variant;
		if (m_Clerk.GetRecentLine(RECENT_LINE_MS, voice, line, variant))
			menu.SetSubtitle(ELIFE_BankClerkComponent.GetLineText(voice, line, variant));

		//! Before the start node's replies are built, so it opens with the right ones.
		ELIFE_EClerkAccountStatus accounts;
		bool opened;
		if (m_Link.GetCustomerContext(m_Clerk.GetOwner(), accounts, opened))
		{
			m_eAccounts = accounts;
			m_bOpenedThisVisit = opened;
		}
	}

	//------------------------------------------------------------------------------------------------
	override void OnEnd()
	{
		if (m_Link && m_Clerk)
			m_Link.RequestClose(m_Clerk.GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	void RequestSay(ELIFE_EClerkLine line)
	{
		if (m_Link && m_Clerk)
			m_Link.RequestSay(m_Clerk.GetOwner(), line);
	}

	//------------------------------------------------------------------------------------------------
	void OpenAccount(string openedNode)
	{
		if (!m_Menu || m_bOpeningAccount)
			return;

		m_bOpeningAccount = true;
		m_sOpenedNode = openedNode;
		m_Menu.SetWaiting(true, "#ELIFE-Clerk_Working");
		m_Link.RequestOpenAccount(m_Clerk.GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	protected void OnAccountOpenedAnswer(bool opened, int reason, string accountNumber)
	{
		m_bOpeningAccount = false;
		if (!m_Menu)
			return;

		if (opened)
		{
			m_Menu.ShowNode(m_sOpenedNode, accountNumber);
			return;
		}

		//! He explains a refusal aloud; a reason without a line of his (out of range) is written instead.
		string notice;
		if (ELIFE_BankClerkComponent.GetReasonLine(reason) == ELIFE_EClerkLine.NONE)
			notice = GetReasonText(reason);

		m_Menu.SetWaiting(false, notice);
	}

	//------------------------------------------------------------------------------------------------
	void OpenTransferSlip(string doneNode, string cancelNode)
	{
		if (!m_Menu || m_bSlipOpen)
			return;

		m_bSlipOpen = ELIFE_TransferSlipMenu.Open(m_Clerk, m_Link);
		if (!m_bSlipOpen)
			return;

		m_sSlipDoneNode = doneNode;
		m_sSlipCancelNode = cancelNode;
		m_Menu.Suspend();
	}

	//------------------------------------------------------------------------------------------------
	//! The slip hands back: with a packed receipt after a booked transfer, empty when put away.
	void OnSlipClosed(string receipt)
	{
		m_bSlipOpen = false;
		if (!m_Menu)
			return;

		m_Menu.Resume();
		if (receipt != "")
			m_Menu.ShowNode(m_sSlipDoneNode, receipt);
		else
			m_Menu.ShowNode(m_sSlipCancelNode);
	}
}
