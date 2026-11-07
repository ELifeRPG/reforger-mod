//------------------------------------------------------------------------------------------------
enum ELIFE_EClerkDialog
{
	NONE,
	SERVICES,
	OPEN_ACCOUNT,
	ACCOUNT_OPENED,
	TRANSFER,
	TRANSFER_DONE
}

//------------------------------------------------------------------------------------------------
//! What the receipt shows after a transfer.
class ELIFE_ClerkTransferReceipt : JsonApiStruct
{
	string targetNumber;
	string targetBankName;
	int amountCents;
	int feeCents;
	int balanceCents;

	//------------------------------------------------------------------------------------------------
	void ELIFE_ClerkTransferReceipt()
	{
		RegV("targetNumber");
		RegV("targetBankName");
		RegV("amountCents");
		RegV("feeCents");
		RegV("balanceCents");
	}
}

//------------------------------------------------------------------------------------------------
//! One visit at the counter, shown as a chain of vanilla dialogs. Moving on to the next one keeps the visit; closing any other way ends it and frees the clerk.
class ELIFE_BankClerkDialogBase : SCR_ConfigurableDialogUi
{
	protected static const ResourceName PRESETS = "{E2F1AA299C290781}Configs/Banking/ELIFE_ClerkDialogs.conf";

	//! The dialog that answers from the server go to.
	protected static ELIFE_BankClerkDialogBase s_Current;

	protected ELIFE_BankClerkComponent m_Clerk;
	protected ELIFE_BankClerkCharacterComponent m_Link;
	protected string m_sData;
	//! The grant can arrive before the replicated customer id does, so a mismatch only counts once the id has matched.
	protected bool m_bSeenAsCustomer;

	protected ELIFE_EClerkDialog m_eNext = ELIFE_EClerkDialog.NONE;

	//------------------------------------------------------------------------------------------------
	//! False if nothing opened.
	static bool Open(ELIFE_EClerkDialog dialog, ELIFE_BankClerkComponent clerk, ELIFE_BankClerkCharacterComponent link, string data = "")
	{
		if (!clerk || !link)
			return false;

		ELIFE_BankClerkDialogBase instance;
		string tag;
		switch (dialog)
		{
			case ELIFE_EClerkDialog.SERVICES:
				instance = new ELIFE_BankClerkServiceDialog();
				tag = "ELIFE_CLERK_SERVICES";
				break;
			case ELIFE_EClerkDialog.OPEN_ACCOUNT:
				instance = new ELIFE_BankClerkOpenAccountDialog();
				tag = "ELIFE_CLERK_OPEN_ACCOUNT";
				break;
			case ELIFE_EClerkDialog.ACCOUNT_OPENED:
				instance = new ELIFE_BankClerkAccountOpenedDialog();
				tag = "ELIFE_CLERK_ACCOUNT_OPENED";
				break;
			case ELIFE_EClerkDialog.TRANSFER:
				//! The paper slip, its own menu rather than a dialog.
				return ELIFE_TransferSlipMenu.Open(clerk, link);
			case ELIFE_EClerkDialog.TRANSFER_DONE:
				instance = new ELIFE_BankClerkTransferDoneDialog();
				tag = "ELIFE_CLERK_TRANSFER_DONE";
				break;
		}

		if (!instance)
			return false;

		//! Set before CreateFromPreset(), which already calls OnMenuOpen().
		instance.m_Clerk = clerk;
		instance.m_Link = link;
		instance.m_sData = data;
		s_Current = ELIFE_BankClerkDialogBase.Cast(SCR_ConfigurableDialogUi.CreateFromPreset(PRESETS, tag, instance));
		return s_Current != null;
	}

	//------------------------------------------------------------------------------------------------
	//! A server answer for whichever clerk dialog is open. With none open (the customer went back meanwhile), a hint still tells them.
	static void OnAccountOpened(bool opened, int reason, string accountNumber)
	{
		if (s_Current)
			s_Current.OnAccountOpenedAnswer(opened, reason, accountNumber);
		else if (opened)
			ShowOpenedHint(accountNumber);
	}

	//------------------------------------------------------------------------------------------------
	static void OnClerkAccounts(int reason, string json)
	{
		ELIFE_TransferSlipMenu slip = ELIFE_TransferSlipMenu.GetOpen();
		if (slip)
			slip.OnClerkAccountsAnswer(reason, json);
	}

	//------------------------------------------------------------------------------------------------
	static void OnTransferQuote(int reason, string json)
	{
		ELIFE_TransferSlipMenu slip = ELIFE_TransferSlipMenu.GetOpen();
		if (slip)
			slip.OnTransferQuoteAnswer(reason, json);
	}

