//------------------------------------------------------------------------------------------------
//! One of the customer's accounts as the transfer form shows it. Money is the Bridge's display text.
class ELIFE_ClerkAccount : JsonApiStruct
{
	string number;
	string balance;
	//! Only for checking an amount on the slip.
	int balanceCents;
	string bankName;
	string feeTerms;

	//------------------------------------------------------------------------------------------------
	void ELIFE_ClerkAccount()
	{
		RegV("number");
		RegV("balance");
		RegV("balanceCents");
		RegV("bankName");
		RegV("feeTerms");
	}
}

//------------------------------------------------------------------------------------------------
class ELIFE_ClerkAccountList : JsonApiStruct
{
	ref array<ref ELIFE_ClerkAccount> items = {};
	//! Who the player last sent money to, newest first, so the slip can offer them without typing.
	ref array<string> recent = {};

	//------------------------------------------------------------------------------------------------
	void ELIFE_ClerkAccountList()
	{
		RegV("items");
		RegV("recent");
	}
}

//------------------------------------------------------------------------------------------------
//! What a large transfer is for, asked by the clerk (see ELIFE_BankClerkComponent.GetPurposeThreshold).
//! Kept in the server log for now: the backend's transfer has no field for it yet.
enum ELIFE_ETransferPurpose
{
	//! Not answered; sent by the customer to put the transfer away.
	NONE,
	GOODS,
	RENT,
	GIFT,
	BUSINESS
}

//------------------------------------------------------------------------------------------------
//! What the confirm page shows. The server keeps its own copy; the client only sends the id back.
class ELIFE_ClerkTransferQuote : JsonApiStruct
{
	int quoteId;
	string fromNumber;
	string targetNumber;
	string targetBankName;
	int amount;
	string feeTerms;
	//! The clerk asks what it is for before booking; the customer answers instead of confirming.
	bool needsPurpose;

	//------------------------------------------------------------------------------------------------
	void ELIFE_ClerkTransferQuote()
	{
		RegV("needsPurpose");
		RegV("quoteId");
		RegV("fromNumber");
		RegV("targetNumber");
		RegV("targetBankName");
		RegV("amount");
		RegV("feeTerms");
	}
}

//------------------------------------------------------------------------------------------------
//! Server-only transfers for the clerk's current customer: their accounts, a quote checked against the backend, then the booking.
class ELIFE_BankClerkTransfers : Managed
{
	protected static const int QUOTE_LIFETIME_MS = 60000;
	protected static const int HTTP_NOT_FOUND = 404;
	protected static const int MAX_RECENT = 5;
	//! Longest TRANSFER_PROCESSING line (3.3 s) plus a pause.
	protected static const int PROCESSING_DELAY_MS = 3500;
	//! Longest reaction to a purpose (PURPOSE_GOODS, 4.8 s) plus a pause, so the booked line doesn't cut it off.
	protected static const int PURPOSE_DELAY_MS = 5000;

	protected ELIFE_BankClerkComponent m_Clerk;

	protected int m_iPlayerId;
	protected string m_sCharacterId;
	protected ref array<ref ELIFE_BankAccountSummary> m_aAccounts = {};
	protected ref ELIFE_BankListDto m_Banks;
	protected ref ELIFE_BaseRestCallback m_LoadCallback;
	protected bool m_bLoading;
	//! Only one load request is out at a time, even across visits, so an answer always belongs to the request it reads.
	protected bool m_bRequestInFlight;
	protected bool m_bRestartLoad;
	//! Outgoing transfers of every account, as "occurredAt|number" so they sort by time.
	protected ref array<string> m_aSentTo = {};
	protected int m_iHistoryIndex;

	protected ref ELIFE_BankAccountSummaryCallback m_TargetCallback;
	protected bool m_bQuoteInFlight;
	protected int m_iNextQuoteId = 1;
	protected ref ELIFE_ClerkTransferQuote m_Quote;
	protected int m_iQuoteTick;
	protected ELIFE_ETransferPurpose m_ePurpose;

