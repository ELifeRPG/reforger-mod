//------------------------------------------------------------------------------------------------
[EntityEditorProps(category: "ELifeRPG/Banking", description: "A bank clerk who serves one customer at a time on behalf of one bank")]
class ELIFE_BankClerkComponentClass : ELIFE_NpcComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! Starts with ELIFE_ENpcReason's values, then the clerk's own.
enum ELIFE_EClerkReason
{
	NONE,
	BUSY,
	OUT_OF_RANGE,
	//! The backend or the bank list can't be reached, or the clerk's bank code matches no bank.
	UNAVAILABLE,
	//! Outside his opening hours.
	CLOSED,
	//! One new account per visit, so the button can't mass-produce them.
	ALREADY_OPENED,
	//! The backend refused; nothing was booked.
	FAILED,
	INVALID_AMOUNT,
	NOT_ENOUGH_MONEY,
	//! Not an account number: wrong shape or check digits.
	INVALID_TARGET,
	TARGET_NOT_FOUND,
	SAME_ACCOUNT,
	QUOTE_EXPIRED,
	//! An ATM or another clerk is booking on the account right now.
	ACCOUNT_BUSY,
	//! No answer from the backend: the booking may or may not have happened.
	UNKNOWN_OUTCOME
}

//------------------------------------------------------------------------------------------------
//! What the clerk says: the line names in his voice table (Configs/Banking/ELIFE_ClerkVoices.conf), whose sound
//! events SOUND_ELIFE_CLERK_<voice>_<line>[_n] the voice tooling (kept outside the repo) generates. A new line needs
//! a new value here, an entry in the table and new events there.
enum ELIFE_EClerkLine
{
	NONE,
	GREETING,
	BUSY,
	UNAVAILABLE,
	TRANSFER_FORM,
	TRANSFER_PROCESSING,
	CHECK_SLIP,
	NOT_ENOUGH_MONEY,
	TARGET_NOT_FOUND,
	ACCOUNT_BUSY,
	QUOTE_EXPIRED,
	TRANSFER_DONE,
	ACCOUNT_OPENED,
	ALREADY_OPENED,
	FAILED,
	UNKNOWN_OUTCOME,
	GOODBYE,
	//! Asked for by the customer's dialogue (askable in the voice table).
	ANYTHING_ELSE,
	OPEN_ACCOUNT_TERMS,
	QUESTIONS,
	INFO_FEES,
	INFO_TRANSFERS,
	INFO_BALANCE,
	INFO_CASH,
	//! Greetings by context, chosen by the server (see PickGreeting).
	GREETING_MORNING,
	GREETING_AFTERNOON,
	GREETING_EVENING,
	GREETING_RETURNING,
	NO_ACCOUNT,
	//! A large transfer: what is it for, then his reaction to the answer (ELIFE_BankClerkTransfers.GetPurposeLine).
	ASK_PURPOSE,
	PURPOSE_GOODS,
	PURPOSE_RENT,
	PURPOSE_GIFT,
	PURPOSE_BUSINESS,
	//! Handing over a paper he offered (ELIFE_BankPaper).
	RECEIPT_HERE,
	PAPERS_HERE
}

//------------------------------------------------------------------------------------------------
//! What the clerk knows about the customer's accounts, looked up when the visit starts.
enum ELIFE_EClerkAccountStatus
{
	//! Not answered (yet), or the lookup failed.
	UNKNOWN,
	NO_ACCOUNT,
	OTHER_BANK,
	//! At this desk's bank, so he greets them as a returning customer.
	THIS_BANK
}

//------------------------------------------------------------------------------------------------
//! The customer's requests (ELIFE_NpcCharacterComponent.Request): a, b and text per kind.
enum ELIFE_EClerkRequest
{
	NONE,
	OPEN_ACCOUNT,
	TRANSFER_ACCOUNTS,
	//! text: a packed ELIFE_ClerkTransferQuote with fromNumber, targetNumber and amount.
	TRANSFER_QUOTE,
	//! a: quote id.
	TRANSFER_CONFIRM,
	//! a: quote id, b: ELIFE_ETransferPurpose.
	TRANSFER_PURPOSE,
	//! a: ELIFE_EBankPaperKind.
	PAPER
}