	//------------------------------------------------------------------------------------------------
	//! If the slip is gone (the player walked off), the transfer still happened, so a hint says so.
	static void OnTransferResult(int reason, int amountCents, int feeCents, int balanceCents)
	{
		ELIFE_TransferSlipMenu slip = ELIFE_TransferSlipMenu.GetOpen();
		if (slip)
			slip.OnTransferResultAnswer(reason, amountCents, feeCents, balanceCents);
		else if (reason == ELIFE_EClerkReason.NONE)
			SCR_HintManagerComponent.ShowCustomHint(WidgetManager.Translate("#ELIFE-Clerk_TransferDone_Hint", ELIFE_PhoneBankingService.FormatMoney(amountCents)), "#ELIFE-Clerk_TransferDone_Title");
		else if (reason == ELIFE_EClerkReason.UNKNOWN_OUTCOME)
			SCR_HintManagerComponent.ShowCustomHint("#ELIFE-Clerk_Reason_UnknownOutcome", "#ELIFE-Clerk_Title");
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
	protected static void ShowOpenedHint(string accountNumber)
	{
		SCR_HintManagerComponent.ShowCustomHint(WidgetManager.Translate("#ELIFE-Clerk_Opened_Hint", ELIFE_PhoneBankingService.GroupIban(accountNumber)), "#ELIFE-Clerk_Opened_Title");
	}

	//------------------------------------------------------------------------------------------------
	protected void OnAccountOpenedAnswer(bool opened, int reason, string accountNumber)
	{
		if (opened)
			ShowOpenedHint(accountNumber);
	}

	//------------------------------------------------------------------------------------------------
	//! The next dialog opens on top before this one closes, so the backdrop never drops out between them.
	protected void GoTo(ELIFE_EClerkDialog next, string data = "")
	{
		//! If the next step fails to open, closing ends the visit so the clerk isn't left reserved.
		m_eNext = next;
		if (!Open(next, m_Clerk, m_Link, data))
			m_eNext = ELIFE_EClerkDialog.NONE;

		Close();
	}

	//------------------------------------------------------------------------------------------------
	//! The server releases the clerk on its own when the customer walks off, dies or idles; the dialog follows.
	override void OnMenuUpdate(float tDelta)
	{
		super.OnMenuUpdate(tDelta);

		if (m_bIsClosing)
			return;

		IEntity character = SCR_PlayerController.GetLocalControlledEntity();
		if (!m_Clerk || !m_Clerk.IsUsableBy(character))
		{
			Close();
			return;
		}

		bool isCustomer = m_Clerk.GetCustomerPlayerId() == SCR_PlayerController.GetLocalPlayerId();
		if (isCustomer)
			m_bSeenAsCustomer = true;
		else if (m_bSeenAsCustomer)
			Close();
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuClose()
	{
		super.OnMenuClose();

		if (s_Current == this)
			s_Current = null;

		//! Handing over to the next dialog keeps the visit.
		if (!m_Link || !m_Clerk || m_eNext != ELIFE_EClerkDialog.NONE)
			return;

		m_Link.RequestClose(m_Clerk.GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	//! "Label: value", one figure per line under what the clerk says.
	protected static string Line(string labelKey, string value)
	{
		return "\n" + WidgetManager.Translate(labelKey) + ": " + value;
	}

	//------------------------------------------------------------------------------------------------
	//! The clerk's sentence, a blank line, then the figures. Line breaks come from here: the string table can't hold them.
	protected void SetStatement(string sentence, string lines, string closing = "")
	{
		string text = WidgetManager.Translate(sentence) + "\n" + lines;
		if (closing != "")
			text += "\n\n" + WidgetManager.Translate(closing);

		SetMessage(text);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetButtonEnabled(string tag, bool enabled)
	{
		SCR_InputButtonComponent button = FindButton(tag);
		if (button)
			button.SetEnabled(enabled);
	}
}

//------------------------------------------------------------------------------------------------
//! The clerk's greeting and the services on offer.
class ELIFE_BankClerkServiceDialog : ELIFE_BankClerkDialogBase
{
	protected static const string BUTTON_TRANSFER = "transfer";
	protected static const string BUTTON_OPEN_ACCOUNT = "open_account";

	protected bool m_bTitleSet;

	//------------------------------------------------------------------------------------------------
	override protected void OnMenuOpen(SCR_ConfigurableDialogUiPreset preset)
	{
		super.OnMenuOpen(preset);

		UpdateTitle();
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuUpdate(float tDelta)
	{
		super.OnMenuUpdate(tDelta);

		UpdateTitle();
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnButtonPressed(SCR_InputButtonComponent button)
	{
		super.OnButtonPressed(button);

		string tag = GetButtonTag(button);
		if (tag == BUTTON_OPEN_ACCOUNT)
			GoTo(ELIFE_EClerkDialog.OPEN_ACCOUNT);
		else if (tag == BUTTON_TRANSFER)
			GoTo(ELIFE_EClerkDialog.TRANSFER);
	}

	//------------------------------------------------------------------------------------------------
	//! The bank's name is replicated and may arrive after the grant; until then the preset's generic title stays.
	protected void UpdateTitle()
	{
		if (m_bTitleSet || !m_Clerk)
			return;

		string bankName = m_Clerk.GetBankName();
		if (bankName == "")
			return;

		SetTitle(bankName);
		m_bTitleSet = true;
	}
}

//------------------------------------------------------------------------------------------------
//! The terms, then the request. Back returns to the services.
class ELIFE_BankClerkOpenAccountDialog : ELIFE_BankClerkDialogBase
{
	protected bool m_bWaiting;

	//------------------------------------------------------------------------------------------------
	override protected void OnMenuOpen(SCR_ConfigurableDialogUiPreset preset)
	{
		super.OnMenuOpen(preset);

		float percent = Math.Round(m_Clerk.GetFeeMultiplier() * 10000) / 100;
		string fee = ELIFE_PhoneBankingService.FormatMoney(m_Clerk.GetFeeBaseCents());
		string sentence = WidgetManager.Translate(preset.m_sMessage, m_Clerk.GetBankName());
		SetStatement(sentence, Line("#ELIFE-Clerk_Label_BookingFee", WidgetManager.Translate("#ELIFE-Clerk_FeeTerms", fee, percent.ToString())));
	}

	//------------------------------------------------------------------------------------------------
	//! Stays open while the server works; the answer moves on or explains.
	override protected void OnConfirm()
	{
		if (m_bWaiting)
			return;

		m_bWaiting = true;
		SetButtonEnabled(BUTTON_CONFIRM, false);
		SetMessage("#ELIFE-Clerk_Working");
		m_Link.RequestOpenAccount(m_Clerk.GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnCancel()
	{
		GoTo(ELIFE_EClerkDialog.SERVICES);
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnAccountOpenedAnswer(bool opened, int reason, string accountNumber)
	{
		m_bWaiting = false;

		if (opened)
		{
			GoTo(ELIFE_EClerkDialog.ACCOUNT_OPENED, accountNumber);
			return;
		}

		//! One per visit stays refused until the next visit, so the button stays off.
		if (reason == ELIFE_EClerkReason.ALREADY_OPENED)
		{
			SetMessage("#ELIFE-Clerk_AlreadyOpened");
			return;
		}

		if (reason == ELIFE_EClerkReason.OUT_OF_RANGE)
			SetMessage("#ELIFE-Clerk_OutOfRange");
		else
			SetMessage("#ELIFE-Clerk_OpenFailed");

		SetButtonEnabled(BUTTON_CONFIRM, true);
	}
}

//------------------------------------------------------------------------------------------------
//! The new number, so the player can note it. Done returns to the services.
class ELIFE_BankClerkAccountOpenedDialog : ELIFE_BankClerkDialogBase
{
	//------------------------------------------------------------------------------------------------
	override protected void OnMenuOpen(SCR_ConfigurableDialogUiPreset preset)
	{
		super.OnMenuOpen(preset);

		SetStatement(preset.m_sMessage, Line("#ELIFE-Clerk_Transfer_Number", ELIFE_PhoneBankingService.GroupIban(m_sData)), "#ELIFE-Clerk_Opened_Closing");
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnCancel()
	{
		GoTo(ELIFE_EClerkDialog.SERVICES);
	}
}

//------------------------------------------------------------------------------------------------
//! The receipt. Done returns to the services.
class ELIFE_BankClerkTransferDoneDialog : ELIFE_BankClerkDialogBase
{
	//------------------------------------------------------------------------------------------------
	override protected void OnMenuOpen(SCR_ConfigurableDialogUiPreset preset)
	{
		super.OnMenuOpen(preset);

		ELIFE_ClerkTransferReceipt receipt = new ELIFE_ClerkTransferReceipt();
		receipt.ExpandFromRAW(m_sData);
		string target = ELIFE_PhoneBankingService.GroupIban(receipt.targetNumber);
		if (receipt.targetBankName != "")
			target += " (" + receipt.targetBankName + ")";

		string lines = Line("#ELIFE-Clerk_Transfer_To", target);
		lines += Line("#ELIFE-Clerk_Transfer_Amount", ELIFE_PhoneBankingService.FormatMoney(receipt.amountCents));
		lines += Line("#ELIFE-Clerk_Label_Fee", ELIFE_PhoneBankingService.FormatMoney(receipt.feeCents));
		lines += Line("#ELIFE-Clerk_Label_NewBalance", ELIFE_PhoneBankingService.FormatMoney(receipt.balanceCents));
		SetStatement(preset.m_sMessage, lines);
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnCancel()
	{
		GoTo(ELIFE_EClerkDialog.SERVICES);
	}
}