	protected ref ELIFE_BankBooking m_Booking;
	protected int m_iBookingPlayerId;
	protected string m_sBookingFrom;
	//! The quote being booked and its purpose, for the receipt.
	protected ref ELIFE_ClerkTransferQuote m_BookingQuote;
	protected ELIFE_ETransferPurpose m_eBookingPurpose;

	//------------------------------------------------------------------------------------------------
	void ELIFE_BankClerkTransfers(ELIFE_BankClerkComponent clerk)
	{
		m_Clerk = clerk;
	}

	//------------------------------------------------------------------------------------------------
	//! When the visit ends. A booking already sent finishes and still answers.
	void Reset()
	{
		m_iPlayerId = 0;
		m_sCharacterId = "";
		m_aAccounts.Clear();
		m_Banks = null;
		m_bLoading = false;
		m_bRestartLoad = false;
		m_bQuoteInFlight = false;
		m_Quote = null;
		m_ePurpose = ELIFE_ETransferPurpose.NONE;
		GetGame().GetCallqueue().Remove(SendProcessedQuote);
		GetGame().GetCallqueue().Remove(BookAnsweredQuote);
	}

	//------------------------------------------------------------------------------------------------
	bool IsBooking()
	{
		return m_sBookingFrom != "";
	}

	//------------------------------------------------------------------------------------------------
	//! A fresh load each time the form opens, so balances are current.
	void RequestAccounts(int playerId, string characterId)
	{
		if (m_bLoading)
			return;

		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (!api || characterId == "")
		{
			SendAccounts(playerId, ELIFE_EClerkReason.UNAVAILABLE);
			return;
		}

		m_iPlayerId = playerId;
		m_sCharacterId = characterId;
		m_bLoading = true;

		//! A request from the last visit is still out; its answer starts this load.
		if (m_bRequestInFlight)
		{
			m_bRestartLoad = true;
			return;
		}

		StartLoad();
	}

	//------------------------------------------------------------------------------------------------
	protected void StartLoad()
	{
		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (!api)
		{
			m_bLoading = false;
			SendAccounts(m_iPlayerId, ELIFE_EClerkReason.UNAVAILABLE);
			return;
		}

		m_bRequestInFlight = true;
		m_LoadCallback = api.ListCharacterBankAccounts(m_sCharacterId, this, "OnLoadAccounts");
	}

