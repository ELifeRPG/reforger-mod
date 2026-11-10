//------------------------------------------------------------------------------------------------
[EntityEditorProps(category: "ELifeRPG/Banking", description: "The ATM's screen: draws the server's state and turns side buttons and the keypad into requests.")]
class ELIFE_ATMScreenComponentClass : ELIFE_WorldScreenComponentClass
{
}

//------------------------------------------------------------------------------------------------
enum ELIFE_EATMScreen
{
	IDLE,
	LOADING,
	NO_ACCOUNT,
	LOAD_FAILED,
	CHOOSE_ACCOUNT,
	HOME,
	TRANSACTIONS,
	AMOUNT,
	OTHER_AMOUNT,
	CONFIRM,
	PROCESSING,
	CHECKING,
	RESULT,
	COUNTING
}

//------------------------------------------------------------------------------------------------
//! One on-screen label beside its physical side button.
class ELIFE_ATMSideLabel
{
	string m_sId;
	Widget m_wRoot;
	Widget m_wTab;
	TextWidget m_wText;
	bool m_bEnabled;
}

//------------------------------------------------------------------------------------------------
//! Client-local view of ELIFE_ATMComponent. It computes no money: every figure comes from the server.
class ELIFE_ATMScreenComponent : ELIFE_WorldScreenComponent
{
	protected const ResourceName LAYOUT_SIDE_LABEL = "{B7D2E94A10C6F358}UI/layouts/Menus/Banking/ATMSideLabel.layout";
	protected const ResourceName LAYOUT_ROW = "{4C81F6A3D95E07B2}UI/layouts/Menus/Banking/ATMTransactionRow.layout";

	protected const string BUTTON_L1 = "side_l1";
	protected const string BUTTON_L2 = "side_l2";
	protected const string BUTTON_L3 = "side_l3";
	protected const string BUTTON_L4 = "side_l4";
	protected const string BUTTON_R1 = "side_r1";
	protected const string BUTTON_R2 = "side_r2";
	protected const string BUTTON_R3 = "side_r3";
	protected const string BUTTON_R4 = "side_r4";
	protected const string KEY_CANCEL = "fn_cancel";
	protected const string KEY_CLEAR = "fn_clear";
	protected const string KEY_CONFIRM = "fn_confirm";
	protected const string KEY_DIGIT_PREFIX = "key_";
	protected const int KEY_DIGIT_LENGTH = 5;

	//! Canvas Y of each physical button's centre: the buttons aren't evenly spaced, and the labels must read straight across.
	protected const float BUTTON_CENTRE_1 = 114;
	protected const float BUTTON_CENTRE_2 = 308;
	protected const float BUTTON_CENTRE_3 = 511;
	protected const float BUTTON_CENTRE_4 = 730;

	//! Canvas px around a label that still counts as clicking it. The buttons are far enough apart that neighbours can't touch.
	protected const float LABEL_HIT_PADDING = 8;

	//! Content areas in canvas px. Each keeps clear of the tabs its screen shows; the card inside hugs its content.
	protected const float CONTENT_LEFT = 48;
	protected const float CONTENT_WIDTH = 928;
	protected const float CONTENT_TOP = 84;
	protected const float CONTENT_BOTTOM = 900;
	protected const float ABOVE_R4_BOTTOM = 690;
	protected const float BELOW_R1_TOP = 176;
	protected const float BELOW_R2_TOP = 372;
	//! Clear of a right-hand tab with the longest label.
	protected const float CONFIRM_WIDTH = 640;
	//! Keeps clear of the account labels on the right.
	protected const float ACCOUNTS_TEXT_WIDTH = 400;
	//! Canvas px from a button's centre to the centre of the bank and balance line under its label.
	protected const float ACCOUNT_DETAIL_OFFSET = 60;
	protected const float AMOUNT_LEFT = 232;
	protected const float AMOUNT_WIDTH = 488;
	//! Above WideNotice, which sits in the gap between the third and fourth button rows.
	protected const float AMOUNT_STACK_BOTTOM = 540;

	protected const float PAGE_ROWS_TOP = 76;
	protected const float PAGE_ROWS_WIDTH = 800;
	protected const int PAGE_ROW_COUNT = 8;
	//! One per button in m_aAccountButtons.
	protected const int ACCOUNTS_PER_PAGE = 3;

	protected const int PRESET_L1 = 50;
	protected const int PRESET_L2 = 100;
	protected const int PRESET_L3 = 200;
	protected const int PRESET_L4 = 500;
	protected const int PRESET_R1 = 1000;
	protected const int PRESET_R2 = 2000;
	protected const int DEPOSIT_PRESETS = 6;

	protected const string UNKNOWN_VALUE = "-";
	protected const string META_SEPARATOR = " · ";
	protected const string ACTIVITY_DOT = "·";
	protected const string ACTIVITY_GAP = " ";
	protected const int ACTIVITY_DOTS = 3;
	protected const int ACTIVITY_STEP_MS = 350;
	protected const int PRESS_FLASH_MS = 150;
	//! A new page's content rises this far into place while it fades in.
	protected const float ENTER_RISE = 16;
	//! The counted sum climbs in steps of the smallest note.
	protected const int COUNT_STEP = 20;

	//! The four type roles; the figure shrinks down to its minimum rather than being cut off.
	protected const int TYPE_FIGURE = 120;
	protected const int TYPE_FIGURE_MIN = 64;
	protected const int TYPE_HEADING = 56;
	protected const int TYPE_BODY = 40;
	protected const int TYPE_SECONDARY = 32;

	//! Stand-in withdrawals for bystanders: a random multiple of the step, with a fee of 1-2 percent.
	protected const int STAND_IN_AMOUNT_STEP = 50;
	protected const int STAND_IN_AMOUNT_MIN_STEPS = 1;
	protected const int STAND_IN_AMOUNT_MAX_STEPS = 40;
	protected const int STAND_IN_FEE_MIN_PERCENT = 1;
	protected const int STAND_IN_FEE_MAX_PERCENT = 3;
	//! How long a stand-in typed amount is, whatever the occupant actually typed.
	protected const int STAND_IN_TYPED_MIN_DIGITS = 2;
	protected const int STAND_IN_TYPED_MAX_DIGITS = 6;

	protected const int KEEP_ALIVE_INTERVAL_MS = 5000;
	//! Well inside the server's 20 s heartbeat window, so a lost one or two don't matter.
	protected const int HEARTBEAT_INTERVAL_MS = 5000;
	//! How long an outcome stays up once there's no session left to dismiss it with.
	protected const int RESULT_LINGER_MS = 15000;
	//! In Sounds/ATM/ELIFE_ATM_Sfx.acp, played from the machine: a key press, and the bill counter while it counts cash,
	//! paid out for a withdrawal, taken in for a deposit.
	protected const string KEY_SOUND = "SOUND_ELIFE_ATM_BEEP";
	protected const string COUNTER_SOUND = "SOUND_ELIFE_ATM_COUNTER";
	//! A deposit's count runs as long as the bill counter (ELIFE_ATM_Counter.wav, 3.1 s with its fade-out).
	protected const int COUNT_MIN_MS = 3000;
	protected const float PAID_HINT_SECONDS = 5;
	//! The zoom hint shows on the first few sessions of a game, until the player has zoomed in once.
	protected const int INSPECT_HINT_MAX_SHOWS = 3;
	protected const float INSPECT_HINT_SECONDS = 5;
	protected static int s_iInspectHintsShown;
	//! Occupant only: the confirm page's receipt choice, off again after each booking or cancel.
	protected bool m_bWantReceipt;

	protected ELIFE_ATMComponent m_ATM;
	protected ELIFE_WorldScreenInteractComponent m_Interact;

	protected Widget m_wStackArea;
	protected Widget m_wRows;
	protected TextWidget m_wAccountNumber;
	protected TextWidget m_wPageMark;
	protected TextWidget m_wMachineId;
	protected ref array<TextWidget> m_aSteps = {};
	protected ELIFE_EATMScreen m_eEnteredScreen = -1;
	protected int m_iEnterStartTick;
	protected bool m_bMotionRunning;
	protected float m_fStackLeft;
	protected float m_fStackTop;
	protected TextWidget m_wBankName;
	protected TextWidget m_wHeading;
	protected Widget m_wLine1;
	protected TextWidget m_wLine1Label;
	protected TextWidget m_wLine1Value;
	protected Widget m_wLine2;
	protected TextWidget m_wLine2Label;
	protected TextWidget m_wLine2Value;
	protected TextWidget m_wCaption;
	protected TextWidget m_wFigure;
	protected TextWidget m_wBody;
	protected TextWidget m_wNotice;
	protected TextWidget m_wWideNotice;
	protected TextWidget m_wActivity;
	protected ref array<ref ELIFE_ATMSideLabel> m_aLabels = {};
	protected string m_sHoveredButton;
	protected string m_sPressedButton;
	protected bool m_bActivityRunning;
	//! Dots showing; 0 until the first step, so a page that's gone within it never shows any.
	protected int m_iActivityDots;

	//! From our grant until the menu closes or the server releases us.
	protected bool m_bSessionOpen;
	protected bool m_bPayloadSeen;
	protected ref ELIFE_BankStatementDto m_Statement;
	protected ref ELIFE_ATMAccountList m_Accounts;
	protected ref array<TextWidget> m_aAccountDetails = {};
	protected int m_iAccountPage;
	protected ref array<string> m_aAccountButtons = {BUTTON_R1, BUTTON_R2, BUTTON_R3};
	protected int m_iLastKeepAliveTick;

	//! Where the session is, while the server's state doesn't override it.
	protected ELIFE_EATMScreen m_eView = ELIFE_EATMScreen.HOME;
	protected ELIFE_EATMScreen m_eConfirmOrigin = ELIFE_EATMScreen.AMOUNT;
	//! Which booking the amount, confirm, processing and result pages are about. A bystander follows the occupant's.
	protected ELIFE_EATMBookingKind m_eKind = ELIFE_EATMBookingKind.WITHDRAW;
	protected bool m_bCountAsked;
	//! Once the counted sum is refused, Back on the presets leads home instead of asking for it again.
	protected bool m_bCountRefused;
	protected int m_iTransactionPage;
	protected string m_sTyped;
	//! The gamepad's digit wheel: which place it turns, 0 being the leftmost.
	protected int m_iWheelPos;

	//! A refusal shown on the page that asked, in place of the result screen.
	protected ELIFE_EATMReason m_eNotice;

	//! The amount a quote was asked for; 0 once answered or abandoned, so a late answer can't change the page.
	protected int m_iQuoteAsked;
	protected int m_iQuoteId;
	protected int m_iQuoteAmount;
	protected string m_sQuoteFeeTerms;
	protected int m_iBookingAmount;
	protected int m_iProcessingStartTick;
	protected int m_iCountStartTick;

	protected ELIFE_EATMReason m_eResult;
	protected int m_iResultAmount;
	//! Empty when the server didn't know it.
	protected string m_sResultFee;
	protected string m_sResultBalance;
	protected int m_iResultDropped;
	//! A paid result waiting out PROCESSING_MIN_MS.
	protected bool m_bResultPending;
	//! Whether m_Statement is newer than the last result, so its balance can stand in for one the server didn't know.
	protected bool m_bStatementAfterResult;

	//! Last page sent to the server, so only a change costs a message. -1 forces the next one.
	protected int m_iSentMirrorScreen = -1;
	protected int m_iSentMirrorPage;
	protected bool m_bSentMirrorTyped;
	protected int m_iSentMirrorReason;
	protected int m_iSentMirrorKind;
	//! The mirrored page the stand-in figures were last rolled for.
	protected ELIFE_EATMScreen m_eMirrorSeen = ELIFE_EATMScreen.IDLE;
	protected bool m_bHeldLock;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		if (!GetGame().InPlayMode())
			return;

		m_ATM = ELIFE_ATMComponent.Cast(owner.FindComponent(ELIFE_ATMComponent));
		m_Interact = ELIFE_WorldScreenInteractComponent.Cast(owner.FindComponent(ELIFE_WorldScreenInteractComponent));
		if (!m_Interact)
			return;

		m_Interact.GetOnButtonPressed().Insert(OnPropButtonPressed);
		m_Interact.GetOnButtonHover().Insert(OnPropButtonHover);
		m_Interact.GetOnActiveChanged().Insert(OnInteractActiveChanged);
		m_Interact.GetOnPadModeChanged().Insert(OnPadModeChanged);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnPadModeChanged(bool padMode)
	{
		if (m_bSessionOpen)
			Render();
	}

