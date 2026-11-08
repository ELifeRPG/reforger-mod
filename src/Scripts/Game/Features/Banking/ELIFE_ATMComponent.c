//------------------------------------------------------------------------------------------------
[EntityEditorProps(category: "ELifeRPG/Banking", description: "Server-authoritative ATM: occupant lock, account data, quotes and bookings")]
class ELIFE_ATMComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! Replicated, so bystanders and late joiners see it too.
enum ELIFE_EATMState
{
	FREE,
	LOADING,
	READY,
	NO_ACCOUNT,
	LOAD_FAILED,
	PROCESSING,
	CHECKING,
	CHOOSE_ACCOUNT
}

//------------------------------------------------------------------------------------------------
enum ELIFE_EATMReason
{
	NONE,
	IN_USE,
	OUT_OF_RANGE,
	NOT_OCCUPANT,
	INVALID_AMOUNT,
	AMOUNT_TOO_HIGH,
	QUOTE_INVALID,
	BUSY,
	NO_ACCOUNT,
	DATA_UNAVAILABLE,
	INSUFFICIENT_FUNDS,
	DENIED,
	NOT_WITHDRAWN,
	PAID,
	CONTACT_STAFF,
	NOT_DEPOSITED,
	NOT_ENOUGH_CASH,
	NOT_OFFERED
}

//------------------------------------------------------------------------------------------------
//! What a quote and the booking after it do. Deposits are a branch service.
enum ELIFE_EATMBookingKind
{
	WITHDRAW,
	DEPOSIT
}

//------------------------------------------------------------------------------------------------
//! One line of the account choice. Built by the server rather than sent as the API's account, which has no bank name.
class ELIFE_ATMAccountChoice : JsonApiStruct
{
	string number;
	//! The Bridge's display text.
	string balance;
	string bankName;

	//------------------------------------------------------------------------------------------------
	void ELIFE_ATMAccountChoice(string accountNumber = "", string accountBalance = "", string accountBankName = "")
	{
		number = accountNumber;
		balance = accountBalance;
		bankName = accountBankName;

		RegV("number");
		RegV("balance");
		RegV("bankName");
	}
}

//------------------------------------------------------------------------------------------------
class ELIFE_ATMAccountList : JsonApiStruct
{
	ref array<ref ELIFE_ATMAccountChoice> items = {};

	//------------------------------------------------------------------------------------------------
	void ELIFE_ATMAccountList()
	{
		RegV("items");
	}
}

//------------------------------------------------------------------------------------------------
//! Wraps the Bridge's bare-array banks response as {"items": <raw>}.
class ELIFE_BankListDto : ELIFE_PhoneRawDto
{
	ref array<ref ELIFE_BankSummary> items = {};

	void ELIFE_BankListDto()
	{
		RegV("items");
	}
}

//------------------------------------------------------------------------------------------------
//! Everything a running booking needs, fixed when it's confirmed. Lives only on the server.
class ELIFE_ATMBooking
{
	ELIFE_EATMBookingKind m_eKind;
	string m_sMachineId;
	int m_iPlayerId;
	IEntity m_Character;
	string m_sCharacterId;
	string m_sAccountNumber;
	int m_iAmount;
	int m_iPaid;
	int m_iDropped;
	//! The Bridge's display text from the booking. Empty until it's booked.
	string m_sFee;
	string m_sNewBalance;
	//! Cash taken from the player for a deposit, held until the booking is settled.
	int m_iTaken;
	ref ELIFE_BankBooking m_Request;

	//------------------------------------------------------------------------------------------------
	bool IsDeposit()
	{
		return m_eKind == ELIFE_EATMBookingKind.DEPOSIT;
	}

	//------------------------------------------------------------------------------------------------
	string Describe()
	{
		//! Two parts, as Format takes at most nine arguments.
		string who = string.Format("%1 %2 player %3 character %4 account %5", typename.EnumToString(ELIFE_EATMBookingKind, m_eKind), m_sMachineId, m_iPlayerId, m_sCharacterId, m_sAccountNumber);
		string booking = "not sent";
		if (m_Request)
			booking = m_Request.Describe();

		return who + string.Format(" amount %1 paid %2 taken %3, booking %4", m_iAmount, m_iPaid, m_iTaken, booking);
	}
}

//------------------------------------------------------------------------------------------------
//! The server owns the prop, so nobody can reach it over RPC: requests arrive and answers leave through
//! ELIFE_ATMCharacterComponent. All API handlers live here, as a handler on a deleted character would be dropped.
class ELIFE_ATMComponent : ScriptComponent
{
	static const int MAX_BOOKING_AMOUNT = 100000;
	//! The processing screen stays at least this long, however fast the answer comes. Only the showing waits, not the money.
	static const int PROCESSING_MIN_MS = 800;
	//! How long the result of a completed booking stays on the screen. Nobody can use the machine until then.
	static const int PAID_RESULT_MS = 4000;

	protected static const int TICK_INTERVAL_MS = 1000;
	//! Without a request or heartbeat for this long, the occupant's game has hung or lost its connection.
	protected static const int HEARTBEAT_TIMEOUT_MS = 20000;
	//! Without a request for this long, the occupant has walked away from the keyboard.
	protected static const int AFK_TIMEOUT_MS = 300000;
	protected static const int QUOTE_LIFETIME_MS = 60000;
	protected static const int REFRESH_MIN_INTERVAL_MS = 2000;

	//! Every account choice is a new load of three API calls.
	protected static const int ACCOUNT_CHANGE_MIN_INTERVAL_MS = 500;

	//! One booking at a time per account, across every ATM on this server. Other servers of the hive can't see it.
	protected static ref array<string> s_aBusyAccounts = {};

	[Attribute("0", UIWidgets.CheckBox, "Stands in a bank branch, which adds the branch-only services. For now that's cash deposits.")]
	protected bool m_bBranchMachine;

	[RplProp(onRplName: "OnOccupantUpdated")]
	protected int m_iOccupantPlayerId;

	[RplProp(onRplName: "OnStateUpdated")]
	protected int m_iState;