//------------------------------------------------------------------------------------------------
//! The server's answers to the customer (ELIFE_NpcComponent.Send): a, b and text per kind.
enum ELIFE_EClerkMessage
{
	NONE,
	//! a: ELIFE_EClerkAccountStatus, b: opened an account this visit.
	CUSTOMER_CONTEXT,
	//! a: reason, b: opened, text: the account number.
	ACCOUNT_OPENED,
	//! a: reason, text: a packed ELIFE_ClerkAccountList.
	ACCOUNTS,
	//! a: reason, text: a packed ELIFE_ClerkTransferQuote.
	TRANSFER_QUOTE,
	//! a: reason, text: a packed ELIFE_ClerkTransferReceipt with the Bridge's amount, fee and balance text.
	TRANSFER_RESULT,
	//! a: reason, b: ELIFE_EBankPaperKind, text: "1" if it went onto the counter.
	PAPER
}

//------------------------------------------------------------------------------------------------
//! The open-account body with only the character set. The generated DTO always sends companyId too, and the backend wants exactly one of them.
class ELIFE_ClerkOpenAccountBody : JsonApiStruct
{
	string characterId;

	void ELIFE_ClerkOpenAccountBody()
	{
		RegV("characterId");
	}
}

//------------------------------------------------------------------------------------------------
//! The bank clerk on top of the generic NPC: the bank the desk belongs to, the customer's accounts, opening
//! accounts, transfers and papers. Requests arrive through ELIFE_NpcCharacterComponent.
class ELIFE_BankClerkComponent : ELIFE_NpcComponent
{
	//! Paper changing hands at his desk: the slip, a receipt, account papers.
	static const string SFX_PAPER = "SOUND_ELIFE_CLERK_SFX_PAPER";
	//! The greeting waits this long at most for the customer's accounts, then he greets without knowing them.
	protected static const int GREETING_LOOKUP_TIMEOUT_MS = 1500;
	//! Hours (in-game) of the time-of-day greetings; outside them he just says hello.
	protected static const int MORNING_FROM_HOUR = 5;
	protected static const int AFTERNOON_FROM_HOUR = 12;
	protected static const int EVENING_FROM_HOUR = 18;
	protected static const int NIGHT_FROM_HOUR = 23;

	//! Set by the ELIFE_BankClerkSpot that spawned him. Server-only.
	protected string m_sBankCode = "FEBK";

	//! TODO: from the backend, so each bank sets its own and it can change without a mod update.
	[Attribute("10000", UIWidgets.EditBox, "Transfers from this amount on (whole Ŧ) are asked what they are for before booking", "1 100000 1")]
	protected int m_iPurposeThreshold;

	//! Public, so it's replicated for every client's dialog title.
	[RplProp()]
	protected string m_sBankName;
	//! The bank's current booking fee terms as display text. Public, shown before opening an account.
	[RplProp()]
	protected string m_sFeeTerms;

	protected string m_sBankId;
	protected ref ELIFE_BaseRestCallback m_BankCallback;
	protected bool m_bBankLoadInFlight;

	protected ref ELIFE_BankAccountSummaryCallback m_OpenCallback;
	protected int m_iOpeningPlayerId;
	protected bool m_bOpenedThisVisit;

	//! Server-side: the customer's accounts for this visit, looked up before the greeting.
	protected ref ELIFE_BankClerkCustomerLookup m_CustomerLookup;
	protected ELIFE_EClerkAccountStatus m_eCustomerAccounts;
	protected bool m_bGreeted;

	//! Client-side: the latest customer context, which may arrive before the grant. It counts only for the granted visit.
	protected int m_iContextVisitId;
	protected ELIFE_EClerkAccountStatus m_eContextAccounts;
	protected bool m_bContextOpenedThisVisit;

	//! Server-side: the papers he offered this visit, each handed over at most once.
	protected ref ELIFE_BankPaper m_PendingReceipt;
	protected ref ELIFE_BankPaper m_PendingAccountPapers;

	protected ref ELIFE_BankClerkTransfers m_Transfers;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		m_Transfers = new ELIFE_BankClerkTransfers(this);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only, before the first customer.
	void SetBankCode(string bankCode)
	{
		m_sBankCode = bankCode;
		m_sBankId = "";
	}

	//------------------------------------------------------------------------------------------------
	string GetBankName()
	{
		return m_sBankName;
	}

	//------------------------------------------------------------------------------------------------
	string GetFeeTerms()
	{
		return m_sFeeTerms;
	}

	//------------------------------------------------------------------------------------------------
	int GetPurposeThreshold()
	{
		return m_iPurposeThreshold;
	}

