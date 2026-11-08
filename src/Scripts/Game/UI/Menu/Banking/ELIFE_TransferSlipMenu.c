//------------------------------------------------------------------------------------------------
//! Forwards clicks and hovers on the slip's buttons to the menu, by widget name.
class ELIFE_TransferSlipClickHandler : ScriptedWidgetEventHandler
{
	protected ELIFE_TransferSlipMenu m_Menu;

	//------------------------------------------------------------------------------------------------
	void ELIFE_TransferSlipClickHandler(ELIFE_TransferSlipMenu menu)
	{
		m_Menu = menu;
	}

	//------------------------------------------------------------------------------------------------
	override bool OnClick(Widget w, int x, int y, int button)
	{
		if (m_Menu && button == 0)
			m_Menu.OnWidgetClicked(w.GetName());

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool OnMouseEnter(Widget w, int x, int y)
	{
		if (m_Menu)
			m_Menu.OnWidgetHovered(w.GetName(), true);

		return false;
	}

	//------------------------------------------------------------------------------------------------
	override bool OnMouseLeave(Widget w, Widget enterW, int x, int y)
	{
		if (m_Menu)
			m_Menu.OnWidgetHovered(w.GetName(), false);

		return false;
	}
}

//------------------------------------------------------------------------------------------------
enum ELIFE_ESlipRow
{
	FROM,
	TO,
	NUMBER,
	AMOUNT,
	ACTIONS
}

//------------------------------------------------------------------------------------------------
//! The transfer at the counter as a paper slip. Mouse and keyboard click and type; a controller moves over the rows and rolls the printed boxes.
//! It is one step of the clerk visit: it hands over to the clerk's dialogs and back without ending the visit.
class ELIFE_TransferSlipMenu : ChimeraMenuBase
{
	protected static const ResourceName BOX_LAYOUT = "{5A2C97FF6C6F6A3E}UI/layouts/Menus/Banking/TransferSlipBox.layout";

	//! The account number: EL printed, two check digits, four letters of the bank code, ten digits.
	protected static const int NUMBER_FIXED = 2;
	protected static const int NUMBER_LETTERS_START = 4;
	protected static const int NUMBER_LETTERS_END = 8;
	//! Enough digits for the booking limit.
	protected static const int AMOUNT_BOXES = 6;
	//! Gaps between groups: fours for the account number as it's printed, threes for the amount like thousands.
	protected static const float GROUP_GAP = 14;
	protected static const int NUMBER_GROUP = 4;
	protected static const int AMOUNT_GROUP = 3;
	protected static const int TYPING_END_GRACE_MS = 200;

	protected static const string DIGITS = "0123456789";
	protected static const string LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

	protected static ELIFE_TransferSlipMenu s_Open;

	protected ELIFE_BankClerkComponent m_Clerk;
	protected ELIFE_BankClerkCharacterComponent m_Link;

	protected ref ELIFE_TransferSlipClickHandler m_ClickHandler;
	protected ref array<Widget> m_aRowFocus = {};
	protected ref array<Widget> m_aRows = {};
	protected TextWidget m_wFromValue;
	protected TextWidget m_wToValue;
	protected TextWidget m_wToTag;
	protected TextWidget m_wSummary;
	protected TextWidget m_wNotice;
	protected TextWidget m_wHint;
	protected EditBoxWidget m_wNumberEdit;
	protected EditBoxWidget m_wAmountEdit;
	protected Widget m_wSubmitFill;
	protected TextWidget m_wSubmitText;
	protected Widget m_wCancelFill;
	protected TextWidget m_wCancelText;

	protected ref array<TextWidget> m_aNumberChars = {};
	protected ref array<Widget> m_aNumberFrames = {};
	protected ref array<Widget> m_aNumberFills = {};
	protected ref array<TextWidget> m_aAmountChars = {};
	protected ref array<Widget> m_aAmountFrames = {};
	protected ref array<Widget> m_aAmountFills = {};

	//! One character per box; "" is an empty box.
	protected ref array<string> m_aNumber = {};
	protected ref array<string> m_aAmount = {};

	protected ref ELIFE_ClerkAccountList m_Accounts;
	protected int m_iFrom;
	//! The target choices: the player's other accounts, recent recipients, then "" for a typed number.
	protected ref array<string> m_aTargets = {};
	protected ref array<bool> m_aTargetIsOwn = {};
	//! The quote the slip confirmed by itself: signing the slip is the approval.
	protected ref ELIFE_ClerkTransferQuote m_Quote;
	protected int m_iTarget;

	protected ELIFE_ESlipRow m_eRow = ELIFE_ESlipRow.FROM;
	//! 0 is Sign and hand over, 1 is Cancel.
	protected int m_iAction;
	//! A controller is rolling the boxes of the focused row.
	protected bool m_bEditing;
	protected int m_iCursor;

	protected bool m_bLoaded;
	protected bool m_bWaiting;
	protected bool m_bHandingOver;
	protected bool m_bIsClosing;
	//! The menu opens before the replicated customer id may have arrived, so a mismatch only counts once it matched.
	protected bool m_bSeenAsCustomer;
	protected string m_sNotice;
	//! The button under the cursor, lit at once; the row highlight is the controller's.
	protected string m_sHovered;
	protected bool m_bWasGamepad;