	[RplProp()]
	protected bool m_bShowingPaid;

	//! What the occupant's screen shows, for everyone else to draw with stand-in figures. Never a real amount.
	[RplProp(onRplName: "OnMirrorUpdated")]
	protected int m_iMirrorScreen;
	[RplProp(onRplName: "OnMirrorUpdated")]
	protected int m_iMirrorPage;
	[RplProp(onRplName: "OnMirrorUpdated")]
	protected bool m_bMirrorTyped;
	[RplProp(onRplName: "OnMirrorUpdated")]
	protected int m_iMirrorReason;
	[RplProp(onRplName: "OnMirrorUpdated")]
	protected int m_iMirrorKind;

	protected ELIFE_WorldScreenInteractComponent m_Interact;
	protected ELIFE_ATMScreenComponent m_Screen;

	protected IEntity m_Occupant;
	protected string m_sOccupantCharacterId;
	protected int m_iLastRequestTick;
	protected int m_iLastHeartbeatTick;
	protected int m_iLastRefreshTick;
	protected bool m_bClosePending;

	protected ref ELIFE_BaseRestCallback m_LoadCallback;
	protected bool m_bLoadInFlight;
	protected bool m_bLoadStale;
	protected string m_sAccountNumber;
	protected string m_sAccountBankId;
	protected string m_sAccountJson;
	//! Only for refusing a quote early; the backend's refusal is the one that counts.
	protected int m_iLoadedBalanceCents;
	protected string m_sFeeTerms;
	protected string m_sRealJson;
	protected string m_sRedactedJson;

	//! Everyone sees the header, so this is replicated. Empty until an account is chosen.
	[RplProp(onRplName: "OnStateUpdated")]
	protected string m_sBankName;

	protected ref ELIFE_BankAccountListDto m_LoadedAccounts;
	//! The personal accounts of the last load: the only numbers a client may choose from.
	protected ref array<string> m_aAccountNumbers = {};
	protected string m_sChosenAccountNumber;
	protected string m_sRealAccountsJson;
	protected string m_sRedactedAccountsJson;

	protected int m_iNextQuoteId = 1;
	protected int m_iQuoteId;
	protected int m_iLastConsumedQuoteId;
	protected int m_iQuoteAmount;
	protected ELIFE_EATMBookingKind m_eQuoteKind;
	protected int m_iQuoteTick;

	protected ref ELIFE_ATMBooking m_Booking;