	//------------------------------------------------------------------------------------------------
	//! False if the answer is no longer wanted: the visit ended, or a new load was asked for meanwhile.
	protected bool TakeAnswer()
	{
		m_bRequestInFlight = false;
		if (!m_bLoading)
			return false;

		if (m_bRestartLoad)
		{
			m_bRestartLoad = false;
			StartLoad();
			return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	void OnLoadAccounts(ELIFE_EApiStatusCode status, JsonApiStruct data, string characterId)
	{
		if (!TakeAnswer())
			return;

		if (status != ELIFE_EApiStatusCode.SUCCESS || !m_LoadCallback)
		{
			m_bLoading = false;
			SendAccounts(m_iPlayerId, ELIFE_EClerkReason.UNAVAILABLE);
			return;
		}

		ELIFE_BankAccountListDto list = new ELIFE_BankAccountListDto();
		list.ExpandFromArray(m_LoadCallback.GetData());

		//! The read has no ownership check of its own.
		m_aAccounts.Clear();
		foreach (ELIFE_BankAccountSummary account : list.items)
		{
			if (account && account.characterId == characterId && account.number != "" && !account.IsCompany())
				m_aAccounts.Insert(account.Copy());
		}

		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (!api)
		{
			m_bLoading = false;
			SendAccounts(m_iPlayerId, ELIFE_EClerkReason.UNAVAILABLE);
			return;
		}

		m_bRequestInFlight = true;
		m_LoadCallback = api.ListBanks(this, "OnLoadBanks");
	}

	//------------------------------------------------------------------------------------------------
	//! For the banks' names; each account carries its own fee terms.
	void OnLoadBanks(ELIFE_EApiStatusCode status, JsonApiStruct data)
	{
		if (!TakeAnswer())
			return;

		if (status != ELIFE_EApiStatusCode.SUCCESS || !m_LoadCallback)
		{
			m_bLoading = false;
			SendAccounts(m_iPlayerId, ELIFE_EClerkReason.UNAVAILABLE);
			return;
		}

		m_Banks = new ELIFE_BankListDto();
		m_Banks.ExpandFromArray(m_LoadCallback.GetData());

		m_aSentTo.Clear();
		m_iHistoryIndex = 0;
		LoadNextHistory();
	}

	//------------------------------------------------------------------------------------------------
	//! One statement after the other; the recent recipients are a convenience, so a failed one is skipped.
	protected void LoadNextHistory()
	{
		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (!api || !m_aAccounts.IsIndexValid(m_iHistoryIndex))
		{
			m_bLoading = false;
			SendAccounts(m_iPlayerId, ELIFE_EClerkReason.NONE);
			return;
		}

		m_bRequestInFlight = true;
		m_LoadCallback = api.ListBankAccountTransactions(m_aAccounts[m_iHistoryIndex].number, this, "OnLoadHistory");
	}

	//------------------------------------------------------------------------------------------------
	void OnLoadHistory(ELIFE_EApiStatusCode status, JsonApiStruct data, string accountNumber)
	{
		if (!TakeAnswer())
			return;

		if (status == ELIFE_EApiStatusCode.SUCCESS && m_LoadCallback)
		{
			ELIFE_BankStatementDto statement = new ELIFE_BankStatementDto();
			statement.ExpandFromArray(m_LoadCallback.GetData());
			foreach (ELIFE_BankAccountTransaction transaction : statement.items)
			{
				if (transaction && transaction.kind == ELIFE_BankAccountTransaction.KIND_TRANSFERRED_OUT && transaction.counterpartyNumber != "")
					m_aSentTo.Insert(transaction.occurredAt + "|" + transaction.counterpartyNumber);
			}
		}
		else
		{
			Print(string.Format("ELIFE_BankClerkTransfers | no history for %1, recent recipients incomplete", accountNumber), LogLevel.WARNING);
		}

		m_iHistoryIndex++;
		LoadNextHistory();
	}

	//------------------------------------------------------------------------------------------------
	//! Newest first, each number once, none of the player's own accounts.
	protected void FillRecent(notnull array<string> recent)
	{
		array<string> sentTo = {};
		sentTo.Copy(m_aSentTo);
		sentTo.Sort(true);

		foreach (string entry : sentTo)
		{
			int separator = entry.LastIndexOf("|");
			if (separator < 0)
				continue;

			string number = entry.Substring(separator + 1, entry.Length() - separator - 1);
			if (!recent.Contains(number) && !FindAccount(number))
				recent.Insert(number);

			if (recent.Count() >= MAX_RECENT)
				return;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void SendAccounts(int playerId, ELIFE_EClerkReason reason)
	{
		ELIFE_BankClerkCharacterComponent link = m_Clerk.GetLink(playerId);
		if (!link)
			return;

		ELIFE_ClerkAccountList list = new ELIFE_ClerkAccountList();
		if (reason == ELIFE_EClerkReason.NONE)
		{
			foreach (ELIFE_BankAccountSummary account : m_aAccounts)
			{
				ELIFE_ClerkAccount choice = new ELIFE_ClerkAccount();
				choice.number = account.number;
				choice.balance = account.balanceStr;
				choice.balanceCents = ELIFE_PhoneBankingService.MoneyTextToCents(account.balanceStr);
				choice.feeTerms = account.feeTermsStr;

				ELIFE_BankSummary bank = FindBank(account.bankId);
				if (bank)
					choice.bankName = bank.name;

				list.items.Insert(choice);
			}

			FillRecent(list.recent);
		}

		list.Pack();
		link.SendClerkAccounts(m_Clerk.GetRplId(), reason, list.AsString());
	}

	//------------------------------------------------------------------------------------------------
	//! Checks what it can here, then asks the backend whether the target exists. One quote at a time.
	void RequestQuote(int playerId, string fromNumber, string targetText, int amount)
	{
		if (m_bQuoteInFlight)
			return;

		//! A booking from the previous visit may still be running.
		if (IsBooking())
		{
			SendQuote(playerId, ELIFE_EClerkReason.ACCOUNT_BUSY, null);
			return;
		}

		ELIFE_EClerkReason reason = ValidateQuote(playerId, fromNumber, targetText, amount);
		if (reason != ELIFE_EClerkReason.NONE)
		{
			SendQuote(playerId, reason, null);
			return;
		}

		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (!api)
		{
			SendQuote(playerId, ELIFE_EClerkReason.UNAVAILABLE, null);
			return;
		}

		ELIFE_BankAccountSummary from = FindAccount(fromNumber);

		m_Quote = new ELIFE_ClerkTransferQuote();
		m_Quote.fromNumber = fromNumber;
		m_Quote.targetNumber = ELIFE_BankAccountNumber.Normalize(targetText);
		m_Quote.amount = amount;
		m_Quote.feeTerms = from.feeTermsStr;
		m_bQuoteInFlight = true;
		m_TargetCallback = api.GetBankAccount(m_Quote.targetNumber, this, "OnTargetLoaded");
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_EClerkReason ValidateQuote(int playerId, string fromNumber, string targetText, int amount)
	{
		if (playerId != m_iPlayerId || m_bLoading)
			return ELIFE_EClerkReason.UNAVAILABLE;

		ELIFE_BankAccountSummary from = FindAccount(fromNumber);
		if (!from)
			return ELIFE_EClerkReason.UNAVAILABLE;

		if (amount <= 0 || amount > ELIFE_ATMComponent.MAX_BOOKING_AMOUNT)
			return ELIFE_EClerkReason.INVALID_AMOUNT;

		string target = ELIFE_BankAccountNumber.Normalize(targetText);
		if (!ELIFE_BankAccountNumber.IsValid(target))
			return ELIFE_EClerkReason.INVALID_TARGET;

		if (target == fromNumber)
			return ELIFE_EClerkReason.SAME_ACCOUNT;

		//! The fee is unknown until booked.
		if (amount * 100 > ELIFE_PhoneBankingService.MoneyTextToCents(from.balanceStr))
			return ELIFE_EClerkReason.NOT_ENOUGH_MONEY;

		return ELIFE_EClerkReason.NONE;
	}

	//------------------------------------------------------------------------------------------------
	//! The target's balance and owner stay on the server; only its bank's name goes into the quote.
	void OnTargetLoaded(ELIFE_EApiStatusCode status, JsonApiStruct data, string targetNumber)
	{
		if (!m_bQuoteInFlight || !m_Quote || targetNumber != m_Quote.targetNumber)
			return;

		m_bQuoteInFlight = false;

		ELIFE_BankAccountSummary target = ELIFE_BankAccountSummary.Cast(data);
		if (status != ELIFE_EApiStatusCode.SUCCESS || !target || target.number != targetNumber)
		{
			ELIFE_EClerkReason reason = ELIFE_EClerkReason.UNAVAILABLE;
			if (m_TargetCallback && m_TargetCallback.GetHttpCode() == HTTP_NOT_FOUND)
				reason = ELIFE_EClerkReason.TARGET_NOT_FOUND;

			m_Quote = null;
			SendQuote(m_iPlayerId, reason, null);
			return;
		}

		ELIFE_BankSummary bank = FindBank(target.bankId);
		if (bank)
			m_Quote.targetBankName = bank.name;

		m_Quote.quoteId = m_iNextQuoteId++;

		//! A large amount waits for its purpose; the answer books it (RequestPurpose). Moving money between own accounts has none to declare.
		if (m_Quote.amount >= m_Clerk.GetPurposeThreshold() && !IsFreeTransfer(FindAccount(m_Quote.fromNumber), target))
		{
			m_Quote.needsPurpose = true;
			m_iQuoteTick = System.GetTickCount();
			m_Clerk.Say(ELIFE_EClerkLine.ASK_PURPOSE);
			SendQuote(m_iPlayerId, ELIFE_EClerkReason.NONE, m_Quote);
			return;
		}

		//! Stays in flight while he "enters" it, so a second slip waits.
		m_bQuoteInFlight = true;
		m_Clerk.Say(ELIFE_EClerkLine.TRANSFER_PROCESSING);
		GetGame().GetCallqueue().CallLater(SendProcessedQuote, PROCESSING_DELAY_MS, false, m_Quote.quoteId);
	}

	//------------------------------------------------------------------------------------------------
	//! Dropped if the visit ended meanwhile.
	protected void SendProcessedQuote(int quoteId)
	{
		if (!m_Quote || m_Quote.quoteId != quoteId)
			return;

		m_bQuoteInFlight = false;
		m_iQuoteTick = System.GetTickCount();
		SendQuote(m_iPlayerId, ELIFE_EClerkReason.NONE, m_Quote);
	}

	//------------------------------------------------------------------------------------------------
	protected void SendQuote(int playerId, ELIFE_EClerkReason reason, ELIFE_ClerkTransferQuote quote)
	{
		ELIFE_BankClerkCharacterComponent link = m_Clerk.GetLink(playerId);
		if (!link)
			return;

		string json;
		if (quote)
		{
			ELIFE_ClerkTransferQuote copy = new ELIFE_ClerkTransferQuote();
			copy.quoteId = quote.quoteId;
			copy.fromNumber = quote.fromNumber;
			copy.targetNumber = quote.targetNumber;
			copy.targetBankName = quote.targetBankName;
			copy.amount = quote.amount;
			copy.feeTerms = quote.feeTerms;
			copy.needsPurpose = quote.needsPurpose;
			copy.Pack();
			json = copy.AsString();
		}

		link.SendTransferQuote(m_Clerk.GetRplId(), reason, json);
	}

	//------------------------------------------------------------------------------------------------
	//! A quote is used once. The account is locked against ATMs and other clerks until the backend answers.
	void RequestConfirm(int playerId, int quoteId)
	{
		if (IsBooking())
		{
			SendResult(playerId, ELIFE_EClerkReason.ACCOUNT_BUSY);
			return;
		}

		//! A quote waiting for its purpose can't be confirmed past the question.
		if (m_bQuoteInFlight || !m_Quote || m_Quote.needsPurpose || m_Quote.quoteId != quoteId || playerId != m_iPlayerId || System.GetTickCount() - m_iQuoteTick > QUOTE_LIFETIME_MS)
		{
			SendResult(playerId, ELIFE_EClerkReason.QUOTE_EXPIRED);
			return;
		}

		ELIFE_ClerkTransferQuote quote = m_Quote;
		m_Quote = null;
		StartBooking(playerId, quote);
	}

	//------------------------------------------------------------------------------------------------
	//! The answer to the clerk's question about a large transfer. NONE puts the transfer away.
	void RequestPurpose(int playerId, int quoteId, ELIFE_ETransferPurpose purpose)
	{
		if (m_bQuoteInFlight || !m_Quote || !m_Quote.needsPurpose || m_Quote.quoteId != quoteId || playerId != m_iPlayerId)
		{
			if (purpose != ELIFE_ETransferPurpose.NONE)
				SendResult(playerId, ELIFE_EClerkReason.QUOTE_EXPIRED);

			return;
		}

		if (purpose == ELIFE_ETransferPurpose.NONE)
		{
			m_Quote = null;
			return;
		}

		ELIFE_EClerkLine line = GetPurposeLine(purpose);
		if (line == ELIFE_EClerkLine.NONE || System.GetTickCount() - m_iQuoteTick > QUOTE_LIFETIME_MS)
		{
			m_Quote = null;
			SendResult(playerId, ELIFE_EClerkReason.QUOTE_EXPIRED);
			return;
		}

		//! In flight while he "enters" it, like an ordinary slip.
		m_ePurpose = purpose;
		m_bQuoteInFlight = true;
		m_Clerk.Say(line);
		GetGame().GetCallqueue().CallLater(BookAnsweredQuote, PURPOSE_DELAY_MS, false, quoteId);
	}

	//------------------------------------------------------------------------------------------------
	//! His reaction to the answer; each ends with putting the transfer through.
	static ELIFE_EClerkLine GetPurposeLine(ELIFE_ETransferPurpose purpose)
	{
		switch (purpose)
		{
			case ELIFE_ETransferPurpose.GOODS: return ELIFE_EClerkLine.PURPOSE_GOODS;
			case ELIFE_ETransferPurpose.RENT: return ELIFE_EClerkLine.PURPOSE_RENT;
			case ELIFE_ETransferPurpose.GIFT: return ELIFE_EClerkLine.PURPOSE_GIFT;
			case ELIFE_ETransferPurpose.BUSINESS: return ELIFE_EClerkLine.PURPOSE_BUSINESS;
		}

		return ELIFE_EClerkLine.NONE;
	}

	//------------------------------------------------------------------------------------------------
	//! The paper's name for a purpose.
	static string GetPurposeText(ELIFE_ETransferPurpose purpose)
	{
		switch (purpose)
		{
			case ELIFE_ETransferPurpose.GOODS: return "#ELIFE-Clerk_Purpose_Goods";
			case ELIFE_ETransferPurpose.RENT: return "#ELIFE-Clerk_Purpose_Rent";
			case ELIFE_ETransferPurpose.GIFT: return "#ELIFE-Clerk_Purpose_Gift";
			case ELIFE_ETransferPurpose.BUSINESS: return "#ELIFE-Clerk_Purpose_Business";
		}

		return "";
	}

	//------------------------------------------------------------------------------------------------
	//! Dropped if the visit ended meanwhile.
	protected void BookAnsweredQuote(int quoteId)
	{
		if (!m_Quote || m_Quote.quoteId != quoteId)
			return;

		m_bQuoteInFlight = false;
		ELIFE_ClerkTransferQuote quote = m_Quote;
		m_Quote = null;
		if (IsBooking())
		{
			SendResult(m_iPlayerId, ELIFE_EClerkReason.ACCOUNT_BUSY);
			return;
		}

		StartBooking(m_iPlayerId, quote);
	}

	//------------------------------------------------------------------------------------------------
	//! The account is locked against ATMs and other clerks until the backend answers.
	protected void StartBooking(int playerId, notnull ELIFE_ClerkTransferQuote quote)
	{
		if (!ELIFE_Api.GetInstance())
		{
			SendResult(playerId, ELIFE_EClerkReason.UNAVAILABLE);
			return;
		}

		if (!ELIFE_ATMComponent.TryLockAccount(quote.fromNumber))
		{
			SendResult(playerId, ELIFE_EClerkReason.ACCOUNT_BUSY);
			return;
		}

		m_iBookingPlayerId = playerId;
		m_sBookingFrom = quote.fromNumber;
		m_BookingQuote = quote;
		m_eBookingPurpose = ELIFE_ETransferPurpose.NONE;
		if (quote.needsPurpose)
			m_eBookingPurpose = m_ePurpose;

		m_Booking = new ELIFE_BankBooking(ELIFE_EBankBookingKind.TRANSFER, quote.fromNumber, quote.amount * 100, m_sCharacterId, m_Clerk.GetSourceId(), quote.targetNumber);
		string purpose;
		if (quote.needsPurpose)
			purpose = ", purpose " + typename.EnumToString(ELIFE_ETransferPurpose, m_eBookingPurpose);

		Print(string.Format("ELIFE_BankClerkTransfers | transfer %1 from %2 to %3 for player %4%5", quote.amount, quote.fromNumber, quote.targetNumber, playerId, purpose), LogLevel.NORMAL);
		m_Booking.Start(this, "OnBankBookingDone");
	}

	//------------------------------------------------------------------------------------------------
	void OnBankBookingDone(ELIFE_BankBooking booking)
	{
		if (booking != m_Booking)
			return;

		string fromNumber = m_sBookingFrom;
		ELIFE_ClerkTransferQuote quote = m_BookingQuote;
		m_BookingQuote = null;
		ELIFE_ATMComponent.UnlockAccount(fromNumber);
		int playerId = m_iBookingPlayerId;
		m_sBookingFrom = "";
		m_iBookingPlayerId = 0;
		m_Booking = null;

		ELIFE_TransactionResult result = booking.GetResult();
		ELIFE_BankAccountSummary from = FindAccount(fromNumber);
		switch (booking.GetOutcome())
		{
			case ELIFE_EBankBookingOutcome.BOOKED:
				Print(string.Format("ELIFE_BankClerkTransfers | transferred %1, fee %2, from %3", result.amountStr, result.feeStr, fromNumber), LogLevel.NORMAL);
				if (from)
					from.balanceStr = result.newBalanceStr;

				KeepReceipt(playerId, quote, from, result);

				SendResult(playerId, ELIFE_EClerkReason.NONE, result.amountStr, result.feeStr, result.newBalanceStr);
				return;

			case ELIFE_EBankBookingOutcome.CONFLICT:
				if (booking.GetProblemCode() == "" || booking.GetProblemCode() == ELIFE_BankBooking.CODE_INSUFFICIENT_BALANCE)
					SendResult(playerId, ELIFE_EClerkReason.NOT_ENOUGH_MONEY);
				else
					SendResult(playerId, ELIFE_EClerkReason.ACCOUNT_BUSY);

				return;

			case ELIFE_EBankBookingOutcome.DENIED:
			case ELIFE_EBankBookingOutcome.INVALID:
				SendResult(playerId, ELIFE_EClerkReason.FAILED);
				return;
		}

		SendResult(playerId, ELIFE_EClerkReason.UNKNOWN_OUTCOME);
	}

	//------------------------------------------------------------------------------------------------
	//! The receipt he offers once it's booked, built here from the booking itself.
	protected void KeepReceipt(int playerId, ELIFE_ClerkTransferQuote quote, ELIFE_BankAccountSummary from, notnull ELIFE_TransactionResult result)
	{
		if (!quote)
			return;

		ELIFE_BankPaper paper = new ELIFE_BankPaper();
		paper.kind = ELIFE_EBankPaperKind.TRANSFER_RECEIPT;
		paper.bankName = m_Clerk.GetBankName();
		if (from)
		{
			ELIFE_BankSummary bank = FindBank(from.bankId);
			if (bank)
				paper.bankName = bank.name;
		}

		paper.accountNumber = quote.fromNumber;
		paper.targetNumber = quote.targetNumber;
		paper.targetBankName = quote.targetBankName;
		paper.amount = result.amountStr;
		paper.fee = result.feeStr;
		//! No balance: the receipt is shown to others as proof of payment.
		paper.purpose = GetPurposeText(m_eBookingPurpose);
		paper.bookingRef = result.transactionId;
		paper.StampNow();
		m_Clerk.KeepPaper(playerId, paper);
	}

	//------------------------------------------------------------------------------------------------
	protected void SendResult(int playerId, ELIFE_EClerkReason reason, string amount = "", string fee = "", string balance = "")
	{
		ELIFE_BankClerkCharacterComponent link = m_Clerk.GetLink(playerId);
		if (link)
			link.SendTransferResult(m_Clerk.GetRplId(), reason, amount, fee, balance);
	}

	//------------------------------------------------------------------------------------------------
	//! Mirrors the backend's rule (#188): both personal accounts of this character at one bank.
	protected bool IsFreeTransfer(ELIFE_BankAccountSummary from, notnull ELIFE_BankAccountSummary target)
	{
		return from && target.characterId == m_sCharacterId && !target.IsCompany() && target.bankId == from.bankId;
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_BankAccountSummary FindAccount(string number)
	{
		foreach (ELIFE_BankAccountSummary account : m_aAccounts)
		{
			if (account.number == number)
				return account;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_BankSummary FindBank(string bankId)
	{
		if (!m_Banks)
			return null;

		foreach (ELIFE_BankSummary bank : m_Banks.items)
		{
			if (bank && bank.bankId == bankId)
				return bank;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! A clerk deleted mid-booking must not leave the account locked for the ATMs.
	void ~ELIFE_BankClerkTransfers()
	{
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(SendProcessedQuote);
			GetGame().GetCallqueue().Remove(BookAnsweredQuote);
		}

		if (m_Booking)
			m_Booking.Cancel();

		if (m_sBookingFrom != "")
		{
			ELIFE_ATMComponent.UnlockAccount(m_sBookingFrom);
			Print(string.Format("ELIFE_BankClerkTransfers | clerk deleted during a booking, UNRESOLVED: transfer from %1 for player %2", m_sBookingFrom, m_iBookingPlayerId), LogLevel.ERROR);
		}
	}
}