	//------------------------------------------------------------------------------------------------
	//! Names this desk on bookings and starts their Idempotency-Keys, so it holds no comma.
	string GetSourceId()
	{
		vector position = GetOwner().GetOrigin();
		int x = Math.Round(position[0] * 10);
		int y = Math.Round(position[1] * 10);
		int z = Math.Round(position[2] * 10);
		return string.Format("Clerk-%1-%2-%3-%4", m_sBankCode, x, y, z);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only.
	void Say(ELIFE_EClerkLine line)
	{
		if (line != ELIFE_EClerkLine.NONE)
			SayLine(typename.EnumToString(ELIFE_EClerkLine, line));
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only.
	void SayOutcome(ELIFE_EClerkReason reason, ELIFE_EClerkLine successLine)
	{
		if (reason == ELIFE_EClerkReason.NONE)
			Say(successLine);
		else
			Say(GetReasonLine(reason));
	}

	//------------------------------------------------------------------------------------------------
	//! Walking out of range gets no comment: the customer can't hear it anyway.
	static ELIFE_EClerkLine GetReasonLine(int reason)
	{
		switch (reason)
		{
			case ELIFE_EClerkReason.BUSY: return ELIFE_EClerkLine.BUSY;
			case ELIFE_EClerkReason.UNAVAILABLE: return ELIFE_EClerkLine.UNAVAILABLE;
			case ELIFE_EClerkReason.ALREADY_OPENED: return ELIFE_EClerkLine.ALREADY_OPENED;
			case ELIFE_EClerkReason.FAILED: return ELIFE_EClerkLine.FAILED;
			case ELIFE_EClerkReason.INVALID_AMOUNT: return ELIFE_EClerkLine.CHECK_SLIP;
			case ELIFE_EClerkReason.INVALID_TARGET: return ELIFE_EClerkLine.CHECK_SLIP;
			case ELIFE_EClerkReason.SAME_ACCOUNT: return ELIFE_EClerkLine.CHECK_SLIP;
			case ELIFE_EClerkReason.NOT_ENOUGH_MONEY: return ELIFE_EClerkLine.NOT_ENOUGH_MONEY;
			case ELIFE_EClerkReason.TARGET_NOT_FOUND: return ELIFE_EClerkLine.TARGET_NOT_FOUND;
			case ELIFE_EClerkReason.QUOTE_EXPIRED: return ELIFE_EClerkLine.QUOTE_EXPIRED;
			case ELIFE_EClerkReason.ACCOUNT_BUSY: return ELIFE_EClerkLine.ACCOUNT_BUSY;
			case ELIFE_EClerkReason.UNKNOWN_OUTCOME: return ELIFE_EClerkLine.UNKNOWN_OUTCOME;
		}

		return ELIFE_EClerkLine.NONE;
	}

	//------------------------------------------------------------------------------------------------
	override protected string GetReasonLineName(int reason)
	{
		ELIFE_EClerkLine line = GetReasonLine(reason);
		if (line == ELIFE_EClerkLine.NONE)
			return "";

		return typename.EnumToString(ELIFE_EClerkLine, line);
	}

	//------------------------------------------------------------------------------------------------
	override string GetRefusalText(int reason)
	{
		if (reason == ELIFE_EClerkReason.BUSY)
			return "#ELIFE-Clerk_Busy";
		if (reason == ELIFE_EClerkReason.OUT_OF_RANGE)
			return "#ELIFE-Clerk_OutOfRange";
		if (reason == ELIFE_EClerkReason.CLOSED)
			return super.GetRefusalText(reason);

		return "#ELIFE-Clerk_Unavailable";
	}

	//------------------------------------------------------------------------------------------------
	// Visits (server)
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	override protected int CheckCustomer(int playerId, notnull IEntity character)
	{
		if (ELIFE_CharacterIdentity.GetCharacterId(playerId) != "")
			return ELIFE_EClerkReason.NONE;

		Print(string.Format("ELIFE_BankClerkComponent | open refused, player %1 has no character id", playerId), LogLevel.WARNING);
		return ELIFE_EClerkReason.UNAVAILABLE;
	}

	//------------------------------------------------------------------------------------------------
	//! The bank never changes for a desk, so it's looked up once and kept.
	override protected bool IsReady()
	{
		return m_sBankId != "";
	}

	//------------------------------------------------------------------------------------------------
	override protected void Prepare()
	{
		if (m_bBankLoadInFlight)
			return;

		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (!api)
		{
			Print("ELIFE_BankClerkComponent | bank lookup failed, API not available", LogLevel.WARNING);
			AnswerWaiting(false);
			return;
		}

		m_bBankLoadInFlight = true;
		m_BankCallback = api.ListBanks(this, "OnLoadBanks");
	}

	//------------------------------------------------------------------------------------------------
	void OnLoadBanks(ELIFE_EApiStatusCode status, JsonApiStruct data)
	{
		m_bBankLoadInFlight = false;

		if (status != ELIFE_EApiStatusCode.SUCCESS || !m_BankCallback)
		{
			Print("ELIFE_BankClerkComponent | bank lookup failed, no bank list", LogLevel.WARNING);
			AnswerWaiting(false);
			return;
		}

		ELIFE_BankListDto banks = new ELIFE_BankListDto();
		banks.ExpandFromArray(m_BankCallback.GetData());
		foreach (ELIFE_BankSummary bank : banks.items)
		{
			if (bank && bank.code == m_sBankCode)
			{
				m_sBankId = bank.bankId;
				m_sBankName = bank.name;
				m_sFeeTerms = bank.feeTermsStr;
				Replication.BumpMe();
				AnswerWaiting(true);
				return;
			}
		}

		Print(string.Format("ELIFE_BankClerkComponent | no bank with code %1", m_sBankCode), LogLevel.ERROR);
		AnswerWaiting(false);
	}

	//------------------------------------------------------------------------------------------------
	//! The greeting depends on the customer's accounts, so they are looked up first; a slow or failed lookup
	//! gets the plain greeting after GREETING_LOOKUP_TIMEOUT_MS.
	override protected void OnVisitStart(int playerId)
	{
		m_bGreeted = false;
		m_eCustomerAccounts = ELIFE_EClerkAccountStatus.UNKNOWN;

		ELIFE_Api api = ELIFE_Api.GetInstance();
		string characterId = ELIFE_CharacterIdentity.GetCharacterId(playerId);
		if (!api || characterId == "")
		{
			Greet();
			return;
		}

		m_CustomerLookup = new ELIFE_BankClerkCustomerLookup(this, characterId);
		m_CustomerLookup.m_Callback = api.ListManagedBankAccounts(characterId, m_CustomerLookup, "OnAnswer");
		GetGame().GetCallqueue().CallLater(Greet, GREETING_LOOKUP_TIMEOUT_MS);
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnVisitEnd()
	{
		m_bOpenedThisVisit = false;
		m_PendingReceipt = null;
		m_PendingAccountPapers = null;
		m_CustomerLookup = null;
		m_eCustomerAccounts = ELIFE_EClerkAccountStatus.UNKNOWN;
		GetGame().GetCallqueue().Remove(Greet);
		m_Transfers.Reset();
	}

	//------------------------------------------------------------------------------------------------
	//! Also after the greeting timed out: the replies still adapt to it.
	void OnCustomerAccounts(notnull ELIFE_BankClerkCustomerLookup lookup, ELIFE_EApiStatusCode status, string characterId)
	{
		if (lookup != m_CustomerLookup || m_iCustomerPlayerId == 0 || ELIFE_CharacterIdentity.GetCharacterId(m_iCustomerPlayerId) != characterId)
			return;

		//! Held here: dropping the lookup below would otherwise free the callback and the lookup mid-call.
		ELIFE_BankClerkCustomerLookup self = lookup;
		ELIFE_BaseRestCallback callback = lookup.m_Callback;
		m_CustomerLookup = null;
		if (status == ELIFE_EApiStatusCode.SUCCESS && callback)
		{
			ELIFE_BankAccountListDto list = new ELIFE_BankAccountListDto();
			list.ExpandFromArray(callback.GetData());

			//! Only the customer's own personal accounts count; the managed list also holds company accounts.
			m_eCustomerAccounts = ELIFE_EClerkAccountStatus.NO_ACCOUNT;
			foreach (ELIFE_BankAccountSummary account : list.items)
			{
				if (!account || account.characterId != characterId || account.number == "" || account.IsCompany())
					continue;

				if (ELIFE_BankAccountNumber.GetBankCode(account.number) == m_sBankCode)
				{
					m_eCustomerAccounts = ELIFE_EClerkAccountStatus.THIS_BANK;
					break;
				}

				m_eCustomerAccounts = ELIFE_EClerkAccountStatus.OTHER_BANK;
			}
		}
		else
		{
			Print(string.Format("ELIFE_BankClerkComponent | %1 couldn't look up the accounts of player %2", m_sBankCode, m_iCustomerPlayerId), LogLevel.WARNING);
		}

		//! A late opening this visit already knows better.
		if (m_bOpenedThisVisit)
			m_eCustomerAccounts = ELIFE_EClerkAccountStatus.THIS_BANK;

		SendCustomerContext();
		Greet();
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. Once per visit, from the lookup's answer or its timeout, whichever comes first.
	protected void Greet()
	{
		GetGame().GetCallqueue().Remove(Greet);
		if (m_bGreeted || m_iCustomerPlayerId == 0)
			return;

		m_bGreeted = true;
		Say(PickGreeting());
	}

	//------------------------------------------------------------------------------------------------
	//! Customers of this bank are welcomed back; anyone else gets the time of day or, half the time, a plain hello.
	protected ELIFE_EClerkLine PickGreeting()
	{
		if (m_eCustomerAccounts == ELIFE_EClerkAccountStatus.THIS_BANK)
			return ELIFE_EClerkLine.GREETING_RETURNING;

		if (Math.RandomInt(0, 2) == 0)
			return ELIFE_EClerkLine.GREETING;

		ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
		TimeAndWeatherManagerEntity time;
		if (world)
			time = world.GetTimeAndWeatherManager();

		if (!time)
			return ELIFE_EClerkLine.GREETING;

		int hour, minute, second;
		time.GetHoursMinutesSeconds(hour, minute, second);
		if (hour >= MORNING_FROM_HOUR && hour < AFTERNOON_FROM_HOUR)
			return ELIFE_EClerkLine.GREETING_MORNING;
		if (hour >= AFTERNOON_FROM_HOUR && hour < EVENING_FROM_HOUR)
			return ELIFE_EClerkLine.GREETING_AFTERNOON;
		if (hour >= EVENING_FROM_HOUR && hour < NIGHT_FROM_HOUR)
			return ELIFE_EClerkLine.GREETING_EVENING;

		return ELIFE_EClerkLine.GREETING;
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. What the customer's dialogue needs for its reply conditions.
	protected void SendCustomerContext()
	{
		int opened;
		if (m_bOpenedThisVisit)
			opened = 1;

		Send(m_iCustomerPlayerId, ELIFE_EClerkMessage.CUSTOMER_CONTEXT, m_eCustomerAccounts, opened);
	}

	//------------------------------------------------------------------------------------------------
	// Requests (server)
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	override void OnRequest(int playerId, notnull IEntity character, notnull ELIFE_NpcCharacterComponent link, int kind, int a, int b, string text)
	{
		switch (kind)
		{
			case ELIFE_EClerkRequest.OPEN_ACCOUNT:
				RequestOpenAccount(playerId, character, link);
				return;

			case ELIFE_EClerkRequest.TRANSFER_ACCOUNTS:
				if (!CheckRequest(playerId, character))
				{
					SendClerkAccounts(link, ELIFE_EClerkReason.OUT_OF_RANGE, "");
					return;
				}

				m_Transfers.RequestAccounts(playerId, ELIFE_CharacterIdentity.GetCharacterId(playerId));
				return;

			case ELIFE_EClerkRequest.TRANSFER_QUOTE:
				if (!CheckRequest(playerId, character))
				{
					SendTransferQuote(link, ELIFE_EClerkReason.OUT_OF_RANGE, "");
					return;
				}

				ELIFE_ClerkTransferQuote slip = new ELIFE_ClerkTransferQuote();
				slip.ExpandFromRAW(text);
				m_Transfers.RequestQuote(playerId, slip.fromNumber, slip.targetNumber, slip.amount);
				return;

			case ELIFE_EClerkRequest.TRANSFER_CONFIRM:
				if (!CheckRequest(playerId, character))
				{
					SendTransferResult(link, ELIFE_EClerkReason.OUT_OF_RANGE);
					return;
				}

				m_Transfers.RequestConfirm(playerId, a);
				return;

			//! The answer to his question about a large transfer; the result comes back like a confirmation's.
			case ELIFE_EClerkRequest.TRANSFER_PURPOSE:
				if (!CheckRequest(playerId, character))
				{
					if (b != ELIFE_ETransferPurpose.NONE)
						SendTransferResult(link, ELIFE_EClerkReason.OUT_OF_RANGE);

					return;
				}

				m_Transfers.RequestPurpose(playerId, a, b);
				return;

			case ELIFE_EClerkRequest.PAPER:
				RequestPaper(playerId, character, link, a);
				return;
		}

		super.OnRequest(playerId, character, link, kind, a, b, text);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. A personal account for the customer at this desk's bank.
	protected void RequestOpenAccount(int playerId, notnull IEntity character, notnull ELIFE_NpcCharacterComponent link)
	{
		if (!CheckRequest(playerId, character))
		{
			SendAccountOpened(link, false, ELIFE_EClerkReason.OUT_OF_RANGE, "");
			return;
		}

		//! The same customer's earlier request still answers; anyone else is told instead of left waiting.
		if (m_iOpeningPlayerId == playerId)
			return;

		if (m_iOpeningPlayerId != 0)
		{
			SendAccountOpened(link, false, ELIFE_EClerkReason.UNAVAILABLE, "");
			return;
		}

		if (m_bOpenedThisVisit)
		{
			SendAccountOpened(link, false, ELIFE_EClerkReason.ALREADY_OPENED, "");
			return;
		}

		string characterId = ELIFE_CharacterIdentity.GetCharacterId(playerId);
		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (m_sBankId == "" || characterId == "" || !api)
		{
			SendAccountOpened(link, false, ELIFE_EClerkReason.UNAVAILABLE, "");
			return;
		}

		ELIFE_ClerkOpenAccountBody body = new ELIFE_ClerkOpenAccountBody();
		body.characterId = characterId;
		body.Pack();

		m_iOpeningPlayerId = playerId;
		m_OpenCallback = new ELIFE_BankAccountSummaryCallback();
		m_OpenCallback.SetCallback(this, "OnAccountOpened", characterId);
		api.GetElifeApi().POST(m_OpenCallback, string.Format("banks/%1/accounts", m_sBankId), body.AsString());
		Print(string.Format("ELIFE_BankClerkComponent | %1 opening an account for player %2", m_sBankCode, playerId), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! Answered even if the customer has walked off meanwhile: the account exists either way.
	void OnAccountOpened(ELIFE_EApiStatusCode status, JsonApiStruct data, string characterId)
	{
		int playerId = m_iOpeningPlayerId;
		m_iOpeningPlayerId = 0;

		ELIFE_BankAccountSummary account = ELIFE_BankAccountSummary.Cast(data);
		bool opened = status == ELIFE_EApiStatusCode.SUCCESS && account && account.number != "" && account.characterId == characterId;
		if (opened && playerId == m_iCustomerPlayerId)
		{
			ELIFE_BankPaper papers = new ELIFE_BankPaper();
			papers.kind = ELIFE_EBankPaperKind.ACCOUNT_PAPERS;
			papers.bankName = m_sBankName;
			papers.accountNumber = account.number;
			papers.feeTerms = account.feeTermsStr;
			if (papers.feeTerms == "")
				papers.feeTerms = m_sFeeTerms;

			papers.StampNow();
			KeepPaper(playerId, papers);

			m_bOpenedThisVisit = true;
			m_eCustomerAccounts = ELIFE_EClerkAccountStatus.THIS_BANK;
			SendCustomerContext();
		}

		if (opened)
			Print(string.Format("ELIFE_BankClerkComponent | %1 opened account %2 for player %3", m_sBankCode, account.number, playerId), LogLevel.NORMAL);
		else
			Print(string.Format("ELIFE_BankClerkComponent | %1 failed to open an account for player %2", m_sBankCode, playerId), LogLevel.WARNING);

		ELIFE_NpcCharacterComponent link = GetLink(playerId);
		if (!link)
			return;

		if (opened)
			SendAccountOpened(link, true, ELIFE_EClerkReason.NONE, account.number);
		else
			SendAccountOpened(link, false, ELIFE_EClerkReason.FAILED, "");
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. What he can hand the customer when asked; a newer one replaces the last.
	void KeepPaper(int playerId, notnull ELIFE_BankPaper paper)
	{
		if (playerId == 0 || playerId != m_iCustomerPlayerId)
			return;

		if (paper.kind == ELIFE_EBankPaperKind.ACCOUNT_PAPERS)
			m_PendingAccountPapers = paper;
		else
			m_PendingReceipt = paper;
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. The customer took him up on the paper he offered. Always answered, so the dialogue can move on.
	protected void RequestPaper(int playerId, notnull IEntity character, notnull ELIFE_NpcCharacterComponent link, ELIFE_EBankPaperKind kind)
	{
		if (!CheckRequest(playerId, character))
		{
			SendPaperResult(link, kind, ELIFE_EClerkReason.OUT_OF_RANGE, false);
			return;
		}

		ELIFE_BankPaper paper = m_PendingReceipt;
		ELIFE_EClerkLine line = ELIFE_EClerkLine.RECEIPT_HERE;
		if (kind == ELIFE_EBankPaperKind.ACCOUNT_PAPERS)
		{
			paper = m_PendingAccountPapers;
			line = ELIFE_EClerkLine.PAPERS_HERE;
		}

		//! Already handed over, or nothing was booked.
		if (!paper || paper.kind != kind)
		{
			SendPaperResult(link, kind, ELIFE_EClerkReason.FAILED, false);
			return;
		}

		bool onGround;
		if (!ELIFE_BankPaperComponent.Issue(character, paper, onGround))
		{
			Print(string.Format("ELIFE_BankClerkComponent | %1 couldn't hand player %2 their paper", m_sBankCode, playerId), LogLevel.ERROR);
			SendPaperResult(link, kind, ELIFE_EClerkReason.FAILED, false);
			return;
		}

		if (kind == ELIFE_EBankPaperKind.ACCOUNT_PAPERS)
			m_PendingAccountPapers = null;
		else
			m_PendingReceipt = null;

		Say(line);
		SendPaperResult(link, kind, ELIFE_EClerkReason.NONE, onGround);
	}

	//------------------------------------------------------------------------------------------------
	// Answers (server). Each says his line for the outcome, then tells the customer's client.
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	protected void SendTo(notnull ELIFE_NpcCharacterComponent link, int kind, int a, int b = 0, string text = "")
	{
		link.SendMessage(GetRplId(), m_iVisitId, kind, a, b, text);
	}

	//------------------------------------------------------------------------------------------------
	void SendAccountOpened(notnull ELIFE_NpcCharacterComponent link, bool opened, int reason, string accountNumber)
	{
		SayOutcome(reason, ELIFE_EClerkLine.ACCOUNT_OPENED);
		int openedFlag;
		if (opened)
			openedFlag = 1;

		SendTo(link, ELIFE_EClerkMessage.ACCOUNT_OPENED, reason, openedFlag, accountNumber);
	}

	//------------------------------------------------------------------------------------------------
	void SendClerkAccounts(notnull ELIFE_NpcCharacterComponent link, int reason, string json)
	{
		SayOutcome(reason, ELIFE_EClerkLine.TRANSFER_FORM);
		SendTo(link, ELIFE_EClerkMessage.ACCOUNTS, reason, 0, json);
	}

	//------------------------------------------------------------------------------------------------
	void SendTransferQuote(notnull ELIFE_NpcCharacterComponent link, int reason, string json)
	{
		//! An accepted slip was already announced as TRANSFER_PROCESSING.
		SayOutcome(reason, ELIFE_EClerkLine.NONE);
		SendTo(link, ELIFE_EClerkMessage.TRANSFER_QUOTE, reason, 0, json);
	}

	//------------------------------------------------------------------------------------------------
	//! amount, fee and balance are the Bridge's display text.
	void SendTransferResult(notnull ELIFE_NpcCharacterComponent link, int reason, string amount = "", string fee = "", string balance = "")
	{
		SayOutcome(reason, ELIFE_EClerkLine.TRANSFER_DONE);

		ELIFE_ClerkTransferReceipt result = new ELIFE_ClerkTransferReceipt();
		result.amount = amount;
		result.fee = fee;
		result.balance = balance;
		result.Pack();
		SendTo(link, ELIFE_EClerkMessage.TRANSFER_RESULT, reason, 0, result.AsString());
	}

	//------------------------------------------------------------------------------------------------
	protected void SendPaperResult(notnull ELIFE_NpcCharacterComponent link, ELIFE_EBankPaperKind kind, int reason, bool onGround)
	{
		string ground;
		if (onGround)
			ground = "1";

		SendTo(link, ELIFE_EClerkMessage.PAPER, reason, kind, ground);
	}

	//------------------------------------------------------------------------------------------------
	// Client
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	override bool OnGranted(notnull ELIFE_NpcCharacterComponent link)
	{
		return ELIFE_BankClerkDialogueController.StartVisit(this, link);
	}

	//------------------------------------------------------------------------------------------------
	//! Client-side. False if none arrived for the visit granted to this link.
	bool GetCustomerContext(notnull ELIFE_NpcCharacterComponent link, out ELIFE_EClerkAccountStatus accounts, out bool openedThisVisit)
	{
		if (m_iContextVisitId == 0 || m_iContextVisitId != link.GetGrantedVisitId(GetOwner()))
			return false;

		accounts = m_eContextAccounts;
		openedThisVisit = m_bContextOpenedThisVisit;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void OnMessage(notnull ELIFE_NpcCharacterComponent link, int visitId, int kind, int a, int b, string text)
	{
		switch (kind)
		{
			case ELIFE_EClerkMessage.CUSTOMER_CONTEXT:
				m_iContextVisitId = visitId;
				m_eContextAccounts = a;
				m_bContextOpenedThisVisit = b != 0;
				ELIFE_BankClerkDialogueController.OnCustomerContext(GetOwner());
				return;

			case ELIFE_EClerkMessage.ACCOUNT_OPENED:
				ELIFE_BankClerkDialogueController.OnAccountOpened(b != 0, a, text);
				return;

			case ELIFE_EClerkMessage.ACCOUNTS:
				ELIFE_TransferSlipForm accountsSlip = ELIFE_TransferSlipForm.GetOpen();
				if (accountsSlip)
					accountsSlip.OnClerkAccountsAnswer(a, text);

				return;

			case ELIFE_EClerkMessage.TRANSFER_QUOTE:
				ELIFE_TransferSlipForm quoteSlip = ELIFE_TransferSlipForm.GetOpen();
				if (quoteSlip)
					quoteSlip.OnTransferQuoteAnswer(a, text);

				return;

			case ELIFE_EClerkMessage.TRANSFER_RESULT:
				OnTransferResult(a, text);
				return;

			case ELIFE_EClerkMessage.PAPER:
				OnPaperResult(a, b, text == "1");
				return;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Client-side. If the slip is gone (the player walked off), the transfer still happened, so a hint says so.
	protected void OnTransferResult(int reason, string json)
	{
		ELIFE_ClerkTransferReceipt result = new ELIFE_ClerkTransferReceipt();
		result.ExpandFromRAW(json);

		ELIFE_TransferSlipForm slip = ELIFE_TransferSlipForm.GetOpen();
		if (slip)
			slip.OnTransferResultAnswer(reason, result.amount, result.fee, result.balance);
		else if (ELIFE_BankClerkDialogueController.OnTransferResult(reason, result.amount, result.fee, result.balance))
			return;
		else if (reason == ELIFE_EClerkReason.NONE)
			SCR_HintManagerComponent.ShowCustomHint(WidgetManager.Translate("#ELIFE-Clerk_TransferDone_Hint", result.amount), "#ELIFE-Clerk_TransferDone_Title");
		else if (reason == ELIFE_EClerkReason.UNKNOWN_OUTCOME)
			SCR_HintManagerComponent.ShowCustomHint("#ELIFE-Clerk_Reason_UnknownOutcome", "#ELIFE-Clerk_Title");
	}

	//------------------------------------------------------------------------------------------------
	//! Client-side. Where the paper went: into the inventory, or onto the counter when there was no room.
	protected void OnPaperResult(int reason, ELIFE_EBankPaperKind kind, bool onGround)
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
	void ~ELIFE_BankClerkComponent()
	{
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(Greet);
	}
}

//------------------------------------------------------------------------------------------------
//! Takes one visit's account lookup answer. Only the clerk holds it, so once that visit ends the answer goes nowhere.
class ELIFE_BankClerkCustomerLookup
{
	protected ELIFE_BankClerkComponent m_Clerk;
	//! Kept here: the managed-list answer doesn't name the character.
	protected string m_sCharacterId;
	ref ELIFE_BaseRestCallback m_Callback;

	//------------------------------------------------------------------------------------------------
	void ELIFE_BankClerkCustomerLookup(ELIFE_BankClerkComponent clerk, string characterId)
	{
		m_Clerk = clerk;
		m_sCharacterId = characterId;
	}

	//------------------------------------------------------------------------------------------------
	void OnAnswer(ELIFE_EApiStatusCode status, JsonApiStruct data)
	{
		if (m_Clerk)
			m_Clerk.OnCustomerAccounts(this, status, m_sCharacterId);
	}
}