	//------------------------------------------------------------------------------------------------
	void OnGrant(bool granted, int reason)
	{
		if (!granted)
		{
			StoreResult(reason, 0, "", "");
			PresentResult();
			return;
		}

		//! The phone may have opened while the request travelled.
		if (ELIFE_PhoneMenu.GetOpen() || !m_Interact)
		{
			SendClose();
			return;
		}

		GetGame().GetCallqueue().Remove(ClearLingeringResult);
		RemoveResultTimers();
		m_bResultPending = false;

		m_bSessionOpen = true;
		m_eResult = ELIFE_EATMReason.NONE;
		SetView(ELIFE_EATMScreen.HOME);
		GetGame().GetCallqueue().Remove(SendHeartbeat);
		GetGame().GetCallqueue().CallLater(SendHeartbeat, HEARTBEAT_INTERVAL_MS, true);
		m_Interact.SetActive(true);
		ELIFE_WorldScreenMenu.Open(m_Interact);
		Render();
		ShowInspectHint();
	}

	//------------------------------------------------------------------------------------------------
	protected void ShowInspectHint()
	{
		if (!m_Interact.CanInspect() || ELIFE_WorldScreenInteractComponent.HasInspected() || s_iInspectHintsShown >= INSPECT_HINT_MAX_SHOWS)
			return;

		s_iInspectHintsShown++;
		SCR_HintManagerComponent.ShowCustomHint("#ELIFE-Hint_ATM_Inspect", "#ELIFE-ATM_Name", INSPECT_HINT_SECONDS);
	}

	//------------------------------------------------------------------------------------------------
	void OnRealPayload(string json)
	{
		m_bStatementAfterResult = true;
		ApplyPayload(json);
	}

	//------------------------------------------------------------------------------------------------
	//! The occupant has the real figures and ignores the broadcast.
	void OnRedactedPayload(string json)
	{
		if (!IsLocalOccupant())
			ApplyPayload(json);
	}

	//------------------------------------------------------------------------------------------------
	void OnRealAccounts(string json)
	{
		ApplyAccounts(json);
	}

	//------------------------------------------------------------------------------------------------
	//! The occupant has the real figures and ignores the broadcast.
	void OnRedactedAccounts(string json)
	{
		if (!IsLocalOccupant())
			ApplyAccounts(json);
	}

	//------------------------------------------------------------------------------------------------
	void OnQuote(int quoteId, int amount, string feeTerms)
	{
		if (m_iQuoteAsked == 0 || amount != m_iQuoteAsked)
			return;

		m_iQuoteId = quoteId;
		m_iQuoteAmount = amount;
		m_sQuoteFeeTerms = feeTerms;

		//! The count stays up while the bill counter runs, however fast the answer comes, so it reads as the machine counting.
		int holdMs = COUNT_MIN_MS - (System.GetTickCount() - m_iCountStartTick);
		if (m_eView == ELIFE_EATMScreen.COUNTING && holdMs > 0)
		{
			GetGame().GetCallqueue().CallLater(OpenConfirm, holdMs);
			return;
		}

		OpenConfirm();
	}

	//------------------------------------------------------------------------------------------------
	//! After a count, Change amount leads to the presets.
	protected void OpenConfirm()
	{
		if (m_iQuoteId == 0)
			return;

		m_eConfirmOrigin = m_eView;
		if (m_eView == ELIFE_EATMScreen.COUNTING)
			m_eConfirmOrigin = ELIFE_EATMScreen.AMOUNT;

		SetView(ELIFE_EATMScreen.CONFIRM);
		Render();
	}

	//------------------------------------------------------------------------------------------------
	//! Stays on the page that asked, so the player can correct it there. An answer nobody is waiting for is dropped.
	void OnQuoteRefused(int reason, int amount)
	{
		if (m_iQuoteAsked == 0 || amount != m_iQuoteAsked)
			return;

		//! A refused count leaves the player on the presets, with the reason.
		if (m_eView == ELIFE_EATMScreen.COUNTING)
			SetView(ELIFE_EATMScreen.AMOUNT);

		if (m_bCountAsked)
			m_bCountRefused = true;

		m_iQuoteAsked = 0;
		m_eNotice = reason;
		Render();
	}

	//------------------------------------------------------------------------------------------------
	void OnResult(int reason, int amount, string fee, string balance, int droppedAmount)
	{
		m_iQuoteId = 0;
		m_sTyped = "";
		m_bStatementAfterResult = false;

		if (reason == ELIFE_EATMReason.QUOTE_INVALID)
		{
			m_iBookingAmount = 0;
			SetView(ELIFE_EATMScreen.AMOUNT);
			m_eNotice = reason;
			Render();
			return;
		}

		StoreResult(reason, amount, fee, balance, droppedAmount);

		int holdMs = ELIFE_ATMComponent.PROCESSING_MIN_MS - (System.GetTickCount() - m_iProcessingStartTick);
		if (reason == ELIFE_EATMReason.PAID && m_bSessionOpen && m_eView == ELIFE_EATMScreen.PROCESSING && holdMs > 0)
		{
			m_bResultPending = true;
			GetGame().GetCallqueue().CallLater(PresentResult, holdMs);
			return;
		}

		PresentResult();
	}

	//------------------------------------------------------------------------------------------------
	void OnMirrorChanged()
	{
		if (!m_ATM || IsLocalOccupant() || m_eResult != ELIFE_EATMReason.NONE)
			return;

		ApplyMirror();
		Render();
	}