	//! Up, down, left, right. The actions repeat while held, so every rising edge is a step.
	protected ref array<bool> m_aPadHeld = {false, false, false, false};
	protected ref array<string> m_aPadActions = {"ELIFE_PadUp", "ELIFE_PadDown", "ELIFE_PadLeft", "ELIFE_PadRight"};
	protected bool m_bSelectHeld;
	protected bool m_bClearHeld;
	protected bool m_bPolling;
	//! Esc that ends typing in a box row must not also close the slip.
	protected int m_iTypingEndTick;
	protected bool m_bWasTyping;
	protected string m_sShownHint;

	//------------------------------------------------------------------------------------------------
	static bool Open(ELIFE_BankClerkComponent clerk, ELIFE_BankClerkCharacterComponent link)
	{
		MenuManager menuManager = GetGame().GetMenuManager();
		if (!menuManager || !clerk || !link)
			return false;

		ELIFE_TransferSlipMenu menu = ELIFE_TransferSlipMenu.Cast(menuManager.OpenMenu(ChimeraMenuPreset.ELIFE_TransferSlipMenu));
		if (!menu)
			return false;

		menu.Setup(clerk, link);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	static ELIFE_TransferSlipMenu GetOpen()
	{
		return s_Open;
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuOpen()
	{
		super.OnMenuOpen();

		s_Open = this;
		Widget root = GetRootWidget();
		m_ClickHandler = new ELIFE_TransferSlipClickHandler(this);

		array<string> rowNames = {"RowFrom", "RowTo", "RowNumber", "RowAmount", "Actions"};
		foreach (string rowName : rowNames)
		{
			m_aRows.Insert(root.FindAnyWidget(rowName));
			m_aRowFocus.Insert(root.FindAnyWidget(rowName + "Focus"));
		}

		array<string> buttonNames = {"FromPrev", "FromNext", "ToPrev", "ToNext", "NumberField", "AmountField", "Submit", "Cancel"};
		foreach (string buttonName : buttonNames)
		{
			Widget button = root.FindAnyWidget(buttonName);
			if (button)
				button.AddHandler(m_ClickHandler);
		}

		m_wFromValue = TextWidget.Cast(root.FindAnyWidget("FromValue"));
		m_wToValue = TextWidget.Cast(root.FindAnyWidget("ToValue"));
		m_wToTag = TextWidget.Cast(root.FindAnyWidget("ToTag"));
		m_wSummary = TextWidget.Cast(root.FindAnyWidget("Summary"));
		m_wNotice = TextWidget.Cast(root.FindAnyWidget("Notice"));
		m_wHint = TextWidget.Cast(root.FindAnyWidget("Hint"));
		m_wNumberEdit = EditBoxWidget.Cast(root.FindAnyWidget("NumberEdit"));
		m_wAmountEdit = EditBoxWidget.Cast(root.FindAnyWidget("AmountEdit"));
		m_wSubmitFill = root.FindAnyWidget("SubmitFill");
		m_wSubmitText = TextWidget.Cast(root.FindAnyWidget("SubmitText"));
		m_wCancelFill = root.FindAnyWidget("CancelFill");
		m_wCancelText = TextWidget.Cast(root.FindAnyWidget("CancelText"));

		ApplyStyle(root);
		SetTypingAllowed(false);
		BuildBoxes(root.FindAnyWidget("NumberBoxes"), ELIFE_BankAccountNumber.LENGTH, NUMBER_GROUP, m_aNumber, m_aNumberChars, m_aNumberFrames, m_aNumberFills);
		BuildBoxes(root.FindAnyWidget("AmountBoxes"), AMOUNT_BOXES, AMOUNT_GROUP, m_aAmount, m_aAmountChars, m_aAmountFrames, m_aAmountFills);
		m_aNumber[0] = ELIFE_BankAccountNumber.COUNTRY.Get(0);
		m_aNumber[1] = ELIFE_BankAccountNumber.COUNTRY.Get(1);

		InputManager inputManager = GetGame().GetInputManager();
		if (inputManager)
			inputManager.AddActionListener("MenuBack", EActionTrigger.DOWN, OnBackAction);

		m_sNotice = "#ELIFE-Clerk_Working";
		Refresh();
	}

	//------------------------------------------------------------------------------------------------
	protected void Setup(ELIFE_BankClerkComponent clerk, ELIFE_BankClerkCharacterComponent link)
	{
		m_Clerk = clerk;
		m_Link = link;

		SetText("BankName", m_Clerk.GetBankName());
		m_Link.RequestTransferAccounts(m_Clerk.GetOwner());
		Refresh();
	}

	//------------------------------------------------------------------------------------------------
	//! Paper, ink and the branch's accent, from the shared palette. The layout's own colours are only the editor preview.
	protected void ApplyStyle(notnull Widget root)
	{
		Color ink = Ink();
		ELIFE_PhoneStyle.SetColorOf(root, "PaperFill", ELIFE_PhoneStyle.Srgb(0.929, 0.902, 0.827));
		ELIFE_PhoneStyle.SetColorOf(root, "PaperShadow", new Color(0, 0, 0, 0.35));
		ELIFE_PhoneStyle.SetColorOf(root, "Dim", new Color(0, 0, 0, 0.6));

		array<string> inked = {"Title", "BankName", "RuleTop", "RuleMid", "Intro", "FromValue", "ToValue", "FromPrevText", "FromNextText", "ToPrevText", "ToNextText", "AmountUnit", "Summary", "SignatureLabel", "SignatureRule", "Hint"};
		foreach (string name : inked)
			ELIFE_PhoneStyle.SetColorOf(root, name, ink);

		array<string> labels = {"FromLabel", "ToLabel", "NumberLabel", "AmountLabel", "ToTag"};
		foreach (string name : labels)
			ELIFE_PhoneStyle.SetColorOf(root, name, Faint());

		ELIFE_PhoneStyle.SetColorOf(root, "Notice", ELIFE_PhoneStyle.Srgb(0.64, 0.23, 0.18));

		SetText("Title", "#ELIFE-Slip_Title");
		SetText("Intro", "#ELIFE-Clerk_Transfer_Message");
		SetText("FromLabel", "#ELIFE-Clerk_Transfer_From");
		SetText("ToLabel", "#ELIFE-Clerk_Transfer_To");
		SetText("NumberLabel", "#ELIFE-Clerk_Transfer_Number");
		SetText("AmountLabel", "#ELIFE-Clerk_Transfer_Amount");
		SetText("SubmitText", "#ELIFE-Slip_Submit");
		SetText("CancelText", "#ELIFE-Slip_Cancel");
		SetText("SignatureLabel", "#ELIFE-Slip_Signature");

		array<string> wrapped = {"Intro", "Summary", "Notice", "Hint"};
		foreach (string name : wrapped)
		{
			TextWidget text = TextWidget.Cast(root.FindAnyWidget(name));
			if (text)
				text.SetTextWrapping(true);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildBoxes(Widget parent, int count, int groupSize, notnull array<string> chars, notnull array<TextWidget> texts, notnull array<Widget> frames, notnull array<Widget> fills)
	{
		for (int i = 0; i < count; i++)
		{
			chars.Insert("");
			if (!parent)
				continue;

			Widget box = GetGame().GetWorkspace().CreateWidgets(BOX_LAYOUT, parent);
			if (!box)
				continue;

			if (i > 0 && i % groupSize == 0)
				LayoutSlot.SetPadding(box, GROUP_GAP, 0, 4, 0);

			texts.Insert(TextWidget.Cast(box.FindAnyWidget("Char")));
			frames.Insert(box.FindAnyWidget("Frame"));
			fills.Insert(box.FindAnyWidget("Fill"));
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static Color Ink()
	{
		return ELIFE_PhoneStyle.Srgb(0.118, 0.141, 0.2);
	}

	//------------------------------------------------------------------------------------------------
	protected static Color Faint()
	{
		return ELIFE_PhoneStyle.Srgb(0.118, 0.141, 0.2, 0.6);
	}

	//------------------------------------------------------------------------------------------------
	//! The clerk's branch accent, deep enough to read on paper.
	protected static Color Accent(float alpha = 1)
	{
		Color accent = ELIFE_PhoneStyle.AccentDeepBank();
		return new Color(accent.R(), accent.G(), accent.B(), alpha);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetText(string name, string text)
	{
		TextWidget widget = TextWidget.Cast(GetRootWidget().FindAnyWidget(name));
		if (widget)
			widget.SetText(text);
	}

	//------------------------------------------------------------------------------------------------
	//! The answers from the server.
	void OnClerkAccountsAnswer(int reason, string json)
	{
		if (reason != ELIFE_EClerkReason.NONE)
		{
			m_sNotice = ELIFE_BankClerkDialogBase.GetReasonText(reason);
			Refresh();
			return;
		}

		m_Accounts = new ELIFE_ClerkAccountList();
		m_Accounts.ExpandFromRAW(json);
		if (m_Accounts.items.IsEmpty())
		{
			m_sNotice = "#ELIFE-Clerk_Transfer_NoAccount";
			m_eRow = ELIFE_ESlipRow.ACTIONS;
			m_iAction = 1;
			Refresh();
			return;
		}

		m_iFrom = 0;
		FillTargets();
		m_bLoaded = true;
		m_sNotice = "";
		Refresh();
	}

	//------------------------------------------------------------------------------------------------
	//! The server checked everything and found the target; the signed slip is the approval, so it books at once.
	void OnTransferQuoteAnswer(int reason, string json)
	{
		if (reason == ELIFE_EClerkReason.NONE)
		{
			m_Quote = new ELIFE_ClerkTransferQuote();
			m_Quote.ExpandFromRAW(json);
			m_Link.RequestTransferConfirm(m_Clerk.GetOwner(), m_Quote.quoteId);
			return;
		}

		m_bWaiting = false;
		m_sNotice = ELIFE_BankClerkDialogBase.GetReasonText(reason);
		Refresh();
	}

	//------------------------------------------------------------------------------------------------
	//! Booked: the clerk hands back the receipt. Anything else stays on the slip with the reason.
	void OnTransferResultAnswer(int reason, string amount, string fee, string balance)
	{
		m_bWaiting = false;
		if (reason == ELIFE_EClerkReason.NONE && m_Quote)
		{
			ELIFE_ClerkTransferReceipt receipt = new ELIFE_ClerkTransferReceipt();
			receipt.targetNumber = m_Quote.targetNumber;
			receipt.targetBankName = m_Quote.targetBankName;
			receipt.amount = amount;
			receipt.fee = fee;
			receipt.balance = balance;
			receipt.Pack();
			HandOver(ELIFE_EClerkDialog.TRANSFER_DONE, receipt.AsString());
			return;
		}

		m_sNotice = ELIFE_BankClerkDialogBase.GetReasonText(reason);
		Refresh();
	}

	//------------------------------------------------------------------------------------------------
	//! The player's other accounts, then the recent recipients, then a typed number. A changed From keeps the chosen target where it can.
	protected void FillTargets()
	{
		string from = GetFromNumber();
		string previous;
		if (m_aTargets.IsIndexValid(m_iTarget))
			previous = m_aTargets[m_iTarget];

		m_aTargets.Clear();
		m_aTargetIsOwn.Clear();
		foreach (ELIFE_ClerkAccount account : m_Accounts.items)
		{
			if (account.number != from)
				AddTarget(account.number, true);
		}

		foreach (string number : m_Accounts.recent)
		{
			if (number != from && !m_aTargets.Contains(number))
				AddTarget(number, false);
		}

		AddTarget("", false);
		m_iTarget = Math.Max(m_aTargets.Find(previous), 0);
	}

	//------------------------------------------------------------------------------------------------
	protected void AddTarget(string number, bool own)
	{
		m_aTargets.Insert(number);
		m_aTargetIsOwn.Insert(own);
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_ClerkAccount GetFromAccount()
	{
		if (!m_Accounts || !m_Accounts.items.IsIndexValid(m_iFrom))
			return null;

		return m_Accounts.items[m_iFrom];
	}

	//------------------------------------------------------------------------------------------------
	protected string GetFromNumber()
	{
		ELIFE_ClerkAccount account = GetFromAccount();
		if (!account)
			return "";

		return account.number;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsOtherTarget()
	{
		return m_aTargets.IsIndexValid(m_iTarget) && m_aTargets[m_iTarget] == "";
	}

	//------------------------------------------------------------------------------------------------
	protected string GetTargetNumber()
	{
		if (!m_aTargets.IsIndexValid(m_iTarget))
			return "";

		if (m_aTargets[m_iTarget] != "")
			return m_aTargets[m_iTarget];

		string number;
		foreach (string character : m_aNumber)
			number += character;

		return number;
	}

	//------------------------------------------------------------------------------------------------
	protected int GetAmount()
	{
		string digits;
		foreach (string character : m_aAmount)
			digits += character;

		//! ToInt() throws on an empty string.
		if (digits.IsEmpty())
			return 0;

		return digits.ToInt();
	}

	//------------------------------------------------------------------------------------------------
	//! The same checks the server makes, so the player sees the problem before handing the slip over.
	protected string GetProblem()
	{
		ELIFE_ClerkAccount from = GetFromAccount();
		if (!from)
			return "";

		string target = GetTargetNumber();
		if (IsOtherTarget() && target.Length() == ELIFE_BankAccountNumber.LENGTH && !ELIFE_BankAccountNumber.IsValid(target))
			return "#ELIFE-Clerk_Reason_InvalidTarget";

		if (target == from.number)
			return "#ELIFE-Clerk_Reason_SameAccount";

		int amount = GetAmount();
		if (amount > ELIFE_ATMComponent.MAX_BOOKING_AMOUNT)
			return WidgetManager.Translate("#ELIFE-Clerk_Transfer_Limit", ELIFE_PhoneBankingService.FormatWholeMoney(ELIFE_ATMComponent.MAX_BOOKING_AMOUNT));

		//! The fee is unknown until booked.
		if (amount > 0 && amount * 100 > from.balanceCents)
			return "#ELIFE-Clerk_Reason_NotEnoughMoney";

		return "";
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsReady()
	{
		return m_bLoaded && !m_bWaiting && GetFromAccount() && GetAmount() > 0 && ELIFE_BankAccountNumber.IsValid(GetTargetNumber()) && GetProblem() == "";
	}

	//------------------------------------------------------------------------------------------------
	//! Mouse.
	void OnWidgetClicked(string name)
	{
		if (m_bIsClosing || m_bWaiting)
			return;

		switch (name)
		{
			case "FromPrev":
				m_eRow = ELIFE_ESlipRow.FROM;
				StepFrom(-1);
				break;
			case "FromNext":
				m_eRow = ELIFE_ESlipRow.FROM;
				StepFrom(1);
				break;
			case "ToPrev":
				m_eRow = ELIFE_ESlipRow.TO;
				StepTarget(-1);
				break;
			case "ToNext":
				m_eRow = ELIFE_ESlipRow.TO;
				StepTarget(1);
				break;
			case "NumberField":
				m_eRow = ELIFE_ESlipRow.NUMBER;
				StartTyping(m_wNumberEdit);
				break;
			case "AmountField":
				m_eRow = ELIFE_ESlipRow.AMOUNT;
				StartTyping(m_wAmountEdit);
				break;
			case "Submit":
				Submit();
				break;
			case "Cancel":
				Cancel();
				break;
		}

		Refresh();
	}

	//------------------------------------------------------------------------------------------------
	void OnWidgetHovered(string name, bool entered)
	{
		if (entered)
			m_sHovered = name;
		else if (m_sHovered == name)
			m_sHovered = "";

		PaintHover();
		PaintActions();
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsGamepad()
	{
		InputManager inputManager = GetGame().GetInputManager();
		return inputManager && !inputManager.IsUsingMouseAndKeyboard();
	}

	//------------------------------------------------------------------------------------------------
	//! Keyboard: an invisible edit box takes the typing, and the boxes show it.
	protected void StartTyping(EditBoxWidget edit)
	{
		if (!edit)
			return;

		m_bEditing = false;
		edit.ClearFlags(WidgetFlags.NOFOCUS);
		GetGame().GetWorkspace().SetFocusedWidget(edit);
		edit.ActivateWriteMode();
	}

	//------------------------------------------------------------------------------------------------
	//! The hidden edit boxes only take focus while a click has started typing. Otherwise a menu that focuses
	//! something for the controller would land on them, and A would start typing instead of filling the boxes.
	protected void SetTypingAllowed(bool allowed)
	{
		array<EditBoxWidget> edits = {m_wNumberEdit, m_wAmountEdit};
		foreach (EditBoxWidget edit : edits)
		{
			if (!edit)
				continue;

			if (allowed)
				edit.ClearFlags(WidgetFlags.NOFOCUS);
			else
				edit.SetFlags(WidgetFlags.NOFOCUS);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void EndTyping()
	{
		SetTypingAllowed(false);
		GetGame().GetWorkspace().SetFocusedWidget(null);
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsTyping()
	{
		return (m_wNumberEdit && m_wNumberEdit.IsInWriteMode()) || (m_wAmountEdit && m_wAmountEdit.IsInWriteMode());
	}

	//------------------------------------------------------------------------------------------------
	//! Typed text into the boxes: spaces dropped, capitals, EL printed already, and only what fits each box.
	protected void SyncTyping()
	{
		bool typing = IsTyping();
		if (m_bWasTyping && !typing)
		{
			m_iTypingEndTick = System.GetTickCount();
			EndTyping();
		}

		bool changed = typing != m_bWasTyping;
		m_bWasTyping = typing;

		//! The edit box holds exactly what the boxes show, so any difference is a keystroke.
		if (m_wNumberEdit && m_wNumberEdit.IsInWriteMode() && m_wNumberEdit.GetText() != GetEditableNumber())
		{
			SetNumberText(m_wNumberEdit.GetText());
			m_wNumberEdit.SetText(GetEditableNumber());
			changed = true;
		}

		if (m_wAmountEdit && m_wAmountEdit.IsInWriteMode() && m_wAmountEdit.GetText() != GetAmountText())
		{
			SetAmountText(m_wAmountEdit.GetText());
			m_wAmountEdit.SetText(GetAmountText());
			changed = true;
		}

		if (changed)
			Refresh();
	}

	//------------------------------------------------------------------------------------------------
	protected void SetNumberText(string text)
	{
		string normalized = ELIFE_BankAccountNumber.Normalize(text);
		if (normalized.StartsWith(ELIFE_BankAccountNumber.COUNTRY))
			normalized = normalized.Substring(NUMBER_FIXED, normalized.Length() - NUMBER_FIXED);

		int position = NUMBER_FIXED;
		for (int i = 0; i < normalized.Length() && position < ELIFE_BankAccountNumber.LENGTH; i++)
		{
			string character = normalized.Get(i);
			if (GetAlphabet(position).Contains(character))
			{
				m_aNumber[position] = character;
				position++;
			}
		}

		for (int i = position; i < ELIFE_BankAccountNumber.LENGTH; i++)
			m_aNumber[i] = "";
	}

	//------------------------------------------------------------------------------------------------
	//! What the edit box holds: the boxes after EL, up to the first empty one.
	protected string GetEditableNumber()
	{
		string text;
		for (int i = NUMBER_FIXED; i < ELIFE_BankAccountNumber.LENGTH && m_aNumber[i] != ""; i++)
			text += m_aNumber[i];

		return text;
	}

	//------------------------------------------------------------------------------------------------
	//! Typed digits fill the boxes from the right, like a calculator: "1000" lands in the last four.
	protected void SetAmountText(string text)
	{
		string digits;
		for (int i = 0; i < text.Length() && digits.Length() < AMOUNT_BOXES; i++)
		{
			string character = text.Get(i);
			if (DIGITS.Contains(character) && (character != "0" || !digits.IsEmpty()))
				digits += character;
		}

		int offset = AMOUNT_BOXES - digits.Length();
		for (int box = 0; box < AMOUNT_BOXES; box++)
		{
			if (box < offset)
				m_aAmount[box] = "";
			else
				m_aAmount[box] = digits.Get(box - offset);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The boxes are places, as on the ATM: left of the first non-zero digit they stay empty, right of it they
	//! read 0. So a 1 in the third box of six fills the rest with 0 and makes 1 000.
	protected void NormalizeAmount()
	{
		bool started;
		for (int i = 0; i < AMOUNT_BOXES; i++)
		{
			string character = m_aAmount[i];
			if (!started && character != "" && character != "0")
				started = true;

			if (!started)
				m_aAmount[i] = "";
			else if (character == "")
				m_aAmount[i] = "0";
		}
	}

	//------------------------------------------------------------------------------------------------
	protected string GetAmountText()
	{
		string text;
		foreach (string character : m_aAmount)
			text += character;

		return text;
	}

	//------------------------------------------------------------------------------------------------
	//! Which characters a box of the number takes.
	protected string GetAlphabet(int position)
	{
		if (position >= NUMBER_LETTERS_START && position < NUMBER_LETTERS_END)
			return LETTERS;

		return DIGITS;
	}

	//------------------------------------------------------------------------------------------------
	protected void StepFrom(int direction)
	{
		if (!m_Accounts || m_Accounts.items.IsEmpty())
			return;

		int count = m_Accounts.items.Count();
		m_iFrom = (m_iFrom + direction + count) % count;
		FillTargets();
		m_sNotice = "";
	}

	//------------------------------------------------------------------------------------------------
	protected void StepTarget(int direction)
	{
		int count = m_aTargets.Count();
		if (count == 0)
			return;

		m_iTarget = (m_iTarget + direction + count) % count;
		m_sNotice = "";
	}

	//------------------------------------------------------------------------------------------------
	protected void Submit()
	{
		if (!IsReady())
			return;

		m_bWaiting = true;
		m_bEditing = false;
		m_sNotice = "#ELIFE-Clerk_Working";
		m_Link.RequestTransferQuote(m_Clerk.GetOwner(), GetFromNumber(), GetTargetNumber(), GetAmount());
	}

	//------------------------------------------------------------------------------------------------
	protected void Cancel()
	{
		HandOver(ELIFE_EClerkDialog.SERVICES, "");
	}

	//------------------------------------------------------------------------------------------------
	//! The next step opens on top before the slip closes, so the backdrop never drops out and the visit continues.
	protected void HandOver(ELIFE_EClerkDialog next, string data)
	{
		if (m_bIsClosing)
			return;

		m_bHandingOver = ELIFE_BankClerkDialogBase.Open(next, m_Clerk, m_Link, data);
		CloseSlip();
	}

	//------------------------------------------------------------------------------------------------
	protected void CloseSlip()
	{
		if (m_bIsClosing)
			return;

		m_bIsClosing = true;
		Close();
	}

	//------------------------------------------------------------------------------------------------
	//! Esc or B: out of the boxes first, then back to the clerk.
	protected void OnBackAction()
	{
		if (m_bIsClosing || !IsFocused() || m_bWaiting)
			return;

		if (IsTyping() || System.GetTickCount() - m_iTypingEndTick < TYPING_END_GRACE_MS)
			return;

		if (m_bEditing)
		{
			m_bEditing = false;
			Refresh();
			return;
		}

		Cancel();
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuUpdate(float tDelta)
	{
		super.OnMenuUpdate(tDelta);

		if (m_bIsClosing)
			return;

		IEntity character = SCR_PlayerController.GetLocalControlledEntity();
		if (!m_Clerk || !m_Clerk.IsUsableBy(character))
		{
			CloseSlip();
			return;
		}

		bool isCustomer = m_Clerk.GetCustomerPlayerId() == SCR_PlayerController.GetLocalPlayerId();
		if (isCustomer)
			m_bSeenAsCustomer = true;
		else if (m_bSeenAsCustomer)
		{
			CloseSlip();
			return;
		}

		SyncTyping();
		PollPad();

		//! Mouse and controller light different things, so a switch repaints.
		bool gamepad = IsGamepad();
		if (gamepad != m_bWasGamepad)
		{
			m_bWasGamepad = gamepad;
			Refresh();
		}

		UpdateHint();
	}

	//------------------------------------------------------------------------------------------------
	//! A controller, through the mod's pad actions. The first frame only records state, so a press held from before does nothing.
	protected void PollPad()
	{
		//! Typing is the keyboard's; picking up the controller ends it.
		if (IsTyping() && IsGamepad())
			EndTyping();

		if (!IsFocused() || IsTyping())
		{
			m_bPolling = false;
			return;
		}

		bool active = m_bPolling;
		m_bPolling = true;

		foreach (int i, string action : m_aPadActions)
		{
			bool down = IsActionDown(action);
			if (active && down && !m_aPadHeld[i])
				OnPad(i);

			m_aPadHeld[i] = down;
		}

		bool selectDown = IsActionDown("ELIFE_PadSelect");
		if (active && selectDown && !m_bSelectHeld)
			OnPadSelect();

		m_bSelectHeld = selectDown;

		bool clearDown = IsActionDown("ELIFE_KeyClear");
		if (active && clearDown && !m_bClearHeld && m_bEditing)
			ClearAtCursor();

		m_bClearHeld = clearDown;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsActionDown(string action)
	{
		InputManager inputManager = GetGame().GetInputManager();
		return inputManager && inputManager.GetActionValue(action) > 0;
	}

	//------------------------------------------------------------------------------------------------
	//! 0 up, 1 down, 2 left, 3 right.
	protected void OnPad(int direction)
	{
		GetGame().GetWorkspace().SetFocusedWidget(null);
		if (m_bWaiting)
			return;

		int vertical;
		if (direction == 0)
			vertical = -1;
		else if (direction == 1)
			vertical = 1;

		int horizontal;
		if (direction == 2)
			horizontal = -1;
		else if (direction == 3)
			horizontal = 1;

		if (m_bEditing)
		{
			if (vertical != 0)
				RollAtCursor(-vertical);
			else
				MoveCursor(horizontal);

			Refresh();
			return;
		}

		if (vertical != 0)
		{
			MoveRow(vertical);
		}
		else if (m_eRow == ELIFE_ESlipRow.FROM)
		{
			StepFrom(horizontal);
		}
		else if (m_eRow == ELIFE_ESlipRow.TO)
		{
			StepTarget(horizontal);
		}
		else if (m_eRow == ELIFE_ESlipRow.ACTIONS)
		{
			m_iAction = Math.ClampInt(m_iAction + horizontal, 0, 1);
		}

		Refresh();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnPadSelect()
	{
		GetGame().GetWorkspace().SetFocusedWidget(null);
		if (m_bWaiting)
			return;

		if (m_bEditing)
		{
			m_bEditing = false;
		}
		else if (m_eRow == ELIFE_ESlipRow.NUMBER || m_eRow == ELIFE_ESlipRow.AMOUNT)
		{
			m_bEditing = true;
			m_iCursor = GetStartBox();
		}
		else if (m_eRow == ELIFE_ESlipRow.FROM)
		{
			StepFrom(1);
		}
		else if (m_eRow == ELIFE_ESlipRow.TO)
		{
			StepTarget(1);
		}
		else if (m_eRow == ELIFE_ESlipRow.ACTIONS)
		{
			if (m_iAction == 0)
				Submit();
			else
				Cancel();
		}

		Refresh();
	}

	//------------------------------------------------------------------------------------------------
	//! Skips the number row while a listed account is the target, and everything but the buttons until the accounts are in.
	protected void MoveRow(int direction)
	{
		int row = m_eRow;
		for (int step = 0; step < 5; step++)
		{
			row = Math.ClampInt(row + direction, ELIFE_ESlipRow.FROM, ELIFE_ESlipRow.ACTIONS);
			if (IsRowAvailable(row))
				break;
		}

		if (IsRowAvailable(row))
			m_eRow = row;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsRowAvailable(int row)
	{
		if (row == ELIFE_ESlipRow.ACTIONS)
			return true;

		if (!m_bLoaded)
			return false;

		return row != ELIFE_ESlipRow.NUMBER || IsOtherTarget();
	}

	//------------------------------------------------------------------------------------------------
	protected int GetStartBox()
	{
		if (m_eRow == ELIFE_ESlipRow.NUMBER)
		{
			for (int i = NUMBER_FIXED; i < ELIFE_BankAccountNumber.LENGTH; i++)
			{
				if (m_aNumber[i] == "")
					return i;
			}

			return ELIFE_BankAccountNumber.LENGTH - 1;
		}

		//! The amount's boxes are places, so a controller starts on the ones, like the ATM's wheel.
		return AMOUNT_BOXES - 1;
	}

	//------------------------------------------------------------------------------------------------
	protected void MoveCursor(int direction)
	{
		if (m_eRow == ELIFE_ESlipRow.NUMBER)
			m_iCursor = Math.ClampInt(m_iCursor + direction, NUMBER_FIXED, ELIFE_BankAccountNumber.LENGTH - 1);
		else
			m_iCursor = Math.ClampInt(m_iCursor + direction, 0, AMOUNT_BOXES - 1);
	}

	//------------------------------------------------------------------------------------------------
	//! Up counts up. The number's boxes go empty, 0 to 9 (or A to Z), empty again; the amount's roll 0 to 9 with no carry.
	protected void RollAtCursor(int direction)
	{
		if (m_eRow != ELIFE_ESlipRow.NUMBER)
		{
			if (!m_aAmount.IsIndexValid(m_iCursor))
				return;

			int digit = Math.Max(DIGITS.IndexOf(m_aAmount[m_iCursor]), 0);
			digit = (digit + direction + 10) % 10;
			m_aAmount[m_iCursor] = digit.ToString();
			NormalizeAmount();
			SyncEditsFromBoxes();
			m_sNotice = "";
			return;
		}

		array<string> chars = m_aNumber;
		string alphabet = GetAlphabet(m_iCursor);
		if (!chars.IsIndexValid(m_iCursor))
			return;

		int count = alphabet.Length() + 1;
		int index = alphabet.IndexOf(chars[m_iCursor]) + 1;
		if (chars[m_iCursor] == "")
			index = 0;

		index = (index + direction + count) % count;
		if (index == 0)
			chars[m_iCursor] = "";
		else
			chars[m_iCursor] = alphabet.Get(index - 1);

		SyncEditsFromBoxes();
		m_sNotice = "";
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearAtCursor()
	{
		array<string> chars = m_aAmount;
		if (m_eRow == ELIFE_ESlipRow.NUMBER)
			chars = m_aNumber;

		if (chars.IsIndexValid(m_iCursor))
			chars[m_iCursor] = "";

		NormalizeAmount();
		SyncEditsFromBoxes();
		Refresh();
	}

	//------------------------------------------------------------------------------------------------
	protected void SyncEditsFromBoxes()
	{
		if (m_wNumberEdit)
			m_wNumberEdit.SetText(GetEditableNumber());

		if (m_wAmountEdit)
			m_wAmountEdit.SetText(GetAmountText());
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdateHint()
	{
		if (!m_wHint)
			return;

		string hint = "#ELIFE-Slip_Hint_Pad";
		if (!IsGamepad())
			hint = "#ELIFE-Slip_Hint_Mouse";
		else if (m_bEditing)
			hint = "#ELIFE-Slip_Hint_Edit";

		if (hint == m_sShownHint)
			return;

		m_sShownHint = hint;
		m_wHint.SetText(hint);
	}

	//------------------------------------------------------------------------------------------------
	//! Everything visible follows the state here, so every input ends with one call.
	protected void Refresh()
	{
		if (!IsRowAvailable(m_eRow))
			m_eRow = ELIFE_ESlipRow.ACTIONS;

		ELIFE_ClerkAccount from = GetFromAccount();
		if (m_wFromValue)
		{
			if (from)
				m_wFromValue.SetText(ELIFE_PhoneBankingService.GroupIban(from.number));
			else
				m_wFromValue.SetText("—");
		}

		//! The number alone fits the row; whose it is goes on the small line under it.
		string toValue = "—";
		string toTag;
		if (IsOtherTarget())
		{
			toValue = "#ELIFE-Clerk_Transfer_OtherAccount";
		}
		else if (m_aTargets.IsIndexValid(m_iTarget))
		{
			toValue = ELIFE_PhoneBankingService.GroupIban(m_aTargets[m_iTarget]);
			toTag = "#ELIFE-Slip_Recent";
			if (m_aTargetIsOwn[m_iTarget])
				toTag = "#ELIFE-Slip_Own";
		}

		if (m_wToValue)
			m_wToValue.SetText(toValue);

		if (m_wToTag)
		{
			m_wToTag.SetText(toTag);
			m_wToTag.SetVisible(toTag != "");
		}

		if (m_aRows.IsIndexValid(ELIFE_ESlipRow.NUMBER) && m_aRows[ELIFE_ESlipRow.NUMBER])
			m_aRows[ELIFE_ESlipRow.NUMBER].SetVisible(IsOtherTarget());

		foreach (int row, Widget focus : m_aRowFocus)
		{
			if (!focus)
				continue;

			bool typed = (row == ELIFE_ESlipRow.NUMBER && m_wNumberEdit && m_wNumberEdit.IsInWriteMode()) || (row == ELIFE_ESlipRow.AMOUNT && m_wAmountEdit && m_wAmountEdit.IsInWriteMode());
			if (typed || (row == m_eRow && IsGamepad()))
				focus.SetColor(Accent(0.16));
			else
				focus.SetColor(Accent(0));
		}

		PaintBoxes(m_aNumber, m_aNumberChars, m_aNumberFrames, m_aNumberFills, ELIFE_ESlipRow.NUMBER, NUMBER_FIXED);
		PaintBoxes(m_aAmount, m_aAmountChars, m_aAmountFrames, m_aAmountFills, ELIFE_ESlipRow.AMOUNT, 0);
		PaintHover();
		PaintActions();
		PaintSummary(from);

		if (m_wNotice)
			m_wNotice.SetText(m_sNotice);
	}

	//------------------------------------------------------------------------------------------------
	//! Printed boxes; the one being rolled (or typed into next) carries the accent.
	protected void PaintBoxes(array<string> chars, array<TextWidget> texts, array<Widget> frames, array<Widget> fills, ELIFE_ESlipRow row, int firstEditable)
	{
		int active = -1;
		if (m_eRow == row && m_bEditing)
			active = m_iCursor;
		else if (m_eRow == row && IsTyping())
			active = GetStartBox();

		foreach (int i, TextWidget text : texts)
		{
			bool printed = i < firstEditable;
			if (text)
			{
				text.SetText(chars[i]);
				if (printed)
					text.SetColor(Faint());
				else
					text.SetColor(Ink());
			}

			if (frames.IsIndexValid(i) && frames[i])
			{
				if (i == active)
					frames[i].SetColor(Accent());
				else if (printed)
					frames[i].SetColor(ELIFE_PhoneStyle.Srgb(0.118, 0.141, 0.2, 0.25));
				else
					frames[i].SetColor(ELIFE_PhoneStyle.Srgb(0.118, 0.141, 0.2, 0.55));
			}

			if (!fills.IsIndexValid(i) || !fills[i])
				continue;

			if (i == active)
				fills[i].SetColor(Accent(0.18));
			else
				fills[i].SetColor(Accent(0));
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The arrows under the cursor turn to the accent.
	protected void PaintHover()
	{
		array<string> arrows = {"FromPrev", "FromNext", "ToPrev", "ToNext"};
		foreach (string arrow : arrows)
		{
			Widget text = GetRootWidget().FindAnyWidget(arrow + "Text");
			if (!text)
				continue;

			if (m_sHovered == arrow)
				text.SetColor(Accent());
			else
				text.SetColor(Ink());
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Sign and hand over is inked when ready, faded until then. The controller's focus or the cursor puts the accent on one.
	protected void PaintActions()
	{
		bool gamepad = IsGamepad();
		bool submitLit = (gamepad && m_eRow == ELIFE_ESlipRow.ACTIONS && m_iAction == 0) || (!gamepad && m_sHovered == "Submit");
		bool cancelLit = (gamepad && m_eRow == ELIFE_ESlipRow.ACTIONS && m_iAction == 1) || (!gamepad && m_sHovered == "Cancel");
		Color paper = ELIFE_PhoneStyle.Srgb(0.929, 0.902, 0.827);

		if (m_wSubmitFill)
		{
			if (submitLit && IsReady())
				m_wSubmitFill.SetColor(Accent());
			else if (IsReady())
				m_wSubmitFill.SetColor(Ink());
			else
				m_wSubmitFill.SetColor(ELIFE_PhoneStyle.Srgb(0.118, 0.141, 0.2, 0.3));
		}

		if (m_wSubmitText)
			m_wSubmitText.SetColor(paper);

		if (m_wCancelFill)
		{
			if (cancelLit)
				m_wCancelFill.SetColor(Accent());
			else
				m_wCancelFill.SetColor(ELIFE_PhoneStyle.Srgb(0.118, 0.141, 0.2, 0.12));
		}

		if (m_wCancelText)
		{
			if (cancelLit)
				m_wCancelText.SetColor(paper);
			else
				m_wCancelText.SetColor(Ink());
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void PaintSummary(ELIFE_ClerkAccount from)
	{
		if (!m_wSummary)
			return;

		string problem = GetProblem();
		if (problem != "")
		{
			m_wSummary.SetText(problem);
			return;
		}

		if (!from)
		{
			m_wSummary.SetText("");
			return;
		}

		if (GetAmount() <= 0 || ELIFE_PhoneBankingService.IsFreeOfCharge(from.feeTerms))
		{
			m_wSummary.SetText(WidgetManager.Translate("#ELIFE-Clerk_Transfer_Balance", from.balance));
			return;
		}

		m_wSummary.SetText(WidgetManager.Translate("#ELIFE-Clerk_Transfer_Summary", from.balance, from.feeTerms));
	}

	//------------------------------------------------------------------------------------------------
	//! Leaving without handing over ends the visit and frees the clerk.
	override void OnMenuClose()
	{
		m_bIsClosing = true;
		if (s_Open == this)
			s_Open = null;

		InputManager inputManager = GetGame().GetInputManager();
		if (inputManager)
			inputManager.RemoveActionListener("MenuBack", EActionTrigger.DOWN, OnBackAction);

		if (!m_bHandingOver && m_Link && m_Clerk)
			m_Link.RequestClose(m_Clerk.GetOwner());

		super.OnMenuClose();
	}
}