	//------------------------------------------------------------------------------------------------
	//! Server-only. For other desks that book on an account (the bank clerk), so they and the ATMs never overlap.
	static bool TryLockAccount(string accountNumber)
	{
		if (s_aBusyAccounts.Contains(accountNumber))
			return false;

		s_aBusyAccounts.Insert(accountNumber);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	static void UnlockAccount(string accountNumber)
	{
		s_aBusyAccounts.RemoveItem(accountNumber);
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		m_Interact = ELIFE_WorldScreenInteractComponent.Cast(owner.FindComponent(ELIFE_WorldScreenInteractComponent));
		m_Screen = ELIFE_ATMScreenComponent.Cast(owner.FindComponent(ELIFE_ATMScreenComponent));
	}

	//------------------------------------------------------------------------------------------------
	//! Built from the placement in decimetres, so every machine and every restart gets the same one with nothing replicated.
	string GetBankName()
	{
		return m_sBankName;
	}

	//------------------------------------------------------------------------------------------------
	string GetMachineId()
	{
		vector position = GetOwner().GetOrigin();
		int x = Math.Round(position[0] * 10);
		int y = Math.Round(position[1] * 10);
		int z = Math.Round(position[2] * 10);
		return string.Format("ATM-%1-%2-%3", x, y, z);
	}

	//------------------------------------------------------------------------------------------------
	bool IsBranchMachine()
	{
		return m_bBranchMachine;
	}

	//------------------------------------------------------------------------------------------------
	bool OffersDeposits()
	{
		return m_bBranchMachine;
	}

	//------------------------------------------------------------------------------------------------
	int GetOccupantPlayerId()
	{
		return m_iOccupantPlayerId;
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_EATMState GetState()
	{
		return m_iState;
	}

	//------------------------------------------------------------------------------------------------
	bool IsShowingPaid()
	{
		return m_bShowingPaid;
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_EATMScreen GetMirrorScreen()
	{
		return m_iMirrorScreen;
	}

	//------------------------------------------------------------------------------------------------
	int GetMirrorPage()
	{
		return m_iMirrorPage;
	}

	//------------------------------------------------------------------------------------------------
	bool IsMirrorTyped()
	{
		return m_bMirrorTyped;
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_EATMReason GetMirrorReason()
	{
		return m_iMirrorReason;
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_EATMBookingKind GetMirrorKind()
	{
		return m_iMirrorKind;
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. Display only, so it needs no reach check, but a client can't claim a page or a payout that doesn't exist.
	void SetMirror(int playerId, notnull IEntity character, int screen, int page, bool typed, int reason, int kind)
	{
		if (!IsOccupant(playerId, character))
			return;

		if (screen < ELIFE_EATMScreen.LOADING || screen > ELIFE_EATMScreen.COUNTING || reason < ELIFE_EATMReason.NONE || reason > ELIFE_EATMReason.NOT_OFFERED)
			return;

		if (kind != ELIFE_EATMBookingKind.WITHDRAW && (kind != ELIFE_EATMBookingKind.DEPOSIT || !m_bBranchMachine))
			return;

		if (reason == ELIFE_EATMReason.PAID && !m_bShowingPaid)
			return;

		m_iMirrorScreen = screen;
		m_iMirrorPage = Math.Max(page, 0);
		m_bMirrorTyped = typed;
		m_iMirrorReason = reason;
		m_iMirrorKind = kind;
		Replication.BumpMe();

		//! onRplName doesn't fire on the authority itself.
		OnMirrorUpdated();
	}

	//------------------------------------------------------------------------------------------------
	//! True if the character is alive and within the screen's use range of its centre.
	bool IsUsableBy(IEntity character)
	{
		return m_Interact && m_Interact.IsUsableBy(character);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. Cached, so late joiners never trigger a fetch or a second redaction.
	string GetRedactedJson()
	{
		return m_sRedactedJson;
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. A repeat from the occupant re-sends the grant, so reopening during a booking works.
	void RequestOpen(int playerId, notnull IEntity character, notnull ELIFE_ATMCharacterComponent link)
	{
		if (m_iOccupantPlayerId != 0 && m_iOccupantPlayerId != playerId)
		{
			link.SendGrant(GetRplId(), false, ELIFE_EATMReason.IN_USE);
			return;
		}

		//! A refusal would replace the result on the occupant's screen.
		if (m_bShowingPaid)
			return;

		if (!IsUsableBy(character))
		{
			Print(string.Format("ELIFE_ATMComponent | open refused, player %1 is dead or out of range", playerId), LogLevel.WARNING);
			link.SendGrant(GetRplId(), false, ELIFE_EATMReason.OUT_OF_RANGE);
			return;
		}

		if (m_iOccupantPlayerId == playerId)
		{
			if (character != m_Occupant)
			{
				Print(string.Format("ELIFE_ATMComponent | open refused, player %1 holds the lock with another character", playerId), LogLevel.WARNING);
				link.SendGrant(GetRplId(), false, ELIFE_EATMReason.IN_USE);
				return;
			}

			m_bClosePending = false;
			m_iLastRequestTick = System.GetTickCount();
			link.SendGrant(GetRplId(), true, ELIFE_EATMReason.NONE);
			if (m_sRealAccountsJson != "")
				link.SendAccounts(GetRplId(), m_sRealAccountsJson);

			if (m_sRealJson != "")
				link.SendPayload(GetRplId(), m_sRealJson);

			return;
		}

		string characterId = ELIFE_CharacterIdentity.GetCharacterId(playerId);
		if (characterId == "")
		{
			Print(string.Format("ELIFE_ATMComponent | open refused, player %1 has no character id", playerId), LogLevel.WARNING);
			link.SendGrant(GetRplId(), false, ELIFE_EATMReason.DATA_UNAVAILABLE);
			return;
		}

		m_iOccupantPlayerId = playerId;
		m_Occupant = character;
		m_sOccupantCharacterId = characterId;
		m_iLastRequestTick = System.GetTickCount();
		m_iLastRefreshTick = m_iLastRequestTick;
		m_iLastHeartbeatTick = m_iLastRequestTick;
		m_iState = ELIFE_EATMState.LOADING;
		Replication.BumpMe();

		//! onRplName doesn't fire on the authority itself.
		OnOccupantUpdated();
		OnStateUpdated();

		Print(string.Format("ELIFE_ATMComponent | %1 locked by player %2", GetMachineId(), playerId), LogLevel.NORMAL);
		link.SendGrant(GetRplId(), true, ELIFE_EATMReason.NONE);

		GetGame().GetCallqueue().CallLater(Tick, TICK_INTERVAL_MS, true);
		StartLoad();
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. During a booking the release waits until it has finished.
	void RequestClose(int playerId)
	{
		if (playerId == 0 || playerId != m_iOccupantPlayerId || m_bShowingPaid)
			return;

		if (m_Booking)
		{
			m_bClosePending = true;
			return;
		}

		Release("closed by the occupant");
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only.
	void KeepAlive(int playerId, notnull IEntity character)
	{
		if (IsOccupant(playerId, character))
			m_iLastRequestTick = System.GetTickCount();
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. Only proves the occupant's game still runs; it doesn't count as input.
	void Heartbeat(int playerId, notnull IEntity character)
	{
		if (IsOccupant(playerId, character))
			m_iLastHeartbeatTick = System.GetTickCount();
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. Throttled, since every load is three API calls.
	void RequestRefresh(int playerId, notnull IEntity character)
	{
		if (!IsOccupant(playerId, character))
			return;

		int now = System.GetTickCount();
		m_iLastRequestTick = now;
		//! Try again must always answer; StartLoad() already runs one load at a time.
		bool loadProblem = m_iState == ELIFE_EATMState.LOAD_FAILED || m_iState == ELIFE_EATMState.NO_ACCOUNT;
		if (!loadProblem && now - m_iLastRefreshTick < REFRESH_MIN_INTERVAL_MS)
			return;

		m_iLastRefreshTick = now;
		if (!m_Booking && m_iState == ELIFE_EATMState.LOAD_FAILED)
			SetState(ELIFE_EATMState.LOADING);

		StartLoad();
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. The number came from a client, so it only counts if the last load found it among this character's accounts.
	void RequestSelectAccount(int playerId, notnull IEntity character, string accountNumber)
	{
		if (m_aAccountNumbers.Contains(accountNumber))
			ChangeAccount(playerId, character, accountNumber);
		else
			Print(string.Format("ELIFE_ATMComponent | player %1 chose account %2, which is not on their list", playerId, accountNumber), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. Back to the account list.
	void RequestChangeAccount(int playerId, notnull IEntity character)
	{
		ChangeAccount(playerId, character, "");
	}

	//------------------------------------------------------------------------------------------------
	//! A quote is for one account's fees, so it goes too. A dropped request is safe: the client only follows the state.
	protected void ChangeAccount(int playerId, notnull IEntity character, string accountNumber)
	{
		if (!IsOccupant(playerId, character) || m_Booking || m_bShowingPaid)
			return;

		int now = System.GetTickCount();
		m_iLastRequestTick = now;
		if (now - m_iLastRefreshTick < ACCOUNT_CHANGE_MIN_INTERVAL_MS)
			return;

		m_iLastRefreshTick = now;
		m_sChosenAccountNumber = accountNumber;
		m_iQuoteId = 0;
		ClearLoadedAccount();
		SetState(ELIFE_EATMState.LOADING);
		StartLoad();
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearLoadedAccount()
	{
		m_sAccountNumber = "";
		m_sFeeTerms = "";
		m_iLoadedBalanceCents = 0;
		m_sRealJson = "";
		m_sRedactedJson = "";
		SetBankName("");
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only.
	void RequestQuote(int playerId, notnull IEntity character, notnull ELIFE_ATMCharacterComponent link, ELIFE_EATMBookingKind kind, int amount)
	{
		ELIFE_EATMReason reason = ValidateQuote(playerId, character, kind, amount);
		if (reason != ELIFE_EATMReason.NONE)
		{
			Print(string.Format("ELIFE_ATMComponent | quote refused for player %1, amount %2, reason %3", playerId, amount, typename.EnumToString(ELIFE_EATMReason, reason)), LogLevel.WARNING);
			link.SendQuoteRefused(GetRplId(), reason, amount);
			return;
		}

		m_iLastRequestTick = System.GetTickCount();
		m_iQuoteId = m_iNextQuoteId;
		m_iNextQuoteId++;
		m_iQuoteAmount = amount;
		m_eQuoteKind = kind;
		m_iQuoteTick = m_iLastRequestTick;

		link.SendQuote(GetRplId(), m_iQuoteId, amount, m_sFeeTerms);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. Consumes the quote before anything else can happen, so a repeat finds nothing to confirm.
	void RequestConfirm(int playerId, notnull IEntity character, notnull ELIFE_ATMCharacterComponent link, int quoteId)
	{
		if (quoteId != 0 && quoteId == m_iLastConsumedQuoteId)
		{
			Print(string.Format("ELIFE_ATMComponent | repeated confirm of quote %1 from player %2 ignored", quoteId, playerId), LogLevel.NORMAL);
			return;
		}

		ELIFE_EATMReason reason = ValidateConfirm(playerId, character, quoteId);
		if (reason != ELIFE_EATMReason.NONE)
		{
			Print(string.Format("ELIFE_ATMComponent | confirm refused for player %1, quote %2, reason %3", playerId, quoteId, typename.EnumToString(ELIFE_EATMReason, reason)), LogLevel.WARNING);
			link.SendResult(GetRplId(), reason, m_iQuoteAmount, "", "");
			return;
		}

		m_iLastRequestTick = System.GetTickCount();
		m_iLastConsumedQuoteId = quoteId;
		m_iQuoteId = 0;

		m_Booking = new ELIFE_ATMBooking();
		m_Booking.m_eKind = m_eQuoteKind;
		m_Booking.m_sMachineId = GetMachineId();
		m_Booking.m_iPlayerId = playerId;
		m_Booking.m_Character = character;
		m_Booking.m_sCharacterId = m_sOccupantCharacterId;
		m_Booking.m_sAccountNumber = m_sAccountNumber;
		m_Booking.m_iAmount = m_iQuoteAmount;
		s_aBusyAccounts.Insert(m_sAccountNumber);
		SetState(ELIFE_EATMState.PROCESSING);

		//! Taken first, so the cash can't be spent or dropped while the deposit runs.
		if (m_Booking.IsDeposit())
		{
			if (!ELIFE_CashComponent.TakeFromCarriedStacks(character, m_Booking.m_iAmount))
			{
				AbortBooking(ELIFE_EATMReason.NOT_ENOUGH_CASH, "cash no longer carried");
				return;
			}

			m_Booking.m_iTaken = m_Booking.m_iAmount;
		}

		if (!ELIFE_Api.GetInstance())
		{
			AbortBooking(ELIFE_EATMReason.DATA_UNAVAILABLE, "API not available");
			return;
		}

		ELIFE_EBankBookingKind bookingKind = ELIFE_EBankBookingKind.WITHDRAW;
		if (m_Booking.IsDeposit())
			bookingKind = ELIFE_EBankBookingKind.DEPOSIT;

		//! The account came from the occupant's own list, which the load filtered by character.
		m_Booking.m_Request = new ELIFE_BankBooking(bookingKind, m_sAccountNumber, m_Booking.m_iAmount * 100, m_sOccupantCharacterId, m_Booking.m_sMachineId);
		Print(string.Format("ELIFE_ATMComponent | booking: %1", m_Booking.Describe()), LogLevel.NORMAL);
		m_Booking.m_Request.Start(this, "OnBankBookingDone", "OnBankBookingResend");
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_EATMReason ValidateQuote(int playerId, notnull IEntity character, ELIFE_EATMBookingKind kind, int amount)
	{
		if (!IsOccupant(playerId, character))
			return ELIFE_EATMReason.NOT_OCCUPANT;

		if (m_Booking || m_bShowingPaid)
			return ELIFE_EATMReason.BUSY;

		if (!IsUsableBy(character))
			return ELIFE_EATMReason.OUT_OF_RANGE;

		if (amount <= 0)
			return ELIFE_EATMReason.INVALID_AMOUNT;

		if (amount > MAX_BOOKING_AMOUNT)
			return ELIFE_EATMReason.AMOUNT_TOO_HIGH;

		if (m_sAccountNumber == "")
			return ELIFE_EATMReason.NO_ACCOUNT;

		if (kind == ELIFE_EATMBookingKind.DEPOSIT)
		{
			if (!m_bBranchMachine)
				return ELIFE_EATMReason.NOT_OFFERED;

			if (ELIFE_CashComponent.CountCarriedCash(character) < amount)
				return ELIFE_EATMReason.NOT_ENOUGH_CASH;

			return ELIFE_EATMReason.NONE;
		}

		if (kind != ELIFE_EATMBookingKind.WITHDRAW)
			return ELIFE_EATMReason.NOT_OFFERED;

		//! The fee is unknown until booked.
		if (m_iLoadedBalanceCents < amount * 100)
			return ELIFE_EATMReason.INSUFFICIENT_FUNDS;

		return ELIFE_EATMReason.NONE;
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_EATMReason ValidateConfirm(int playerId, notnull IEntity character, int quoteId)
	{
		if (!IsOccupant(playerId, character))
			return ELIFE_EATMReason.NOT_OCCUPANT;

		if (m_Booking || m_bShowingPaid)
			return ELIFE_EATMReason.BUSY;

		if (quoteId == 0 || quoteId != m_iQuoteId || System.GetTickCount() - m_iQuoteTick > QUOTE_LIFETIME_MS)
			return ELIFE_EATMReason.QUOTE_INVALID;

		if (!IsUsableBy(character))
			return ELIFE_EATMReason.OUT_OF_RANGE;

		if (s_aBusyAccounts.Contains(m_sAccountNumber))
			return ELIFE_EATMReason.BUSY;

		return ELIFE_EATMReason.NONE;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsOccupant(int playerId, IEntity character)
	{
		return playerId != 0 && playerId == m_iOccupantPlayerId && character == m_Occupant;
	}

	//------------------------------------------------------------------------------------------------
	//! Polled, as nothing here needs a game-mode hook: a disconnect or a deleted character both show up as a changed controlled entity.
	protected void Tick()
	{
		if (m_Booking)
			return;

		if (!IsOccupantConnected())
		{
			Release("the occupant disconnected or the character is gone");
			return;
		}

		//! The machine stays taken until EndPaidHold, wherever the player walks.
		if (m_bShowingPaid)
			return;

		if (!IsUsableBy(m_Occupant))
		{
			Release("the occupant died or left the range");
			return;
		}

		//! A request proves the game runs as well as a heartbeat does.
		int now = System.GetTickCount();
		int lastSign = m_iLastRequestTick;
		if (m_iLastHeartbeatTick > lastSign)
			lastSign = m_iLastHeartbeatTick;

		if (now - lastSign > HEARTBEAT_TIMEOUT_MS)
		{
			Release("the occupant's game stopped sending heartbeats");
			return;
		}

		if (now - m_iLastRequestTick > AFK_TIMEOUT_MS)
			Release("no input from the occupant");
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsOccupantConnected()
	{
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager || !m_Occupant)
			return false;

		return playerManager.IsPlayerConnected(m_iOccupantPlayerId) && playerManager.GetPlayerControlledEntity(m_iOccupantPlayerId) == m_Occupant;
	}

	//------------------------------------------------------------------------------------------------
	//! Never called during a booking. A load still in flight notices the missing occupant and stops.
	protected void Release(string why)
	{
		GetGame().GetCallqueue().Remove(Tick);
		GetGame().GetCallqueue().Remove(EndPaidHold);

		Print(string.Format("ELIFE_ATMComponent | %1 released by player %2: %3", GetMachineId(), m_iOccupantPlayerId, why), LogLevel.NORMAL);

		m_iOccupantPlayerId = 0;
		m_Occupant = null;
		m_sOccupantCharacterId = "";
		m_bClosePending = false;
		m_iQuoteId = 0;
		m_sAccountNumber = "";
		m_sAccountBankId = "";
		m_sAccountJson = "";
		m_sFeeTerms = "";
		m_iLoadedBalanceCents = 0;
		m_sRealJson = "";
		m_sRedactedJson = "";
		m_aAccountNumbers.Clear();
		m_LoadedAccounts = null;
		m_sBankName = "";
		m_sChosenAccountNumber = "";
		m_sRealAccountsJson = "";
		m_sRedactedAccountsJson = "";
		m_bShowingPaid = false;
		m_iMirrorScreen = 0;
		m_iMirrorPage = 0;
		m_bMirrorTyped = false;
		m_iMirrorReason = 0;
		m_iState = ELIFE_EATMState.FREE;
		Replication.BumpMe();

		//! onRplName doesn't fire on the authority itself.
		OnOccupantUpdated();
		OnStateUpdated();
	}

	//------------------------------------------------------------------------------------------------
	//! Before the result goes out: on a host the answer arrives inside the send. Covers the client's wait before it shows the result.
	protected void BeginPaidHold()
	{
		m_bShowingPaid = true;
		Replication.BumpMe();

		GetGame().GetCallqueue().CallLater(EndPaidHold, PROCESSING_MIN_MS + PAID_RESULT_MS);
	}

	//------------------------------------------------------------------------------------------------
	protected void EndPaidHold()
	{
		Release("the paid result has been shown");
	}

	//------------------------------------------------------------------------------------------------
	protected void SetState(ELIFE_EATMState newState)
	{
		m_iState = newState;
		Replication.BumpMe();

		//! onRplName doesn't fire on the authority itself.
		OnStateUpdated();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnOccupantUpdated()
	{
		if (m_Screen)
			m_Screen.OnOccupancyChanged();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnMirrorUpdated()
	{
		if (m_Screen)
			m_Screen.OnMirrorChanged();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnStateUpdated()
	{
		if (m_Screen)
			m_Screen.OnStateChanged();
	}

	//------------------------------------------------------------------------------------------------
	protected RplId GetRplId()
	{
		RplComponent rpl = RplComponent.Cast(GetOwner().FindComponent(RplComponent));
		if (!rpl)
			return RplId.Invalid();

		return rpl.Id();
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_ATMCharacterComponent GetOccupantLink()
	{
		if (!m_Occupant)
			return null;

		return ELIFE_ATMCharacterComponent.Cast(m_Occupant.FindComponent(ELIFE_ATMCharacterComponent));
	}

	//------------------------------------------------------------------------------------------------
	//! The bodies of the bare-array routes aren't parsed by the callback, so a bad one would read as an empty list.
	protected bool IsArrayBody(string body)
	{
		return body.StartsWith("[");
	}

	//------------------------------------------------------------------------------------------------
	//! One sequential load at a time: accounts, banks, transactions. A request during a load re-runs it once it lands.
	protected void StartLoad()
	{
		if (m_bLoadInFlight)
		{
			m_bLoadStale = true;
			return;
		}

		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (!api)
		{
			Print("ELIFE_ATMComponent | load failed, API not available", LogLevel.WARNING);
			EndLoad(ELIFE_EATMState.LOAD_FAILED);
			return;
		}

		m_bLoadInFlight = true;
		m_bLoadStale = false;
		m_LoadCallback = api.ListCharacterBankAccounts(m_sOccupantCharacterId, this, "OnLoadAccounts");
	}

	//------------------------------------------------------------------------------------------------
	//! A running booking keeps its own state; it sets the idle one when it finishes.
	protected void EndLoad(ELIFE_EATMState resultState)
	{
		m_bLoadInFlight = false;

		if (!m_Booking)
			SetState(resultState);
	}

	//------------------------------------------------------------------------------------------------
	//! True if this load's answer is no longer wanted: the occupant left, or a newer request is waiting.
	protected bool DropStaleLoad()
	{
		if (m_iOccupantPlayerId == 0)
		{
			m_bLoadInFlight = false;
			return true;
		}

		if (!m_bLoadStale)
			return false;

		m_bLoadInFlight = false;
		StartLoad();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	void OnLoadAccounts(ELIFE_EApiStatusCode status, JsonApiStruct data, string characterId)
	{
		if (DropStaleLoad())
			return;

		if (status != ELIFE_EApiStatusCode.SUCCESS || !m_LoadCallback || !IsArrayBody(m_LoadCallback.GetData()))
		{
			Print(string.Format("ELIFE_ATMComponent | load failed, no account list for character %1", characterId), LogLevel.WARNING);
			EndLoad(ELIFE_EATMState.LOAD_FAILED);
			return;
		}

		ELIFE_BankAccountListDto list = new ELIFE_BankAccountListDto();
		list.ExpandFromArray(m_LoadCallback.GetData());

		//! The read has no ownership check of its own.
		m_LoadedAccounts = new ELIFE_BankAccountListDto();
		foreach (ELIFE_BankAccountSummary account : list.items)
		{
			if (account && account.characterId == characterId && account.number != "" && !account.IsCompany())
				m_LoadedAccounts.items.Insert(account.Copy());
		}

		if (m_LoadedAccounts.items.IsEmpty())
		{
			PublishAccounts(m_LoadedAccounts, null);
			ClearLoadedAccount();
			EndLoad(ELIFE_EATMState.NO_ACCOUNT);
			return;
		}

		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (!api)
		{
			Print("ELIFE_ATMComponent | load failed, API not available", LogLevel.WARNING);
			EndLoad(ELIFE_EATMState.LOAD_FAILED);
			return;
		}

		m_LoadCallback = api.ListBanks(this, "OnLoadBanks");
	}

	//------------------------------------------------------------------------------------------------
	//! A lone account needs no choosing. Null means the player has to choose.
	protected ELIFE_BankAccountSummary PickAccount(notnull ELIFE_BankAccountListDto ownAccounts)
	{
		if (ownAccounts.items.Count() == 1)
			return ownAccounts.items[0];

		foreach (ELIFE_BankAccountSummary account : ownAccounts.items)
		{
			if (account.number == m_sChosenAccountNumber)
				return account;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Bystanders get stand-in numbers and balances, and no ids. Bank names are public.
	protected void PublishAccounts(notnull ELIFE_BankAccountListDto ownAccounts, ELIFE_BankListDto banks)
	{
		m_aAccountNumbers.Clear();

		ELIFE_ATMAccountList real = new ELIFE_ATMAccountList();
		ELIFE_ATMAccountList redacted = new ELIFE_ATMAccountList();
		foreach (ELIFE_BankAccountSummary account : ownAccounts.items)
		{
			m_aAccountNumbers.Insert(account.number);

			string bankName = "";
			ELIFE_BankSummary bank = FindBank(banks, account.bankId);
			if (bank)
				bankName = bank.name;

			real.items.Insert(new ELIFE_ATMAccountChoice(account.number, account.balanceStr, bankName));
			redacted.items.Insert(new ELIFE_ATMAccountChoice(ELIFE_DataRedactor.RedactDigits(account.number), ELIFE_DataRedactor.RedactMoneyText(account.balanceStr), bankName));
		}

		real.Pack();
		m_sRealAccountsJson = real.AsString();
		redacted.Pack();
		m_sRedactedAccountsJson = redacted.AsString();

		ELIFE_ATMCharacterComponent link = GetOccupantLink();
		if (link)
			link.SendAccounts(GetRplId(), m_sRealAccountsJson);

		Rpc(RpcDo_RedactedAccounts, m_sRedactedAccountsJson);

		//! A broadcast never reaches the authority's own view, which only a host has.
		if (m_Screen && SCR_PlayerController.GetLocalPlayerId() != 0)
			m_Screen.OnRedactedAccounts(m_sRedactedAccountsJson);
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_BankSummary FindBank(ELIFE_BankListDto banks, string bankId)
	{
		if (!banks)
			return null;

		foreach (ELIFE_BankSummary bank : banks.items)
		{
			if (bank && bank.bankId == bankId)
				return bank;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! The name in the screen's header. Empty until an account is chosen.
	protected void SetBankName(string bankName)
	{
		m_sBankName = bankName;
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_RedactedAccounts(string json)
	{
		if (m_Screen)
			m_Screen.OnRedactedAccounts(json);
	}

	//------------------------------------------------------------------------------------------------
	string GetRedactedAccountsJson()
	{
		return m_sRedactedAccountsJson;
	}

	//------------------------------------------------------------------------------------------------
	//! Without the bank list the statement and quotes still work; only the banks go unnamed.
	void OnLoadBanks(ELIFE_EApiStatusCode status, JsonApiStruct data)
	{
		if (DropStaleLoad())
			return;

		ELIFE_BankListDto banks;
		if (status == ELIFE_EApiStatusCode.SUCCESS && m_LoadCallback && IsArrayBody(m_LoadCallback.GetData()))
		{
			banks = new ELIFE_BankListDto();
			banks.ExpandFromArray(m_LoadCallback.GetData());
		}

		PublishAccounts(m_LoadedAccounts, banks);

		ELIFE_BankAccountSummary picked = PickAccount(m_LoadedAccounts);
		if (!picked)
		{
			ClearLoadedAccount();
			EndLoad(ELIFE_EATMState.CHOOSE_ACCOUNT);
			return;
		}

		m_sAccountNumber = picked.number;
		m_sAccountBankId = picked.bankId;

		ELIFE_BankAccountSummary copy = picked.Copy();
		copy.Pack();
		m_sAccountJson = copy.AsString();

		//! The account's terms, copied at opening, not the bank's current ones.
		m_sFeeTerms = picked.feeTermsStr;
		ELIFE_BankSummary bank = FindBank(banks, m_sAccountBankId);
		if (bank)
			SetBankName(bank.name);
		else
			SetBankName("");

		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (!api)
		{
			Print("ELIFE_ATMComponent | load failed, API not available", LogLevel.WARNING);
			EndLoad(ELIFE_EATMState.LOAD_FAILED);
			return;
		}

		m_LoadCallback = api.ListBankAccountTransactions(m_sAccountNumber, this, "OnLoadTransactions");
	}

	//------------------------------------------------------------------------------------------------
	void OnLoadTransactions(ELIFE_EApiStatusCode status, JsonApiStruct data, string accountNumber)
	{
		if (DropStaleLoad())
			return;

		if (status != ELIFE_EApiStatusCode.SUCCESS || !m_LoadCallback || !IsArrayBody(m_LoadCallback.GetData()))
		{
			Print(string.Format("ELIFE_ATMComponent | load failed, no transactions for account %1", accountNumber), LogLevel.WARNING);
			EndLoad(ELIFE_EATMState.LOAD_FAILED);
			return;
		}

		ELIFE_BankStatementDto statement = new ELIFE_BankStatementDto();
		statement.ExpandFromArray(m_LoadCallback.GetData(), "\"account\":" + m_sAccountJson + ",");
		PublishStatement(statement);
	}

	//------------------------------------------------------------------------------------------------
	//! Ends the load. The statement must already hold its raw JSON.
	protected void PublishStatement(notnull ELIFE_BankStatementDto statement)
	{
		m_sRealJson = statement.GetRawJson();
		m_iLoadedBalanceCents = ELIFE_PhoneBankingService.MoneyTextToCents(statement.account.balanceStr);

		//! Redacted once per load, so bystanders and late joiners all see the same stand-in figures.
		ELIFE_BankStatementDto redacted = statement.Redact();
		redacted.Pack();
		m_sRedactedJson = redacted.AsString();

		EndLoad(ELIFE_EATMState.READY);

		ELIFE_ATMCharacterComponent link = GetOccupantLink();
		if (link)
			link.SendPayload(GetRplId(), m_sRealJson);

		Rpc(RpcDo_RedactedPayload, m_sRedactedJson);

		//! A broadcast never reaches the authority's own view, which only a host has.
		if (m_Screen && SCR_PlayerController.GetLocalPlayerId() != 0)
			m_Screen.OnRedactedPayload(m_sRedactedJson);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_RedactedPayload(string json)
	{
		if (m_Screen)
			m_Screen.OnRedactedPayload(json);
	}

	//------------------------------------------------------------------------------------------------
	void OnBankBookingResend(ELIFE_BankBooking request)
	{
		if (m_Booking && m_Booking.m_Request == request && m_iState != ELIFE_EATMState.CHECKING)
			SetState(ELIFE_EATMState.CHECKING);
	}

	//------------------------------------------------------------------------------------------------
	void OnBankBookingDone(ELIFE_BankBooking request)
	{
		if (!m_Booking || m_Booking.m_Request != request)
		{
			Print(string.Format("ELIFE_ATMComponent | booking answer arrived with no booking running: %1", request.Describe()), LogLevel.ERROR);
			return;
		}

		ELIFE_TransactionResult result = request.GetResult();
		switch (request.GetOutcome())
		{
			case ELIFE_EBankBookingOutcome.BOOKED:
				m_Booking.m_sFee = result.feeStr;
				m_Booking.m_sNewBalance = result.newBalanceStr;
				Settle();
				return;

			case ELIFE_EBankBookingOutcome.CONFLICT:
				if (m_Booking.IsDeposit())
					AbortBooking(ELIFE_EATMReason.NOT_DEPOSITED, "booking refused with 409");
				else if (request.GetProblemCode() == "" || request.GetProblemCode() == ELIFE_BankBooking.CODE_INSUFFICIENT_BALANCE)
					AbortBooking(ELIFE_EATMReason.INSUFFICIENT_FUNDS, "booking refused with 409");
				else
					AbortBooking(ELIFE_EATMReason.NOT_WITHDRAWN, "booking refused with 409");

				return;

			case ELIFE_EBankBookingOutcome.DENIED:
				AbortBooking(ELIFE_EATMReason.DENIED, "booking refused with 403/404");
				return;

			case ELIFE_EBankBookingOutcome.INVALID:
				if (m_Booking.IsDeposit())
					AbortBooking(ELIFE_EATMReason.NOT_DEPOSITED, "booking refused as invalid");
				else
					AbortBooking(ELIFE_EATMReason.NOT_WITHDRAWN, "booking refused as invalid");

				return;
		}

		ReportUnresolved("booking outcome unknown after every resend");
	}

	//------------------------------------------------------------------------------------------------
	//! The booking went through once: a withdrawal pays out, a deposit already holds its cash.
	protected void Settle()
	{
		if (!m_Booking.IsDeposit())
		{
			PayOut();
			return;
		}

		Print(string.Format("ELIFE_ATMComponent | deposited, fee %1: %2", m_Booking.m_sFee, m_Booking.Describe()), LogLevel.NORMAL);
		SendBookingResult(ELIFE_EATMReason.PAID);
		FinishBooking();
	}

	//------------------------------------------------------------------------------------------------
	//! The money has left the account, so the cash goes to the character even if dead; overflow lands on the ground.
	protected void PayOut()
	{
		IEntity character = m_Booking.m_Character;
		if (character)
		{
			int dropped;
			m_Booking.m_iPaid = SpawnCashStacks(m_Booking.m_iAmount, character, dropped);
			m_Booking.m_iDropped = dropped;
		}

		if (m_Booking.m_iPaid < m_Booking.m_iAmount)
		{
			ReportUnresolved(string.Format("withdrawn but only %1 of the cash paid out", m_Booking.m_iPaid));
			return;
		}

		Print(string.Format("ELIFE_ATMComponent | paid out, fee %1: %2", m_Booking.m_sFee, m_Booking.Describe()), LogLevel.NORMAL);
		SendBookingResult(ELIFE_EATMReason.PAID);
		FinishBooking();
	}

	//------------------------------------------------------------------------------------------------
	//! Carried stacks fill up first, and only the rest takes new stacks. Stops at the first stack that fails to spawn.
	//! Returns what was actually paid; dropped is the part of it on the ground.
	protected int SpawnCashStacks(int amount, notnull IEntity character, out int dropped)
	{
		dropped = 0;

		int paid = ELIFE_CashComponent.AddToCarriedStacks(character, amount);
		while (paid < amount)
		{
			int stack = amount - paid;
			if (stack > ELIFE_CashComponent.MAX_AMOUNT)
				stack = ELIFE_CashComponent.MAX_AMOUNT;

			bool onGround;
			if (!ELIFE_CashComponent.SpawnCash(stack, character, onGround))
				break;

			paid += stack;
			if (onGround)
				dropped += stack;
		}

		return paid;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsAlive(notnull IEntity entity)
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(entity);
		if (!character)
			return false;

		CharacterControllerComponent controller = character.GetCharacterController();
		return controller && !controller.IsDead();
	}

	//------------------------------------------------------------------------------------------------
	//! Definite: no money moved. A deposit's cash goes back to the player.
	protected void AbortBooking(ELIFE_EATMReason reason, string why)
	{
		Print(string.Format("ELIFE_ATMComponent | booking not executed, %1: %2", why, m_Booking.Describe()), LogLevel.WARNING);
		ReturnTakenCash();
		if (m_Booking.m_iTaken > 0)
			reason = ELIFE_EATMReason.CONTACT_STAFF;

		SendBookingResult(reason);
		FinishBooking();
	}

	//------------------------------------------------------------------------------------------------
	//! Into carried stacks first, then the ground, like a payout.
	protected void ReturnTakenCash()
	{
		if (m_Booking.m_iTaken <= 0)
			return;

		IEntity character = m_Booking.m_Character;
		if (!IsPresent(character))
		{
			Print(string.Format("ELIFE_ATMComponent | character gone, deposit cash not returned, UNRESOLVED: %1", m_Booking.Describe()), LogLevel.ERROR);
			return;
		}

		int dropped;
		int returned = SpawnCashStacks(m_Booking.m_iTaken, character, dropped);
		m_Booking.m_iDropped = dropped;
		if (returned < m_Booking.m_iTaken)
			Print(string.Format("ELIFE_ATMComponent | only %1 of the deposit cash returned, UNRESOLVED: %2", returned, m_Booking.Describe()), LogLevel.ERROR);

		m_Booking.m_iTaken -= returned;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsPresent(IEntity character)
	{
		PlayerManager playerManager = GetGame().GetPlayerManager();
		return character && playerManager && playerManager.IsPlayerConnected(m_Booking.m_iPlayerId) && playerManager.GetPlayerControlledEntity(m_Booking.m_iPlayerId) == character && IsAlive(character);
	}

	//------------------------------------------------------------------------------------------------
	//! Money may be in the wrong place and the mod can't tell or fix it; only staff with this log line can.
	protected void ReportUnresolved(string why)
	{
		Print(string.Format("ELIFE_ATMComponent | UNRESOLVED, contact staff, %1: %2", why, m_Booking.Describe()), LogLevel.ERROR);
		SendBookingResult(ELIFE_EATMReason.CONTACT_STAFF);
		FinishBooking();
	}

	//------------------------------------------------------------------------------------------------
	protected void SendBookingResult(ELIFE_EATMReason reason)
	{
		ELIFE_ATMCharacterComponent link = GetOccupantLink();
		if (!link)
		{
			Print(string.Format("ELIFE_ATMComponent | result %1 not delivered, occupant gone: %2", typename.EnumToString(ELIFE_EATMReason, reason), m_Booking.Describe()), LogLevel.WARNING);
			return;
		}

		//! A withdrawal reports the cash paid out, an unresolved deposit the cash it still holds.
		int amount = m_Booking.m_iAmount;
		if (!m_Booking.IsDeposit() && (reason == ELIFE_EATMReason.PAID || reason == ELIFE_EATMReason.CONTACT_STAFF))
			amount = m_Booking.m_iPaid;
		else if (m_Booking.IsDeposit() && reason == ELIFE_EATMReason.CONTACT_STAFF)
			amount = m_Booking.m_iTaken;

		if (reason == ELIFE_EATMReason.PAID)
			BeginPaidHold();

		link.SendResult(GetRplId(), reason, amount, m_Booking.m_sFee, m_Booking.m_sNewBalance, m_Booking.m_iDropped);
	}

	//------------------------------------------------------------------------------------------------
	//! The lock comes free through a pending close or the next tick, so the idle clock restarts here.
	protected void FinishBooking()
	{
		s_aBusyAccounts.RemoveItem(m_Booking.m_sAccountNumber);
		m_Booking = null;
		m_iLastRequestTick = System.GetTickCount();

		if (m_bClosePending && !m_bShowingPaid)
		{
			Release("closed by the occupant during a booking");
			return;
		}

		SetState(ELIFE_EATMState.READY);
		StartLoad();
	}

	//------------------------------------------------------------------------------------------------
	//! Static prop, so this should only happen at shutdown - a server restart mid-flow can't be recovered from.
	void ~ELIFE_ATMComponent()
	{
		GetGame().GetCallqueue().Remove(Tick);
		GetGame().GetCallqueue().Remove(EndPaidHold);

		if (!m_Booking)
			return;

		if (m_Booking.m_Request)
			m_Booking.m_Request.Cancel();

		s_aBusyAccounts.RemoveItem(m_Booking.m_sAccountNumber);
		Print(string.Format("ELIFE_ATMComponent | ATM deleted during a booking, UNRESOLVED: %1", m_Booking.Describe()), LogLevel.ERROR);
	}
}