	//------------------------------------------------------------------------------------------------
	//! The server's lock decides: losing it closes the menu without another close request.
	void OnOccupancyChanged()
	{
		bool mine = HoldsLock();

		//! The figures of a bystander's stand-in page must not reach the player's own.
		if (mine && !m_bHeldLock)
			ClearStandIns();

		m_bHeldLock = mine;

		if (!mine)
		{
			ClearStandIns();
			m_iSentMirrorScreen = -1;
			m_Accounts = null;
			m_Statement = null;
			m_bPayloadSeen = false;
			m_iQuoteId = 0;
			m_iQuoteAsked = 0;
			m_eNotice = ELIFE_EATMReason.NONE;

			//! The server keeps the lock for as long as a paid result is up, so this ends it.
			if (m_eResult == ELIFE_EATMReason.PAID && !m_bResultPending)
			{
				GetGame().GetCallqueue().Remove(ClearLingeringResult);
				m_eResult = ELIFE_EATMReason.NONE;
				SetView(ELIFE_EATMScreen.HOME);
			}
		}

		if (m_bSessionOpen && !mine)
		{
			m_bSessionOpen = false;
			RemoveResultTimers();

			//! Already paid, so the player still hears about it.
			if (m_bResultPending)
				PresentResult();
			else
				LingerResult();

			ELIFE_WorldScreenMenu menu = ELIFE_WorldScreenMenu.GetOpen();
			if (menu && menu.GetBoundScreen() == m_Interact)
				menu.CloseMenu();
		}

		if (!IsLocalOccupant() && m_eResult == ELIFE_EATMReason.NONE)
			ApplyMirror();

		Render();
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearStandIns()
	{
		m_sTyped = "";
		m_iTransactionPage = 0;
		m_iAccountPage = 0;
		m_iQuoteAmount = 0;
		m_iBookingAmount = 0;
		m_bStatementAfterResult = false;
		m_eMirrorSeen = ELIFE_EATMScreen.IDLE;
		m_eKind = ELIFE_EATMBookingKind.WITHDRAW;
		m_bCountRefused = false;
	}

	//------------------------------------------------------------------------------------------------
	//! Fills the fields the pages already draw from with stand-ins, so a bystander runs the same rendering as the occupant.
	protected void ApplyMirror()
	{
		ELIFE_EATMScreen screen = m_ATM.GetMirrorScreen();
		m_eKind = m_ATM.GetMirrorKind();

		if (screen == ELIFE_EATMScreen.CHOOSE_ACCOUNT)
			m_iAccountPage = m_ATM.GetMirrorPage();
		else
			m_iTransactionPage = m_ATM.GetMirrorPage();

		m_eNotice = ELIFE_EATMReason.NONE;
		if (screen != ELIFE_EATMScreen.RESULT)
			m_eNotice = m_ATM.GetMirrorReason();

		if (screen == ELIFE_EATMScreen.OTHER_AMOUNT)
			SetStandInTyped(m_ATM.IsMirrorTyped());

		if (screen == ELIFE_EATMScreen.CONFIRM || screen == ELIFE_EATMScreen.PROCESSING || screen == ELIFE_EATMScreen.CHECKING || screen == ELIFE_EATMScreen.RESULT)
		{
			//! A new Confirm page means a new amount; the pages after it keep showing the same one.
			if (screen == ELIFE_EATMScreen.CONFIRM && m_eMirrorSeen != ELIFE_EATMScreen.CONFIRM)
				m_iQuoteAmount = 0;

			if (m_iQuoteAmount == 0)
				RollStandInAmount();

			//! Reads the redacted statement's balance, which the result then shows.
			m_sResultBalance = "";
			m_bStatementAfterResult = true;
		}

		m_eMirrorSeen = screen;
	}

	//------------------------------------------------------------------------------------------------
	//! One rolled amount for as long as something is typed: the length of the real one would give its size away.
	protected void SetStandInTyped(bool typed)
	{
		if (!typed)
		{
			m_sTyped = "";
			return;
		}

		if (m_sTyped != "")
			return;

		int count = Math.RandomInt(STAND_IN_TYPED_MIN_DIGITS, STAND_IN_TYPED_MAX_DIGITS);
		while (m_sTyped.Length() < count)
		{
			int low = 0;
			if (m_sTyped == "")
				low = 1;

			int digit = Math.RandomInt(low, 10);
			m_sTyped += digit.ToString();
		}
	}

	//------------------------------------------------------------------------------------------------
	//! What was "typed" carries over to Confirm, as it does for the occupant.
	protected void RollStandInAmount()
	{
		int amount = Math.RandomInt(STAND_IN_AMOUNT_MIN_STEPS, STAND_IN_AMOUNT_MAX_STEPS) * STAND_IN_AMOUNT_STEP;
		if (m_eMirrorSeen == ELIFE_EATMScreen.OTHER_AMOUNT && m_sTyped != "")
			amount = m_sTyped.ToInt();

		int percent = Math.RandomInt(STAND_IN_FEE_MIN_PERCENT, STAND_IN_FEE_MAX_PERCENT);
		m_iQuoteAmount = amount;
		m_sQuoteFeeTerms = percent.ToString() + " %";
		m_iBookingAmount = amount;
		m_iResultAmount = amount;
		m_sResultFee = Money(amount * percent);
	}

	//------------------------------------------------------------------------------------------------
	//! Only for the occupant. A failed send stays unrecorded, so the next render tries again.
	protected void SendMirror(ELIFE_EATMScreen screen)
	{
		if (!HoldsLock())
			return;

		int reason = m_eNotice;
		if (screen == ELIFE_EATMScreen.RESULT)
			reason = m_eResult;

		int page;
		if (screen == ELIFE_EATMScreen.TRANSACTIONS)
			page = m_iTransactionPage;
		else if (screen == ELIFE_EATMScreen.CHOOSE_ACCOUNT)
			page = m_iAccountPage;

		bool typed = screen == ELIFE_EATMScreen.OTHER_AMOUNT && m_sTyped != "";

		if (screen == m_iSentMirrorScreen && page == m_iSentMirrorPage && typed == m_bSentMirrorTyped && reason == m_iSentMirrorReason && m_eKind == m_iSentMirrorKind)
			return;

		ELIFE_ATMCharacterComponent link = GetLink();
		if (!link)
			return;

		m_iSentMirrorScreen = screen;
		m_iSentMirrorPage = page;
		m_bSentMirrorTyped = typed;
		m_iSentMirrorReason = reason;
		m_iSentMirrorKind = m_eKind;
		link.RequestMirror(GetOwner(), screen, page, typed, reason, m_eKind);
	}

	//------------------------------------------------------------------------------------------------
	//! A result from the occupant's page, or this client's own if it has one.
	protected ELIFE_EATMReason GetShownResult()
	{
		if (m_eResult != ELIFE_EATMReason.NONE || IsLocalOccupant())
			return m_eResult;

		return m_ATM.GetMirrorReason();
	}

	//------------------------------------------------------------------------------------------------
	void OnStateChanged()
	{
		//! The old statement must not show as the next account's while that one's payload travels.
		if (m_ATM && m_ATM.GetState() == ELIFE_EATMState.CHOOSE_ACCOUNT && IsLocalOccupant())
		{
			m_Statement = null;
			if (m_eView != ELIFE_EATMScreen.RESULT && m_eView != ELIFE_EATMScreen.PROCESSING)
				SetView(ELIFE_EATMScreen.HOME);
		}

		Render();
	}

	//------------------------------------------------------------------------------------------------
	protected bool HoldsLock()
	{
		return m_ATM && m_ATM.GetOccupantPlayerId() != 0 && m_ATM.GetOccupantPlayerId() == SCR_PlayerController.GetLocalPlayerId();
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsLocalOccupant()
	{
		return m_bSessionOpen || HoldsLock();
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyPayload(string json)
	{
		m_bPayloadSeen = true;
		m_Statement = null;
		if (json != "")
		{
			m_Statement = new ELIFE_BankStatementDto();
			m_Statement.ExpandFromRAW(json);
		}

		Render();
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyAccounts(string json)
	{
		m_bPayloadSeen = true;
		m_Accounts = null;
		if (json != "")
		{
			m_Accounts = new ELIFE_ATMAccountList();
			m_Accounts.ExpandFromRAW(json);
		}

		Render();
	}

	//------------------------------------------------------------------------------------------------
	protected bool HasSeveralAccounts()
	{
		return m_Accounts && m_Accounts.items.Count() > 1;
	}

	//------------------------------------------------------------------------------------------------
	protected void StoreResult(int reason, int amount, string fee, string balance, int droppedAmount = 0)
	{
		m_eResult = reason;
		m_iResultAmount = amount;
		m_sResultFee = fee;
		m_sResultBalance = balance;
		m_iResultDropped = droppedAmount;
	}

	//------------------------------------------------------------------------------------------------
	//! The stored result goes on screen. A paid one is announced here, once, and releases the player at the same moment.
	protected void PresentResult()
	{
		m_bResultPending = false;
		m_iBookingAmount = 0;
		SetView(ELIFE_EATMScreen.RESULT);
		LingerResult();

		if (m_eResult == ELIFE_EATMReason.PAID)
			AnnouncePaid();

		Render();

		if (m_eResult == ELIFE_EATMReason.PAID)
			CloseScreen();
	}

	//------------------------------------------------------------------------------------------------
	//! The hint stays silent. A withdrawal's cash is counted out now; a deposit's was counted when it went in.
	protected void AnnouncePaid()
	{
		if (IsDeposit())
		{
			SCR_HintManagerComponent.ShowCustomHint(WidgetManager.Translate("#ELIFE-ATM_Hint_Deposit", Whole(m_iResultAmount)), "#ELIFE-ATM_Hint_Deposit_Title", PAID_HINT_SECONDS, true);
			return;
		}

		PlaySfx(COUNTER_SOUND);
		SCR_HintManagerComponent.ShowCustomHint(GetPaidHintText(), "#ELIFE-ATM_Hint_Cash_Title", PAID_HINT_SECONDS, true);
	}

	//------------------------------------------------------------------------------------------------
	protected string GetPaidHintText()
	{
		if (m_iResultDropped <= 0)
			return WidgetManager.Translate("#ELIFE-ATM_Hint_Cash", Whole(m_iResultAmount));

		if (m_iResultDropped >= m_iResultAmount)
			return WidgetManager.Translate("#ELIFE-ATM_Hint_Cash_Ground", Whole(m_iResultDropped));

		return WidgetManager.Translate("#ELIFE-ATM_Hint_Cash_Split", Whole(m_iResultAmount - m_iResultDropped), Whole(m_iResultDropped));
	}

	//------------------------------------------------------------------------------------------------
	//! Only on this machine, from the ATM itself.
	protected void PlaySfx(string soundEvent)
	{
		SoundComponent sound = SoundComponent.Cast(GetOwner().FindComponent(SoundComponent));
		if (!sound)
			return;

		if (sound.SoundEvent(soundEvent) == AudioHandle.Invalid)
			Print(string.Format("ELIFE_ATMScreenComponent | no sound for %1", soundEvent), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	protected void RemoveResultTimers()
	{
		GetGame().GetCallqueue().Remove(PresentResult);
	}

	//------------------------------------------------------------------------------------------------
	//! A result with no session behind it (refused open, or the menu closed mid-withdrawal) has no Done to clear it.
	protected void LingerResult()
	{
		if (m_bSessionOpen || m_eResult == ELIFE_EATMReason.NONE)
			return;

		GetGame().GetCallqueue().Remove(ClearLingeringResult);
		GetGame().GetCallqueue().CallLater(ClearLingeringResult, RESULT_LINGER_MS);
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearLingeringResult()
	{
		m_eResult = ELIFE_EATMReason.NONE;
		SetView(ELIFE_EATMScreen.HOME);

		if (!IsLocalOccupant())
			ApplyMirror();

		Render();
	}

	//------------------------------------------------------------------------------------------------
	protected void SetView(ELIFE_EATMScreen view)
	{
		GetGame().GetCallqueue().Remove(OpenConfirm);
		m_eView = view;
		m_eNotice = ELIFE_EATMReason.NONE;
		m_iQuoteAsked = 0;
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_ATMCharacterComponent GetLink()
	{
		IEntity character = SCR_PlayerController.GetLocalControlledEntity();
		if (!character)
			return null;

		return ELIFE_ATMCharacterComponent.Cast(character.FindComponent(ELIFE_ATMCharacterComponent));
	}

	//------------------------------------------------------------------------------------------------
	//! With no character left to send through (death), the server's tick releases the lock.
	protected void SendClose()
	{
		ELIFE_ATMCharacterComponent link = GetLink();
		if (link)
			link.RequestClose(GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	protected void OnInteractActiveChanged(bool active)
	{
		if (active || !m_bSessionOpen)
			return;

		m_bSessionOpen = false;
		m_iQuoteId = 0;
		m_sTyped = "";
		RemoveResultTimers();

		//! Already paid, so the player still hears about it.
		if (m_bResultPending)
			PresentResult();

		//! The player has left, but the server keeps the machine until the result has been shown.
		if (m_eResult == ELIFE_EATMReason.PAID && m_eView == ELIFE_EATMScreen.RESULT)
		{
			LingerResult();
			Render();
			return;
		}

		m_eResult = ELIFE_EATMReason.NONE;
		SetView(ELIFE_EATMScreen.HOME);
		SendClose();
		Render();
	}

	//------------------------------------------------------------------------------------------------
	//! Every recheck: a bystander near a busy ATM that hasn't had its data yet asks for it (streamed in late, or just bound).
	protected override void OnProximityChecked(float distance)
	{
		if (!m_bBound || m_bPayloadSeen || !m_ATM || m_ATM.GetOccupantPlayerId() == 0 || IsLocalOccupant())
			return;

		ELIFE_ATMCharacterComponent link = GetLink();
		if (link)
			link.RequestRedacted(GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	//! The server's state outranks the player's page, and a result outlasts the load that follows it.
	protected ELIFE_EATMScreen ResolveScreen()
	{
		if (!IsLocalOccupant())
		{
			if (m_eResult != ELIFE_EATMReason.NONE)
				return ELIFE_EATMScreen.RESULT;

			if (m_ATM.GetOccupantPlayerId() != 0)
				return ResolveMirrorScreen();

			return ELIFE_EATMScreen.IDLE;
		}

		ELIFE_EATMState state = m_ATM.GetState();
		if (state == ELIFE_EATMState.PROCESSING)
			return ELIFE_EATMScreen.PROCESSING;

		if (state == ELIFE_EATMState.CHECKING)
			return ELIFE_EATMScreen.CHECKING;

		//! The view is PROCESSING from the confirm press until the answer is shown, whatever order state and answer arrive in.
		if (m_eView == ELIFE_EATMScreen.RESULT || m_eView == ELIFE_EATMScreen.PROCESSING)
			return m_eView;

		if (state == ELIFE_EATMState.CHOOSE_ACCOUNT)
		{
			if (m_Accounts)
				return ELIFE_EATMScreen.CHOOSE_ACCOUNT;

			return ELIFE_EATMScreen.LOADING;
		}

		if (state == ELIFE_EATMState.NO_ACCOUNT)
			return ELIFE_EATMScreen.NO_ACCOUNT;

		if (state == ELIFE_EATMState.LOAD_FAILED)
			return ELIFE_EATMScreen.LOAD_FAILED;

		if (state != ELIFE_EATMState.READY || !m_Statement)
			return ELIFE_EATMScreen.LOADING;

		return m_eView;
	}

	//------------------------------------------------------------------------------------------------
	//! The occupant's page, once there is one and the redacted statement to fill it with has arrived.
	protected ELIFE_EATMScreen ResolveMirrorScreen()
	{
		ELIFE_EATMScreen screen = m_ATM.GetMirrorScreen();
		if (screen == ELIFE_EATMScreen.IDLE)
			return ELIFE_EATMScreen.LOADING;

		if (screen == ELIFE_EATMScreen.LOADING || screen == ELIFE_EATMScreen.NO_ACCOUNT || screen == ELIFE_EATMScreen.LOAD_FAILED || screen == ELIFE_EATMScreen.PROCESSING || screen == ELIFE_EATMScreen.CHECKING || screen == ELIFE_EATMScreen.COUNTING)
			return screen;

		if (screen == ELIFE_EATMScreen.CHOOSE_ACCOUNT)
		{
			if (m_Accounts)
				return screen;

			return ELIFE_EATMScreen.LOADING;
		}

		if (!m_Statement)
			return ELIFE_EATMScreen.LOADING;

		return screen;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnPropButtonHover(string id)
	{
		m_sHoveredButton = id;
		PaintLabels();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnPropButtonPressed(string id)
	{
		if (!m_bSessionOpen || !m_ATM)
			return;

		ELIFE_ATMCharacterComponent link = GetLink();
		if (!link)
			return;

		SendKeepAlive(link);
		FlashPressed(id);
		PlaySfx(KEY_SOUND);

		switch (ResolveScreen())
		{
			case ELIFE_EATMScreen.LOADING:
				PressLoading(id);
				break;
			case ELIFE_EATMScreen.NO_ACCOUNT:
			case ELIFE_EATMScreen.LOAD_FAILED:
				PressLoadProblem(id, link);
				break;
			case ELIFE_EATMScreen.CHOOSE_ACCOUNT:
				PressChooseAccount(id, link);
				break;
			case ELIFE_EATMScreen.HOME:
				PressHome(id, link);
				break;
			case ELIFE_EATMScreen.TRANSACTIONS:
				PressTransactions(id);
				break;
			case ELIFE_EATMScreen.AMOUNT:
				PressAmount(id, link);
				break;
			case ELIFE_EATMScreen.COUNTING:
				if (id == BUTTON_R4 || id == KEY_CANCEL)
					SetView(ELIFE_EATMScreen.HOME);

				break;
			case ELIFE_EATMScreen.OTHER_AMOUNT:
				PressOtherAmount(id, link);
				break;
			case ELIFE_EATMScreen.CONFIRM:
				PressConfirm(id, link);
				break;
			case ELIFE_EATMScreen.RESULT:
				PressResult(id);
				break;
		}

		Render();
	}

	//------------------------------------------------------------------------------------------------
	protected void FlashPressed(string id)
	{
		m_sPressedButton = id;
		GetGame().GetCallqueue().Remove(ClearPressed);
		GetGame().GetCallqueue().CallLater(ClearPressed, PRESS_FLASH_MS);
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearPressed()
	{
		m_sPressedButton = "";
		PaintLabels();
	}

	//------------------------------------------------------------------------------------------------
	protected void PressLoading(string id)
	{
		if (id == KEY_CANCEL)
			CloseScreen();
	}

	//------------------------------------------------------------------------------------------------
	protected void PressLoadProblem(string id, notnull ELIFE_ATMCharacterComponent link)
	{
		if (id == BUTTON_R1)
		{
			link.RequestRefresh(GetOwner());
		}
		else if (id == BUTTON_R3 && HasSeveralAccounts())
		{
			m_iAccountPage = 0;
			link.RequestChangeAccount(GetOwner());
		}
		else if (id == BUTTON_R4 || id == KEY_CANCEL)
		{
			CloseScreen();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void PressChooseAccount(string id, notnull ELIFE_ATMCharacterComponent link)
	{
		if (id == BUTTON_L1 && m_iAccountPage > 0)
		{
			m_iAccountPage--;
		}
		else if (id == BUTTON_L2 && HasNextAccountPage())
		{
			m_iAccountPage++;
		}
		else if (id == BUTTON_R4 || id == KEY_CANCEL)
		{
			CloseScreen();
		}
		else
		{
			int slot = m_aAccountButtons.Find(id);
			int index = m_iAccountPage * ACCOUNTS_PER_PAGE + slot;
			if (slot >= 0 && m_Accounts && index < m_Accounts.items.Count())
			{
				SetView(ELIFE_EATMScreen.HOME);
				link.RequestSelectAccount(GetOwner(), m_Accounts.items[index].number);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool HasNextAccountPage()
	{
		return m_Accounts && (m_iAccountPage + 1) * ACCOUNTS_PER_PAGE < m_Accounts.items.Count();
	}

	//------------------------------------------------------------------------------------------------
	protected void PressHome(string id, notnull ELIFE_ATMCharacterComponent link)
	{
		if (id == BUTTON_R1)
		{
			m_eKind = ELIFE_EATMBookingKind.WITHDRAW;
			SetView(ELIFE_EATMScreen.AMOUNT);
		}
		else if (id == BUTTON_L1 && m_ATM.OffersDeposits() && GetCarriedCash() > 0)
		{
			//! Like a cash-in slot: everything carried is counted, up to the limit, and the presets are one press away.
			m_eKind = ELIFE_EATMBookingKind.DEPOSIT;
			SetView(ELIFE_EATMScreen.COUNTING);
			m_iCountStartTick = System.GetTickCount();
			m_bCountRefused = false;
			PlaySfx(COUNTER_SOUND);
			AskQuote(Math.Min(GetCarriedCash(), ELIFE_ATMComponent.MAX_BOOKING_AMOUNT), link, true);
		}
		else if (id == BUTTON_R3 && HasSeveralAccounts())
		{
			m_iAccountPage = 0;
			link.RequestChangeAccount(GetOwner());
		}
		else if (id == BUTTON_R2)
		{
			m_iTransactionPage = 0;
			SetView(ELIFE_EATMScreen.TRANSACTIONS);
		}
		else if (id == BUTTON_R4 || id == KEY_CANCEL)
		{
			CloseScreen();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void PressTransactions(string id)
	{
		if (id == BUTTON_R1 && HasNewerPage())
			m_iTransactionPage--;
		else if (id == BUTTON_R2 && HasOlderPage())
			m_iTransactionPage++;
		else if (id == BUTTON_R4 || id == KEY_CANCEL)
			SetView(ELIFE_EATMScreen.HOME);
	}

	//------------------------------------------------------------------------------------------------
	protected void PressAmount(string id, notnull ELIFE_ATMCharacterComponent link)
	{
		if (id == BUTTON_R3)
		{
			m_sTyped = "";
			m_iWheelPos = GetWheelDigits() - 1;
			SetView(ELIFE_EATMScreen.OTHER_AMOUNT);
			return;
		}

		if (id == BUTTON_R4 && IsDeposit() && !m_bCountRefused && GetCarriedCash() > 0)
		{
			//! Back to the counted sum, recounted in case the inventory changed meanwhile: shown and heard like the first count.
			SetView(ELIFE_EATMScreen.COUNTING);
			m_iCountStartTick = System.GetTickCount();
			PlaySfx(COUNTER_SOUND);
			AskQuote(Math.Min(GetCarriedCash(), ELIFE_ATMComponent.MAX_BOOKING_AMOUNT), link, true);
			return;
		}

		if (id == BUTTON_R4 || id == KEY_CANCEL)
		{
			SetView(ELIFE_EATMScreen.HOME);
			return;
		}

		int preset = GetPreset(id);
		if (preset > 0 && IsAffordable(preset))
			AskQuote(preset, link);
	}

	//------------------------------------------------------------------------------------------------
	protected void PressOtherAmount(string id, notnull ELIFE_ATMCharacterComponent link)
	{
		if (id.Length() == KEY_DIGIT_LENGTH && id.StartsWith(KEY_DIGIT_PREFIX))
		{
			PressDigit(id.Substring(KEY_DIGIT_LENGTH - 1, 1));
		}
		else if (id == KEY_CLEAR)
		{
			m_sTyped = "";
			m_eNotice = ELIFE_EATMReason.NONE;
			m_iQuoteAsked = 0;
		}
		else if (id == KEY_CONFIRM || id == BUTTON_R3)
		{
			if (m_sTyped != "")
				AskQuote(m_sTyped.ToInt(), link);
		}
		else if (id == BUTTON_R4 || id == KEY_CANCEL)
		{
			SetView(ELIFE_EATMScreen.AMOUNT);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Capped by digits only: an amount above the limit is the server's to refuse, with its reason on screen.
	protected void PressDigit(string digit)
	{
		int maxAmount = ELIFE_ATMComponent.MAX_BOOKING_AMOUNT;
		if (m_sTyped.Length() >= maxAmount.ToString().Length() || (m_sTyped == "" && digit == "0"))
			return;

		m_sTyped += digit;
		m_eNotice = ELIFE_EATMReason.NONE;
		m_iQuoteAsked = 0;
	}

	//------------------------------------------------------------------------------------------------
	protected void PressConfirm(string id, notnull ELIFE_ATMCharacterComponent link)
	{
		if (id == BUTTON_R2)
		{
			m_bWantReceipt = !m_bWantReceipt;
			return;
		}

		if (id == BUTTON_R3)
		{
			m_iQuoteId = 0;
			SetView(m_eConfirmOrigin);
			return;
		}

		if (id == BUTTON_L4 || id == KEY_CANCEL)
		{
			m_iQuoteId = 0;
			m_bWantReceipt = false;
			SetView(ELIFE_EATMScreen.HOME);
			return;
		}

		if ((id != BUTTON_R4 && id != KEY_CONFIRM) || m_iQuoteId == 0)
			return;

		//! On a host the answer arrives inside the request, so everything it reads or overrides is set first.
		int quoteId = m_iQuoteId;
		m_iBookingAmount = m_iQuoteAmount;
		m_iProcessingStartTick = System.GetTickCount();
		m_iQuoteId = 0;
		bool receipt = m_bWantReceipt;
		m_bWantReceipt = false;
		SetView(ELIFE_EATMScreen.PROCESSING);
		link.RequestConfirm(GetOwner(), quoteId, receipt);
	}

	//------------------------------------------------------------------------------------------------
	protected void PressResult(string id)
	{
		//! The menu is already closing.
		if (m_eResult == ELIFE_EATMReason.PAID)
			return;

		if (id != BUTTON_R4 && id != KEY_CANCEL)
			return;

		m_eResult = ELIFE_EATMReason.NONE;
		SetView(ELIFE_EATMScreen.HOME);
	}

	//------------------------------------------------------------------------------------------------
	//! A second request while one travels would make the first answer's quote id stale.
	protected void AskQuote(int amount, notnull ELIFE_ATMCharacterComponent link, bool isCount = false)
	{
		if (m_iQuoteAsked != 0)
			return;

		m_eNotice = ELIFE_EATMReason.NONE;
		m_iQuoteAsked = amount;
		m_bCountAsked = isCount;
		link.RequestQuote(GetOwner(), m_eKind, amount);
	}

	//------------------------------------------------------------------------------------------------
	protected void CloseScreen()
	{
		ELIFE_WorldScreenMenu menu = ELIFE_WorldScreenMenu.GetOpen();
		if (menu && menu.GetBoundScreen() == m_Interact)
			menu.CloseMenu();
	}

	//------------------------------------------------------------------------------------------------
	//! Every few seconds while the menu is open, whether or not anything is pressed. Stops itself once the session ends.
	protected void SendHeartbeat()
	{
		if (!m_bSessionOpen)
		{
			GetGame().GetCallqueue().Remove(SendHeartbeat);
			return;
		}

		ELIFE_ATMCharacterComponent link = GetLink();
		if (link)
			link.RequestHeartbeat(GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	//! A press, so it also counts as input for the server's AFK limit.
	protected void SendKeepAlive(notnull ELIFE_ATMCharacterComponent link)
	{
		int now = System.GetTickCount();
		if (m_iLastKeepAliveTick != 0 && now - m_iLastKeepAliveTick < KEEP_ALIVE_INTERVAL_MS)
			return;

		m_iLastKeepAliveTick = now;
		link.RequestKeepAlive(GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	protected int GetPreset(string id)
	{
		if (IsDeposit())
			return GetDepositPreset(id);

		if (id == BUTTON_L1)
			return PRESET_L1;

		if (id == BUTTON_L2)
			return PRESET_L2;

		if (id == BUTTON_L3)
			return PRESET_L3;

		if (id == BUTTON_L4)
			return PRESET_L4;

		if (id == BUTTON_R1)
			return PRESET_R1;

		if (id == BUTTON_R2)
			return PRESET_R2;

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! The count is known, so deposits offer round steps that fit it, smallest on L1. Fewer than six leave the rest blank.
	protected int GetDepositPreset(string id)
	{
		array<string> buttons = {BUTTON_L1, BUTTON_L2, BUTTON_L3, BUTTON_L4, BUTTON_R1, BUTTON_R2};
		int slot = buttons.Find(id);
		if (slot < 0)
			return 0;

		int cap = Math.Min(GetCarriedCash(), ELIFE_ATMComponent.MAX_BOOKING_AMOUNT);
		array<int> steps = {10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000};
		array<int> fitting = {};
		foreach (int step : steps)
		{
			if (step <= cap)
				fitting.Insert(step);
		}

		int first = Math.Max(fitting.Count() - DEPOSIT_PRESETS, 0);
		if (first + slot >= fitting.Count())
			return 0;

		return fitting[first + slot];
	}

	//------------------------------------------------------------------------------------------------
	//! Only dims and blocks a preset; the server still decides on amount plus fee.
	protected bool IsAffordable(int amount)
	{
		if (IsDeposit())
			return amount <= GetCarriedCash();

		return HasAccount() && amount * 100 <= ELIFE_PhoneBankingService.MoneyTextToCents(m_Statement.account.balanceStr);
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsDeposit()
	{
		return m_eKind == ELIFE_EATMBookingKind.DEPOSIT;
	}

	//------------------------------------------------------------------------------------------------
	//! The occupant's own count. A bystander has no inventory to read, so it gets a stand-in.
	protected int GetCarriedCash()
	{
		if (!IsLocalOccupant())
			return STAND_IN_AMOUNT_MAX_STEPS * STAND_IN_AMOUNT_STEP;

		IEntity character = SCR_PlayerController.GetLocalControlledEntity();
		if (!character)
			return 0;

		return ELIFE_CashComponent.CountCarriedCash(character);
	}

	//------------------------------------------------------------------------------------------------
	//! The deposit wording where it differs, else the withdrawal's.
	protected string ForKind(string withdrawKey, string depositKey)
	{
		if (IsDeposit())
			return depositKey;

		return withdrawKey;
	}

	//------------------------------------------------------------------------------------------------
	protected bool HasAccount()
	{
		return m_Statement && m_Statement.account.number != "";
	}

	//------------------------------------------------------------------------------------------------
	protected bool HasNewerPage()
	{
		return m_iTransactionPage > 0;
	}

	//------------------------------------------------------------------------------------------------
	protected bool HasOlderPage()
	{
		return m_Statement && (m_iTransactionPage + 1) * PAGE_ROW_COUNT < m_Statement.items.Count();
	}

	//------------------------------------------------------------------------------------------------
	protected override bool BuildContent(Widget host)
	{
		if (!super.BuildContent(host))
			return false;

		Widget contentRoot = GetContentRoot();
		if (!contentRoot)
			return true;

		m_wStackArea = contentRoot.FindAnyWidget("StackArea");
		m_wRows = contentRoot.FindAnyWidget("Rows");
		m_wAccountNumber = TextWidget.Cast(contentRoot.FindAnyWidget("AccountNumber"));
		m_wPageMark = TextWidget.Cast(contentRoot.FindAnyWidget("PageMark"));
		m_wBankName = TextWidget.Cast(contentRoot.FindAnyWidget("BankName"));
		m_wHeading = FindWrappedText(contentRoot, "Heading");
		m_wLine1 = contentRoot.FindAnyWidget("Line1");
		m_wLine1Label = TextWidget.Cast(contentRoot.FindAnyWidget("Line1Label"));
		m_wLine1Value = TextWidget.Cast(contentRoot.FindAnyWidget("Line1Value"));
		m_wLine2 = contentRoot.FindAnyWidget("Line2");
		m_wLine2Label = TextWidget.Cast(contentRoot.FindAnyWidget("Line2Label"));
		m_wLine2Value = TextWidget.Cast(contentRoot.FindAnyWidget("Line2Value"));
		m_wCaption = TextWidget.Cast(contentRoot.FindAnyWidget("Caption"));
		m_wFigure = TextWidget.Cast(contentRoot.FindAnyWidget("Figure"));
		m_wBody = FindWrappedText(contentRoot, "Body");
		m_wNotice = FindWrappedText(contentRoot, "Notice");
		m_wWideNotice = FindWrappedText(contentRoot, "WideNotice");
		m_wActivity = TextWidget.Cast(contentRoot.FindAnyWidget("Activity"));

		Widget ring = contentRoot.FindAnyWidget("Border");
		if (ring)
			ring.SetColor(RingColor());

		TextWidget mark = TextWidget.Cast(contentRoot.FindAnyWidget("BankMark"));
		if (mark)
			mark.SetText(ELIFE_PhoneStyle.CURRENCY_SYMBOL);

		m_wMachineId = TextWidget.Cast(contentRoot.FindAnyWidget("MachineId"));
		if (m_wMachineId && m_ATM)
			m_wMachineId.SetText(m_ATM.GetMachineId());

		m_aSteps.Clear();
		array<string> stepNames = {"StepAmount", "StepConfirm", "StepDone"};
		foreach (string stepName : stepNames)
		{
			TextWidget step = TextWidget.Cast(contentRoot.FindAnyWidget(stepName));
			if (step)
				m_aSteps.Insert(step);
		}

		if (m_aSteps.Count() == 3)
		{
			m_aSteps[0].SetText("#ELIFE-ATM_Step_Amount");
			m_aSteps[1].SetText("#ELIFE-ATM_Step_Confirm");
			m_aSteps[2].SetText("#ELIFE-ATM_Step_Done");
		}

		m_eEnteredScreen = -1;
		ApplyStyle(contentRoot);

		BuildLabels(contentRoot.FindAnyWidget("ScreenFrame"));
		BuildAccountDetail(contentRoot, "AccountDetail1", BUTTON_CENTRE_1);
		BuildAccountDetail(contentRoot, "AccountDetail2", BUTTON_CENTRE_2);
		BuildAccountDetail(contentRoot, "AccountDetail3", BUTTON_CENTRE_3);

		//! A late joiner may never have had a mirror update to roll its stand-ins from.
		if (m_ATM && !IsLocalOccupant() && m_eResult == ELIFE_EATMReason.NONE)
			ApplyMirror();

		Render();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Sizes and resting colours; the layout's own values are only the editor preview.
	protected void ApplyStyle(notnull Widget root)
	{
		Color primary = ELIFE_PhoneStyle.TextPrimary();
		Color secondary = ELIFE_PhoneStyle.TextSecondary();

		ELIFE_PhoneStyle.SetColorOf(root, "Background", ELIFE_PhoneStyle.Ink());
		ELIFE_PhoneStyle.SetColorOf(root, "StripHairline", ELIFE_PhoneStyle.Hairline());

		ELIFE_PhoneStyle.SetTypeOf(root, "Figure", TYPE_FIGURE, true, primary);
		if (m_wFigure)
			m_wFigure.SetMinFontSize(TYPE_FIGURE_MIN);

		ELIFE_PhoneStyle.SetTypeOf(root, "Heading", TYPE_HEADING, true, primary);
		ELIFE_PhoneStyle.SetTypeOf(root, "Activity", TYPE_HEADING, true, Accent());

		ELIFE_PhoneStyle.SetTypeOf(root, "BankMark", TYPE_BODY, true, primary);
		ELIFE_PhoneStyle.SetTypeOf(root, "Body", TYPE_BODY, false, primary);
		ELIFE_PhoneStyle.SetTypeOf(root, "AccountNumber", TYPE_BODY, false, secondary);
		ELIFE_PhoneStyle.SetTypeOf(root, "Caption", TYPE_BODY, false, secondary);
		ELIFE_PhoneStyle.SetTypeOf(root, "Line1Label", TYPE_BODY, false, secondary);
		ELIFE_PhoneStyle.SetTypeOf(root, "Line2Label", TYPE_BODY, false, secondary);
		ELIFE_PhoneStyle.SetTypeOf(root, "Line1Value", TYPE_BODY, true, primary);
		ELIFE_PhoneStyle.SetTypeOf(root, "Line2Value", TYPE_BODY, true, primary);
		ELIFE_PhoneStyle.SetTypeOf(root, "Notice", TYPE_BODY, false, ELIFE_PhoneStyle.Negative());
		ELIFE_PhoneStyle.SetTypeOf(root, "WideNotice", TYPE_BODY, false, ELIFE_PhoneStyle.Negative());

		ELIFE_PhoneStyle.SetTypeOf(root, "BankName", TYPE_SECONDARY, false, primary);
		ELIFE_PhoneStyle.SetTypeOf(root, "PageMark", TYPE_SECONDARY, false, secondary);
		ELIFE_PhoneStyle.SetTypeOf(root, "MachineId", TYPE_SECONDARY, false, secondary);
		ELIFE_PhoneStyle.SetTypeOf(root, "StepAmount", TYPE_SECONDARY, false, ELIFE_PhoneStyle.TextTertiary());
		ELIFE_PhoneStyle.SetTypeOf(root, "StepConfirm", TYPE_SECONDARY, false, ELIFE_PhoneStyle.TextTertiary());
		ELIFE_PhoneStyle.SetTypeOf(root, "StepDone", TYPE_SECONDARY, false, ELIFE_PhoneStyle.TextTertiary());
		ELIFE_PhoneStyle.SetTypeOf(root, "AccountDetail1", TYPE_SECONDARY, false, secondary);
		ELIFE_PhoneStyle.SetTypeOf(root, "AccountDetail2", TYPE_SECONDARY, false, secondary);
		ELIFE_PhoneStyle.SetTypeOf(root, "AccountDetail3", TYPE_SECONDARY, false, secondary);
	}

	//------------------------------------------------------------------------------------------------
	protected TextWidget FindWrappedText(notnull Widget root, string name)
	{
		TextWidget text = TextWidget.Cast(root.FindAnyWidget(name));
		if (text)
			text.SetTextWrapping(true);

		return text;
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildLabels(Widget frame)
	{
		m_aLabels.Clear();
		if (!frame)
			return;

		BuildLabel(frame, BUTTON_L1, false, BUTTON_CENTRE_1);
		BuildLabel(frame, BUTTON_L2, false, BUTTON_CENTRE_2);
		BuildLabel(frame, BUTTON_L3, false, BUTTON_CENTRE_3);
		BuildLabel(frame, BUTTON_L4, false, BUTTON_CENTRE_4);
		BuildLabel(frame, BUTTON_R1, true, BUTTON_CENTRE_1);
		BuildLabel(frame, BUTTON_R2, true, BUTTON_CENTRE_2);
		BuildLabel(frame, BUTTON_R3, true, BUTTON_CENTRE_3);
		BuildLabel(frame, BUTTON_R4, true, BUTTON_CENTRE_4);
	}

	//------------------------------------------------------------------------------------------------
	//! Hugs its edge and centres on the button, however wide the localized text comes out.
	protected void BuildLabel(notnull Widget frame, string id, bool rightEdge, float centreY)
	{
		Widget root = GetGame().GetWorkspace().CreateWidgets(LAYOUT_SIDE_LABEL, frame);
		if (!root)
			return;

		TextWidget text = TextWidget.Cast(root.FindAnyWidget("Text"));
		Widget tab = root.FindAnyWidget("Tab");
		if (!text || !tab)
		{
			root.RemoveFromHierarchy();
			return;
		}

		ELIFE_PhoneStyle.SetTypeOf(root, "Text", TYPE_BODY, false);
		FrameSlot.SetSizeToContent(root, true);
		if (rightEdge)
		{
			FrameSlot.SetAnchor(root, 1, 0);
			FrameSlot.SetAlignment(root, 1, 0.5);
			FrameSlot.SetPos(root, -ELIFE_PhoneStyle.SPACE_2, centreY);
		}
		else
		{
			FrameSlot.SetAnchor(root, 0, 0);
			FrameSlot.SetAlignment(root, 0, 0.5);
			FrameSlot.SetPos(root, ELIFE_PhoneStyle.SPACE_2, centreY);
		}

		ELIFE_ATMSideLabel label = new ELIFE_ATMSideLabel();
		label.m_sId = id;
		label.m_wRoot = root;
		label.m_wTab = tab;
		label.m_wText = text;
		m_aLabels.Insert(label);
	}

	//------------------------------------------------------------------------------------------------
	//! Under its button's tab, aligned with the label text inside it.
	protected void BuildAccountDetail(notnull Widget root, string name, float centreY)
	{
		TextWidget detail = TextWidget.Cast(root.FindAnyWidget(name));
		if (!detail)
			return;

		FrameSlot.SetSizeToContent(detail, true);
		FrameSlot.SetAnchor(detail, 1, 0);
		FrameSlot.SetAlignment(detail, 1, 0.5);
		FrameSlot.SetPos(detail, -(ELIFE_PhoneStyle.SPACE_2 + ELIFE_PhoneStyle.SPACE_3), centreY + ACCOUNT_DETAIL_OFFSET);
		m_aAccountDetails.Insert(detail);
	}

	//------------------------------------------------------------------------------------------------
	protected override void OnUnbound()
	{
		super.OnUnbound();

		m_wStackArea = null;
		m_wRows = null;
		m_wAccountNumber = null;
		m_wPageMark = null;
		m_wMachineId = null;
		m_aSteps.Clear();
		StopMotion();
		m_wBankName = null;
		m_aAccountDetails.Clear();
		m_wHeading = null;
		m_wLine1 = null;
		m_wLine1Label = null;
		m_wLine1Value = null;
		m_wLine2 = null;
		m_wLine2Label = null;
		m_wLine2Value = null;
		m_wCaption = null;
		m_wFigure = null;
		m_wBody = null;
		m_wNotice = null;
		m_wWideNotice = null;
		m_wActivity = null;
		StopActivity();
		m_aLabels.Clear();
	}

	//------------------------------------------------------------------------------------------------
	protected void Render()
	{
		if (!m_ATM)
			return;

		ELIFE_EATMScreen screen = ResolveScreen();
		SendMirror(screen);

		if (!m_wStackArea || !m_wRows)
			return;

		ResetContent();
		RenderStrip(screen);
		RenderContent(screen);
		UpdateActivity(screen);
		EnterScreen(screen);

		//! A bystander sees the occupant's labels too, but only a session has a menu to press them with.
		if (m_bSessionOpen || (!IsLocalOccupant() && m_eResult == ELIFE_EATMReason.NONE))
			RenderLabels(screen);

		PaintLabels();
	}

	//------------------------------------------------------------------------------------------------
	protected void ResetContent()
	{
		foreach (ELIFE_ATMSideLabel label : m_aLabels)
		{
			label.m_wText.SetText("");
			label.m_bEnabled = false;
		}

		m_wHeading.SetVisible(false);
		m_wAccountNumber.SetVisible(false);
		m_wLine1.SetVisible(false);
		m_wLine2.SetVisible(false);
		m_wCaption.SetVisible(false);
		m_wFigure.SetVisible(false);
		m_wBody.SetVisible(false);
		m_wBody.SetColor(ELIFE_PhoneStyle.TextPrimary());
		m_wNotice.SetVisible(false);
		m_wWideNotice.SetVisible(false);
		m_wActivity.SetVisible(false);
		foreach (TextWidget detail : m_aAccountDetails)
		{
			detail.SetVisible(false);
		}

		m_wRows.SetVisible(false);
		ClearRows();
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearRows()
	{
		Widget child = m_wRows.GetChildren();
		while (child)
		{
			Widget next = child.GetSibling();
			child.RemoveFromHierarchy();
			child = next;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void RenderStrip(ELIFE_EATMScreen screen)
	{
		m_wBankName.SetText(m_ATM.GetBankName());

		if (screen == ELIFE_EATMScreen.TRANSACTIONS)
			m_wPageMark.SetTextFormat("#ELIFE-ATM_Tx_Page", m_iTransactionPage + 1, GetTransactionPageCount());
		else
			m_wPageMark.SetText("");

		//! The steps take the machine id's place while a booking runs.
		int current = GetBookingStep(screen);
		if (m_wMachineId)
			m_wMachineId.SetVisible(current < 0);

		foreach (int i, TextWidget step : m_aSteps)
		{
			step.SetVisible(current >= 0);
			if (i == current)
				step.SetColor(Accent());
			else
				step.SetColor(ELIFE_PhoneStyle.TextTertiary());
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Amount, Confirm or Done, or -1 outside a booking. A result without an amount is a refused session, not a booking.
	protected int GetBookingStep(ELIFE_EATMScreen screen)
	{
		switch (screen)
		{
			case ELIFE_EATMScreen.AMOUNT:
			case ELIFE_EATMScreen.OTHER_AMOUNT:
			case ELIFE_EATMScreen.COUNTING:
				return 0;
			case ELIFE_EATMScreen.CONFIRM:
			case ELIFE_EATMScreen.PROCESSING:
			case ELIFE_EATMScreen.CHECKING:
				return 1;
			case ELIFE_EATMScreen.RESULT:
				if (m_iResultAmount > 0)
					return 2;

				break;
		}

		return -1;
	}

	//------------------------------------------------------------------------------------------------
	//! Every new page fades in and rises into place; the counting page also counts up to the carried sum.
	protected void EnterScreen(ELIFE_EATMScreen screen)
	{
		if (screen == m_eEnteredScreen)
			return;

		m_eEnteredScreen = screen;
		m_iEnterStartTick = System.GetTickCount();
		if (m_Interact)
			m_Interact.ResetFocus();

		StepMotion();

		if (!m_bMotionRunning)
		{
			m_bMotionRunning = true;
			GetGame().GetCallqueue().CallLater(StepMotion, ELIFE_PhoneStyle.TICK_MS, true);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void StepMotion()
	{
		if (!m_wStackArea || !m_wRows)
		{
			StopMotion();
			return;
		}

		float elapsed = System.GetTickCount() - m_iEnterStartTick;
		float enter = EaseOut(elapsed / ELIFE_PhoneStyle.DURATION_STATE_MS);
		m_wStackArea.SetOpacity(enter);
		m_wRows.SetOpacity(enter);
		FrameSlot.SetPos(m_wStackArea, m_fStackLeft, m_fStackTop + (1 - enter) * ENTER_RISE);

		bool counting = m_eEnteredScreen == ELIFE_EATMScreen.COUNTING && m_wFigure && m_wFigure.IsVisible();
		float count = EaseOut(elapsed / COUNT_MIN_MS);
		if (counting)
			m_wFigure.SetText(Whole(GetCountShown(count)));

		if (enter >= 1 && (!counting || count >= 1))
			StopMotion();
	}

	//------------------------------------------------------------------------------------------------
	protected int GetCountShown(float progress)
	{
		int target = Math.Min(GetCarriedCash(), ELIFE_ATMComponent.MAX_BOOKING_AMOUNT);
		if (progress >= 1)
			return target;

		return Math.Floor(target * progress / COUNT_STEP) * COUNT_STEP;
	}

	//------------------------------------------------------------------------------------------------
	protected void StopMotion()
	{
		GetGame().GetCallqueue().Remove(StepMotion);
		m_bMotionRunning = false;
	}

	//------------------------------------------------------------------------------------------------
	//! Cubic ease-out, clamped: fast start, soft landing, no overshoot.
	protected float EaseOut(float t)
	{
		t = Math.Clamp(t, 0, 1);
		float rest = 1 - t;
		return 1 - rest * rest * rest;
	}

	//------------------------------------------------------------------------------------------------
	protected int GetTransactionPageCount()
	{
		if (!m_Statement || m_Statement.items.IsEmpty())
			return 1;

		return (m_Statement.items.Count() + PAGE_ROW_COUNT - 1) / PAGE_ROW_COUNT;
	}

	//------------------------------------------------------------------------------------------------
	//! Dots on every page that's waiting for an answer. The page keeps its text, so nothing else moves.
	protected void UpdateActivity(ELIFE_EATMScreen screen)
	{
		if (screen != ELIFE_EATMScreen.LOADING && screen != ELIFE_EATMScreen.PROCESSING && screen != ELIFE_EATMScreen.CHECKING && screen != ELIFE_EATMScreen.COUNTING)
		{
			StopActivity();
			return;
		}

		if (!m_bActivityRunning)
		{
			m_bActivityRunning = true;
			m_iActivityDots = 0;
			GetGame().GetCallqueue().CallLater(StepActivity, ACTIVITY_STEP_MS, true);
		}

		PaintActivity();
		m_wActivity.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	protected void StopActivity()
	{
		GetGame().GetCallqueue().Remove(StepActivity);
		m_bActivityRunning = false;
	}

	//------------------------------------------------------------------------------------------------
	protected void StepActivity()
	{
		m_iActivityDots++;
		if (m_iActivityDots > ACTIVITY_DOTS)
			m_iActivityDots = 1;

		PaintActivity();
	}

	//------------------------------------------------------------------------------------------------
	//! Never empty, as an empty text has no height and the page would jump when the first dot appears.
	protected void PaintActivity()
	{
		if (!m_wActivity)
			return;

		string dots = ACTIVITY_GAP;
		for (int i = 0; i < m_iActivityDots; i++)
		{
			dots += ACTIVITY_DOT + ACTIVITY_GAP;
		}

		m_wActivity.SetText(dots);
	}

	//------------------------------------------------------------------------------------------------
	protected void RenderContent(ELIFE_EATMScreen screen)
	{
		switch (screen)
		{
			case ELIFE_EATMScreen.IDLE:
				PlaceStack(CONTENT_LEFT, CONTENT_TOP, CONTENT_WIDTH, CONTENT_BOTTOM);
				if (m_ATM.OffersDeposits())
					ShowText(m_wHeading, "#ELIFE-ATM_Idle_Branch");
				else
					ShowText(m_wHeading, "#ELIFE-ATM_Idle_Withdraw");

				ShowText(m_wBody, "#ELIFE-ATM_Ready");
				m_wBody.SetColor(ELIFE_PhoneStyle.TextSecondary());
				return;
			case ELIFE_EATMScreen.LOADING:
				PlaceStack(CONTENT_LEFT, CONTENT_TOP, CONTENT_WIDTH, CONTENT_BOTTOM);
				ShowText(m_wHeading, "#ELIFE-ATM_Loading");
				return;
			case ELIFE_EATMScreen.NO_ACCOUNT:
				PlaceStack(CONTENT_LEFT, BELOW_R1_TOP, CONTENT_WIDTH, ABOVE_R4_BOTTOM);
				ShowText(m_wBody, "#ELIFE-ATM_NoAccount");
				return;
			case ELIFE_EATMScreen.LOAD_FAILED:
				//! Narrower when the switch label shares the row.
				if (HasSeveralAccounts())
					PlaceStack(CONTENT_LEFT, BELOW_R1_TOP, CONFIRM_WIDTH, ABOVE_R4_BOTTOM);
				else
					PlaceStack(CONTENT_LEFT, BELOW_R1_TOP, CONTENT_WIDTH, ABOVE_R4_BOTTOM);

				ShowText(m_wBody, "#ELIFE-ATM_LoadFailed");
				return;
			case ELIFE_EATMScreen.CHOOSE_ACCOUNT:
				PlaceStack(CONTENT_LEFT, BELOW_R2_TOP, ACCOUNTS_TEXT_WIDTH, ABOVE_R4_BOTTOM);
				ShowText(m_wHeading, "#ELIFE-ATM_Accounts_Title");
				return;
			case ELIFE_EATMScreen.HOME:
				PlaceStack(CONTENT_LEFT, BELOW_R1_TOP, CONFIRM_WIDTH, CONTENT_BOTTOM);
				if (HasAccount())
					ShowText(m_wAccountNumber, m_Statement.account.GetDisplayNumber());

				ShowText(m_wCaption, "#ELIFE-ATM_Balance");
				ShowFigure(GetBalanceText(), Accent());
				return;
			case ELIFE_EATMScreen.TRANSACTIONS:
				RenderTransactions();
				return;
			case ELIFE_EATMScreen.AMOUNT:
				PlaceStack(AMOUNT_LEFT, CONTENT_TOP, AMOUNT_WIDTH, AMOUNT_STACK_BOTTOM);
				ShowText(m_wHeading, ForKind("#ELIFE-ATM_Withdraw_Title", "#ELIFE-ATM_Deposit_Title"));
				if (IsDeposit())
					ShowLine(m_wLine1, m_wLine1Label, m_wLine1Value, "#ELIFE-ATM_CashCarried", Whole(GetCarriedCash()));
				else
					ShowLine(m_wLine1, m_wLine1Label, m_wLine1Value, "#ELIFE-ATM_Balance", GetBalanceText());

				ShowLine(m_wLine2, m_wLine2Label, m_wLine2Value, "#ELIFE-ATM_Limit", Whole(ELIFE_ATMComponent.MAX_BOOKING_AMOUNT));
				ShowRefusal(m_eNotice, m_wWideNotice);
				return;
			case ELIFE_EATMScreen.OTHER_AMOUNT:
				RenderOtherAmount();
				return;
			case ELIFE_EATMScreen.CONFIRM:
				PlaceStack(CONTENT_LEFT, CONTENT_TOP, CONFIRM_WIDTH, CONTENT_BOTTOM);
				RenderConfirm();
				return;
			case ELIFE_EATMScreen.PROCESSING:
				RenderProcessing();
				return;
			case ELIFE_EATMScreen.COUNTING:
				PlaceStack(CONTENT_LEFT, CONTENT_TOP, CONTENT_WIDTH, ABOVE_R4_BOTTOM);
				ShowText(m_wHeading, "#ELIFE-ATM_Counting");

				//! Only the occupant knows the sum; a bystander sees the dots alone. A re-render keeps the count where it is.
				if (IsLocalOccupant())
				{
					float progress;
					if (m_eEnteredScreen == ELIFE_EATMScreen.COUNTING)
					{
						float countElapsed = System.GetTickCount() - m_iEnterStartTick;
						progress = EaseOut(countElapsed / COUNT_MIN_MS);
					}

					ShowFigure(Whole(GetCountShown(progress)), Accent());
				}

				return;
			case ELIFE_EATMScreen.CHECKING:
				PlaceStack(CONTENT_LEFT, CONTENT_TOP, CONTENT_WIDTH, CONTENT_BOTTOM);
				ShowText(m_wBody, ForKind("#ELIFE-ATM_Checking", "#ELIFE-ATM_Checking_Deposit"));
				return;
			case ELIFE_EATMScreen.RESULT:
				RenderResult();
				return;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void RenderTransactions()
	{
		if (m_Statement.items.IsEmpty())
		{
			PlaceStack(CONTENT_LEFT, CONTENT_TOP, PAGE_ROWS_WIDTH, CONTENT_BOTTOM);
			ShowText(m_wBody, "#ELIFE-ATM_Tx_Empty");
			return;
		}

		ShowRows(m_iTransactionPage * PAGE_ROW_COUNT, PAGE_ROW_COUNT, PAGE_ROWS_TOP, PAGE_ROWS_WIDTH);
	}

	//------------------------------------------------------------------------------------------------
	protected void RenderOtherAmount()
	{
		PlaceStack(CONTENT_LEFT, CONTENT_TOP, CONFIRM_WIDTH, ABOVE_R4_BOTTOM);
		ShowText(m_wHeading, "#ELIFE-ATM_Other_Title");

		string hint = "#ELIFE-ATM_Other_Hint";
		if (IsPadMode())
		{
			ShowFigure(GetWheelMarkup(), new Color(1, 1, 1, 1));
			hint = "#ELIFE-ATM_Other_Hint_Pad";
		}
		else if (m_sTyped == "")
		{
			ShowFigure(Whole(0), ELIFE_PhoneStyle.TextTertiary());
		}
		else
		{
			ShowFigure(Whole(m_sTyped.ToInt()), ELIFE_PhoneStyle.TextPrimary());
		}

		m_wBody.SetTextFormat(hint, Whole(ELIFE_ATMComponent.MAX_BOOKING_AMOUNT));
		m_wBody.SetColor(ELIFE_PhoneStyle.TextSecondary());
		m_wBody.SetVisible(true);
		ShowRefusal(m_eNotice);
	}

	//------------------------------------------------------------------------------------------------
	//! After reopening mid-withdrawal, this client no longer knows the amount.
	protected void RenderProcessing()
	{
		PlaceStack(CONTENT_LEFT, CONTENT_TOP, CONTENT_WIDTH, CONTENT_BOTTOM);

		if (m_iBookingAmount == 0)
		{
			ShowText(m_wHeading, ForKind("#ELIFE-ATM_Processing_Generic", "#ELIFE-ATM_Processing_Deposit_Generic"));
			return;
		}

		m_wHeading.SetTextFormat(ForKind("#ELIFE-ATM_Processing", "#ELIFE-ATM_Processing_Deposit"), Whole(m_iBookingAmount));
		m_wHeading.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	protected void RenderResult()
	{
		PlaceStack(CONTENT_LEFT, CONTENT_TOP, CONTENT_WIDTH, ABOVE_R4_BOTTOM);

		ELIFE_EATMReason result = GetShownResult();
		if (result != ELIFE_EATMReason.PAID)
		{
			ShowRefusal(result);
			return;
		}

		ShowLine(m_wLine1, m_wLine1Label, m_wLine1Value, ForKind("#ELIFE-ATM_PaidOut", "#ELIFE-ATM_Deposited"), Whole(m_iResultAmount));
		ShowLine(m_wLine2, m_wLine2Label, m_wLine2Value, "#ELIFE-ATM_Fee", OrUnknown(m_sResultFee));
		ShowText(m_wCaption, ForKind("#ELIFE-ATM_RemainingBalance", "#ELIFE-ATM_NewBalance"));
		ShowFigure(GetResultBalanceText(), Accent());

		ShowText(m_wHeading, ForKind("#ELIFE-ATM_Complete", "#ELIFE-ATM_Deposit_Complete"));
	}

	//------------------------------------------------------------------------------------------------
	//! The cash that changes hands is the figure: what lands in the inventory, or what was counted in.
	protected void RenderConfirm()
	{
		ShowText(m_wCaption, ForKind("#ELIFE-ATM_Confirm_Receive", "#ELIFE-ATM_Counted"));
		ShowFigure(Whole(m_iQuoteAmount), Accent());

		//! The exact fee is only known once booked.
		if (!ELIFE_PhoneBankingService.IsFreeOfCharge(m_sQuoteFeeTerms))
			ShowLine(m_wLine1, m_wLine1Label, m_wLine1Value, "#ELIFE-ATM_Fee", m_sQuoteFeeTerms);
	}

	//------------------------------------------------------------------------------------------------
	protected void RenderLabels(ELIFE_EATMScreen screen)
	{
		switch (screen)
		{
			case ELIFE_EATMScreen.COUNTING:
				SetLabel(BUTTON_R4, "#ELIFE-ATM_Action_Cancel");
				return;
			case ELIFE_EATMScreen.NO_ACCOUNT:
			case ELIFE_EATMScreen.LOAD_FAILED:
				SetLabel(BUTTON_R1, "#ELIFE-ATM_Action_TryAgain");
				if (HasSeveralAccounts())
					SetLabel(BUTTON_R3, "#ELIFE-ATM_Action_SwitchAccount");

				SetLabel(BUTTON_R4, "#ELIFE-ATM_Action_Exit");
				return;
			case ELIFE_EATMScreen.CHOOSE_ACCOUNT:
				SetAccountLabels();
				SetLabel(BUTTON_R4, "#ELIFE-ATM_Action_Exit");
				return;
			case ELIFE_EATMScreen.HOME:
				if (m_ATM.OffersDeposits())
					SetLabel(BUTTON_L1, "#ELIFE-ATM_Action_Deposit", GetCarriedCash() > 0);

				SetLabel(BUTTON_R1, "#ELIFE-ATM_Action_Withdraw");
				SetLabel(BUTTON_R2, "#ELIFE-ATM_Action_Transactions");
				if (HasSeveralAccounts())
					SetLabel(BUTTON_R3, "#ELIFE-ATM_Action_SwitchAccount");

				SetLabel(BUTTON_R4, "#ELIFE-ATM_Action_Exit");
				return;
			case ELIFE_EATMScreen.TRANSACTIONS:
				if (HasNewerPage())
					SetLabel(BUTTON_R1, "#ELIFE-ATM_Action_Newer");

				if (HasOlderPage())
					SetLabel(BUTTON_R2, "#ELIFE-ATM_Action_Older");

				SetLabel(BUTTON_R4, "#ELIFE-ATM_Action_Back");
				return;
			case ELIFE_EATMScreen.AMOUNT:
				SetPresetLabel(BUTTON_L1);
				SetPresetLabel(BUTTON_L2);
				SetPresetLabel(BUTTON_L3);
				SetPresetLabel(BUTTON_L4);
				SetPresetLabel(BUTTON_R1);
				SetPresetLabel(BUTTON_R2);
				SetLabel(BUTTON_R3, "#ELIFE-ATM_Action_OtherAmount");
				SetLabel(BUTTON_R4, "#ELIFE-ATM_Action_Back");
				return;
			case ELIFE_EATMScreen.OTHER_AMOUNT:
				//! Not on R4, where the next page has its Confirm: a double press must not skip the fee.
				SetLabel(BUTTON_R3, "#ELIFE-ATM_Action_Confirm", m_sTyped != "");
				SetLabel(BUTTON_R4, "#ELIFE-ATM_Action_Back");
				return;
			case ELIFE_EATMScreen.CONFIRM:
				if (m_bWantReceipt)
					SetLabel(BUTTON_R2, "#ELIFE-ATM_Action_ReceiptYes");
				else
					SetLabel(BUTTON_R2, "#ELIFE-ATM_Action_ReceiptNo");

				SetLabel(BUTTON_L4, "#ELIFE-ATM_Action_Cancel");
				SetLabel(BUTTON_R3, "#ELIFE-ATM_Action_ChangeAmount");
				SetLabel(BUTTON_R4, "#ELIFE-ATM_Action_Confirm");
				return;
			case ELIFE_EATMScreen.RESULT:
				if (GetShownResult() != ELIFE_EATMReason.PAID)
					SetLabel(BUTTON_R4, "#ELIFE-ATM_Action_Done");

				return;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The number on the label, the bank and balance under it. Bystanders get stand-ins for number and balance.
	protected void SetAccountLabels()
	{
		if (!m_Accounts)
			return;

		foreach (int slot, string button : m_aAccountButtons)
		{
			int index = m_iAccountPage * ACCOUNTS_PER_PAGE + slot;
			if (index >= m_Accounts.items.Count())
				break;

			ELIFE_ATMAccountChoice account = m_Accounts.items[index];
			SetLabel(button, ELIFE_PhoneBankingService.GroupIban(account.number));

			if (!m_aAccountDetails.IsIndexValid(slot))
				continue;

			string detailText = OrUnknown(account.balance);
			if (account.bankName != "")
				detailText = account.bankName + META_SEPARATOR + detailText;

			m_aAccountDetails[slot].SetText(detailText);
			m_aAccountDetails[slot].SetVisible(true);
		}

		if (m_iAccountPage > 0)
			SetLabel(BUTTON_L1, "#ELIFE-ATM_Action_Previous");

		if (HasNextAccountPage())
			SetLabel(BUTTON_L2, "#ELIFE-ATM_Action_Next");
	}

	//------------------------------------------------------------------------------------------------
	protected void SetPresetLabel(string id)
	{
		int preset = GetPreset(id);
		if (preset <= 0)
			return;

		SetLabel(id, Whole(preset), IsAffordable(preset));
	}

	//------------------------------------------------------------------------------------------------
	protected void SetLabel(string id, string text, bool enabled = true)
	{
		foreach (ELIFE_ATMSideLabel label : m_aLabels)
		{
			if (label.m_sId != id)
				continue;

			label.m_wText.SetText(text);
			label.m_bEnabled = enabled;
			return;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Branch ATMs carry the Bank gold, all others the logo blue.
	protected Color Accent()
	{
		if (m_ATM && m_ATM.IsBranchMachine())
			return ELIFE_PhoneStyle.AccentBank();

		return ELIFE_PhoneStyle.AccentAtm();
	}

	//------------------------------------------------------------------------------------------------
	protected Color AccentDeep()
	{
		if (m_ATM && m_ATM.IsBranchMachine())
			return ELIFE_PhoneStyle.AccentDeepBank();

		return ELIFE_PhoneStyle.AccentDeepAtm();
	}

	//------------------------------------------------------------------------------------------------
	//! Light text fails on the deep gold (3.2:1), dark ink passes (5.3:1); on the deep blue it's the reverse.
	protected Color OnAccentDeep()
	{
		if (m_ATM && m_ATM.IsBranchMachine())
			return ELIFE_PhoneStyle.Ink();

		return ELIFE_PhoneStyle.TextPrimary();
	}

	//------------------------------------------------------------------------------------------------
	//! The colour of the E badge on this machine's case, so screen and case read as one machine.
	protected Color RingColor()
	{
		if (m_ATM && m_ATM.IsBranchMachine())
			return ELIFE_PhoneStyle.AccentBank();

		return ELIFE_PhoneStyle.AccentDeepAtm();
	}

	//------------------------------------------------------------------------------------------------
	protected void PaintLabels()
	{
		//! A tab only behind what can be pressed; a disabled label keeps its text so the choice stays visible.
		foreach (ELIFE_ATMSideLabel label : m_aLabels)
		{
			label.m_wRoot.SetVisible(label.m_wText.GetText() != "");
			label.m_wTab.SetVisible(label.m_bEnabled);

			if (!label.m_bEnabled)
			{
				label.m_wText.SetColor(ELIFE_PhoneStyle.TextTertiary());
			}
			else if (label.m_sId == m_sPressedButton)
			{
				label.m_wTab.SetColor(Accent());
				label.m_wText.SetColor(ELIFE_PhoneStyle.Ink());
			}
			else if (label.m_sId == m_sHoveredButton)
			{
				label.m_wTab.SetColor(AccentDeep());
				label.m_wText.SetColor(OnAccentDeep());
			}
			else
			{
				label.m_wTab.SetColor(ELIFE_PhoneStyle.SurfaceRaised());
				label.m_wText.SetColor(ELIFE_PhoneStyle.TextPrimary());
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Side buttons with an enabled label. While typing an amount the D-pad turns the wheel instead.
	override bool IsButtonNavigable(string id)
	{
		if (!m_bSessionOpen || ResolveScreen() == ELIFE_EATMScreen.OTHER_AMOUNT)
			return false;

		foreach (ELIFE_ATMSideLabel label : m_aLabels)
		{
			if (label.m_sId == id)
				return label.m_bEnabled;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! The right column holds each page's main actions, top first.
	override string GetDefaultFocusButton()
	{
		array<string> order = {BUTTON_R1, BUTTON_R2, BUTTON_R3, BUTTON_R4, BUTTON_L1, BUTTON_L2, BUTTON_L3, BUTTON_L4};
		foreach (string id : order)
		{
			if (IsButtonNavigable(id))
				return id;
		}

		return "";
	}

	//------------------------------------------------------------------------------------------------
	//! Left and right pick the place, up and down roll its digit 0-9 without carrying, like a combination lock.
	override bool HandleNavigate(int dx, int dy)
	{
		if (!m_bSessionOpen || ResolveScreen() != ELIFE_EATMScreen.OTHER_AMOUNT)
			return false;

		int digits = GetWheelDigits();
		if (dx != 0)
		{
			m_iWheelPos = Math.Max(0, Math.Min(m_iWheelPos + dx, digits - 1));
		}
		else
		{
			//! ToInt() throws on an empty string.
			int value;
			if (m_sTyped != "")
				value = m_sTyped.ToInt();

			int place = 1;
			for (int i = m_iWheelPos + 1; i < digits; i++)
			{
				place *= 10;
			}

			int digit = (value / place) % 10;
			int rolled = (digit - dy + 10) % 10;
			value += (rolled - digit) * place;

			m_sTyped = "";
			if (value > 0)
				m_sTyped = value.ToString();

			m_eNotice = ELIFE_EATMReason.NONE;
			m_iQuoteAsked = 0;
		}

		ELIFE_ATMCharacterComponent link = GetLink();
		if (link)
			SendKeepAlive(link);

		PlaySfx(KEY_SOUND);
		Render();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Only the amount entry has nothing to focus and still something to do: A confirms the wheel's amount.
	override bool HandleSelect()
	{
		if (!m_bSessionOpen || !m_Interact || ResolveScreen() != ELIFE_EATMScreen.OTHER_AMOUNT)
			return false;

		m_Interact.PressButton(KEY_CONFIRM);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! The page's own back or cancel. Mid-booking there is none, so the menu closes and the booking finishes on its own.
	override bool HandleBack()
	{
		if (!m_bSessionOpen || !m_Interact)
			return false;

		ELIFE_EATMScreen screen = ResolveScreen();
		if (screen == ELIFE_EATMScreen.PROCESSING || screen == ELIFE_EATMScreen.CHECKING)
			return false;

		if (screen == ELIFE_EATMScreen.RESULT && m_eResult == ELIFE_EATMReason.PAID)
			return false;

		m_Interact.PressButton(KEY_CANCEL);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsPadMode()
	{
		return m_bSessionOpen && m_Interact && m_Interact.IsPadMode();
	}

	//------------------------------------------------------------------------------------------------
	protected int GetWheelDigits()
	{
		int maxAmount = ELIFE_ATMComponent.MAX_BOOKING_AMOUNT;
		return maxAmount.ToString().Length();
	}

	//------------------------------------------------------------------------------------------------
	//! Every place shown, leading zeros dimmed and the turning one in the accent. Grouped in threes like Whole().
	protected string GetWheelMarkup()
	{
		int digits = GetWheelDigits();
		string padded = m_sTyped;
		while (padded.Length() < digits)
		{
			padded = "0" + padded;
		}

		string lead = ToMarkupColour(ELIFE_PhoneStyle.TextTertiary());
		string value = ToMarkupColour(ELIFE_PhoneStyle.TextPrimary());
		string active = ToMarkupColour(Accent());
		int firstValue = digits - m_sTyped.Length();

		string markup;
		for (int i = 0; i < digits; i++)
		{
			if (i > 0 && (digits - i) % 3 == 0)
				markup += " ";

			string colour = value;
			if (i == m_iWheelPos)
				colour = active;
			else if (i < firstValue)
				colour = lead;

			markup += string.Format("<color rgba=\"%1\">%2</color>", colour, padded.Get(i));
		}

		return markup + " " + ELIFE_PhoneStyle.CURRENCY_SYMBOL;
	}

	//------------------------------------------------------------------------------------------------
	//! Widget colours are linear; markup takes 0-255 sRGB.
	protected string ToMarkupColour(notnull Color color)
	{
		Color srgb = color.Copy();
		srgb.LinearToSRGB();
		int r = Math.Round(srgb.R() * 255);
		int g = Math.Round(srgb.G() * 255);
		int b = Math.Round(srgb.B() * 255);
		return string.Format("%1,%2,%3,255", r, g, b);
	}

	//------------------------------------------------------------------------------------------------
	//! Only enabled labels count, which also rules out blank ones: SetLabel is what enables.
	override string FindButtonAtCanvas(float canvasX, float canvasY)
	{
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return "";

		//! Screen rects are in DPI-scaled pixels, so scale the point once instead of every rect.
		float x = workspace.DPIScale(canvasX);
		float y = workspace.DPIScale(canvasY);
		float padding = workspace.DPIScale(LABEL_HIT_PADDING);

		foreach (ELIFE_ATMSideLabel label : m_aLabels)
		{
			if (!label.m_bEnabled)
				continue;

			float left, top, width, height;
			label.m_wRoot.GetScreenPos(left, top);
			label.m_wRoot.GetScreenSize(width, height);

			if (x >= left - padding && x <= left + width + padding && y >= top - padding && y <= top + height + padding)
				return label.m_sId;
		}

		return "";
	}

	//------------------------------------------------------------------------------------------------
	protected void PlaceStack(float left, float top, float width, float bottom)
	{
		m_fStackLeft = left;
		m_fStackTop = top;
		FrameSlot.SetPos(m_wStackArea, left, top);
		FrameSlot.SetSize(m_wStackArea, width, bottom - top);
	}

	//------------------------------------------------------------------------------------------------
	protected void ShowText(notnull TextWidget widget, string text)
	{
		widget.SetText(text);
		widget.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	protected void ShowFigure(string text, Color color)
	{
		m_wFigure.SetColor(color);
		ShowText(m_wFigure, text);
	}

	//------------------------------------------------------------------------------------------------
	protected void ShowLine(notnull Widget line, notnull TextWidget label, notnull TextWidget value, string labelKey, string valueText)
	{
		label.SetText(labelKey);
		value.SetText(valueText);
		line.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	protected void ShowRefusal(ELIFE_EATMReason reason, TextWidget target = null)
	{
		string key = GetRefusalKey(reason);
		if (key == "")
			return;

		if (!target)
			target = m_wNotice;

		string amount = Whole(m_iResultAmount);
		if (reason == ELIFE_EATMReason.AMOUNT_TOO_HIGH)
			amount = Whole(ELIFE_ATMComponent.MAX_BOOKING_AMOUNT);

		target.SetTextFormat(key, amount);
		target.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	protected string GetRefusalKey(ELIFE_EATMReason reason)
	{
		switch (reason)
		{
			case ELIFE_EATMReason.IN_USE:
				return "#ELIFE-ATM_InUse";
			case ELIFE_EATMReason.OUT_OF_RANGE:
				return "#ELIFE-ATM_Reason_OutOfRange";
			case ELIFE_EATMReason.NOT_OCCUPANT:
				return "#ELIFE-ATM_Reason_NotOccupant";
			case ELIFE_EATMReason.INVALID_AMOUNT:
				return ForKind("#ELIFE-ATM_Reason_InvalidAmount", "#ELIFE-ATM_Reason_InvalidAmount_Deposit");
			case ELIFE_EATMReason.AMOUNT_TOO_HIGH:
				return ForKind("#ELIFE-ATM_Reason_AmountTooHigh", "#ELIFE-ATM_Reason_AmountTooHigh_Deposit");
			case ELIFE_EATMReason.QUOTE_INVALID:
				return "#ELIFE-ATM_Reason_QuoteInvalid";
			case ELIFE_EATMReason.BUSY:
				return "#ELIFE-ATM_Reason_Busy";
			case ELIFE_EATMReason.NO_ACCOUNT:
				return "#ELIFE-ATM_NoAccount";
			case ELIFE_EATMReason.DATA_UNAVAILABLE:
				return "#ELIFE-ATM_Reason_DataUnavailable";
			case ELIFE_EATMReason.INSUFFICIENT_FUNDS:
				return "#ELIFE-ATM_Reason_InsufficientFunds";
			case ELIFE_EATMReason.DENIED:
				return ForKind("#ELIFE-ATM_Reason_Denied", "#ELIFE-ATM_Reason_Denied_Deposit");
			case ELIFE_EATMReason.NOT_WITHDRAWN:
				return "#ELIFE-ATM_Reason_NotWithdrawn";
			case ELIFE_EATMReason.CONTACT_STAFF:
				return ForKind("#ELIFE-ATM_Reason_ContactStaff", "#ELIFE-ATM_Reason_ContactStaff_Deposit");
			case ELIFE_EATMReason.NOT_DEPOSITED:
				return "#ELIFE-ATM_Reason_NotDeposited";
			case ELIFE_EATMReason.NOT_ENOUGH_CASH:
				return "#ELIFE-ATM_Reason_NotEnoughCash";
			case ELIFE_EATMReason.NOT_OFFERED:
				return "#ELIFE-ATM_Reason_NotOffered";
		}

		return "";
	}

	//------------------------------------------------------------------------------------------------
	protected string GetBalanceText()
	{
		if (!HasAccount())
			return UNKNOWN_VALUE;

		return OrUnknown(m_Statement.account.balanceStr);
	}

	//------------------------------------------------------------------------------------------------
	//! The statement from before the withdrawal would show the old balance, so it only counts once refreshed.
	protected string GetResultBalanceText()
	{
		if (m_sResultBalance != "")
			return m_sResultBalance;

		if (m_bStatementAfterResult)
			return GetBalanceText();

		return UNKNOWN_VALUE;
	}

	//------------------------------------------------------------------------------------------------
	//! Rows arrive newest first, redacted or not.
	protected void ShowRows(int first, int count, float top, float width)
	{
		FrameSlot.SetPos(m_wRows, CONTENT_LEFT, top);
		FrameSlot.SetSizeX(m_wRows, width);

		int end = first + count;
		if (end > m_Statement.items.Count())
			end = m_Statement.items.Count();

		for (int i = first; i < end; i++)
		{
			Widget row = GetGame().GetWorkspace().CreateWidgets(LAYOUT_ROW, m_wRows);
			if (!row)
				continue;

			AlignableSlot.SetHorizontalAlign(row, LayoutHorizontalAlign.Stretch);
			ELIFE_PhoneStyle.SetTypeOf(row, "RowKind", TYPE_SECONDARY, false, ELIFE_PhoneStyle.TextPrimary());
			ELIFE_PhoneStyle.SetTypeOf(row, "RowAmount", TYPE_SECONDARY, true);
			ELIFE_PhoneStyle.SetTypeOf(row, "RowMeta", TYPE_SECONDARY, false, ELIFE_PhoneStyle.TextSecondary());
			ELIFE_PhoneStyle.SetColorOf(row, "RowHairline", ELIFE_PhoneStyle.Hairline());
			FillRow(row, m_Statement.items[i]);
		}

		m_wRows.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	protected void FillRow(notnull Widget row, ELIFE_BankAccountTransaction transaction)
	{
		TextWidget kind = TextWidget.Cast(row.FindAnyWidget("RowKind"));
		TextWidget amount = TextWidget.Cast(row.FindAnyWidget("RowAmount"));
		TextWidget meta = TextWidget.Cast(row.FindAnyWidget("RowMeta"));
		if (!transaction || !kind || !amount || !meta)
			return;

		string metaText = ELIFE_PhoneAppBase.FormatDayTime(transaction.occurredAt);
		if (!ELIFE_PhoneBankingService.IsZeroMoney(transaction.feeStr))
			metaText += META_SEPARATOR + WidgetManager.Translate("#ELIFE-ATM_Tx_Fee", transaction.feeStr);

		kind.SetText(GetKindText(transaction.kind));
		//! Already signed from this account's side.
		amount.SetText(transaction.amountStr);
		if (!ELIFE_PhoneBankingService.IsZeroMoney(transaction.amountStr))
		{
			if (transaction.IsOutgoing())
				amount.SetColor(ELIFE_PhoneStyle.Negative());
			else
				amount.SetColor(ELIFE_PhoneStyle.Positive());
		}
		meta.SetText(metaText);
	}

	//------------------------------------------------------------------------------------------------
	//! An unknown kind shows as sent rather than blank.
	protected string GetKindText(string kind)
	{
		if (kind == ELIFE_BankAccountTransaction.KIND_DEPOSITED)
			return "#ELIFE-ATM_Tx_Deposited";

		if (kind == ELIFE_BankAccountTransaction.KIND_WITHDRAWN)
			return "#ELIFE-ATM_Tx_Withdrawn";

		if (kind == ELIFE_BankAccountTransaction.KIND_TRANSFERRED_OUT)
			return "#ELIFE-ATM_Tx_TransferredOut";

		if (kind == ELIFE_BankAccountTransaction.KIND_TRANSFERRED_IN)
			return "#ELIFE-ATM_Tx_TransferredIn";

		return kind;
	}

	//------------------------------------------------------------------------------------------------
	protected string OrUnknown(string text)
	{
		if (text == "")
			return UNKNOWN_VALUE;

		return text;
	}

	//------------------------------------------------------------------------------------------------
	protected string Money(int cents)
	{
		return ELIFE_PhoneBankingService.FormatMoney(cents);
	}

	//------------------------------------------------------------------------------------------------
	protected string Whole(int tolars)
	{
		return ELIFE_PhoneBankingService.FormatWholeMoney(tolars);
	}

	//------------------------------------------------------------------------------------------------
	void ~ELIFE_ATMScreenComponent()
	{
		GetGame().GetCallqueue().Remove(SendHeartbeat);
		GetGame().GetCallqueue().Remove(ClearLingeringResult);
		GetGame().GetCallqueue().Remove(ClearPressed);
		StopActivity();
		StopMotion();
		RemoveResultTimers();
	}
}
