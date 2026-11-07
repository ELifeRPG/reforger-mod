//------------------------------------------------------------------------------------------------
//! One of the customer's accounts as the transfer form shows it, with the fee terms of its bank for the live preview.
class ELIFE_ClerkAccount : JsonApiStruct
{
	string number;
	int balanceCents;
	string bankName;
	int feeBaseCents;
	float feeMultiplier;

	//------------------------------------------------------------------------------------------------
	void ELIFE_ClerkAccount()
	{
		RegV("number");
		RegV("balanceCents");
		RegV("bankName");
		RegV("feeBaseCents");
		RegV("feeMultiplier");
	}

	//------------------------------------------------------------------------------------------------
	//! Rounded to whole cents, like everything the player sees.
	int GetFeeCents(int amount)
	{
		return feeBaseCents + Math.Round(amount * 100 * feeMultiplier);
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
//! What the confirm page shows. The server keeps its own copy; the client only sends the id back.
class ELIFE_ClerkTransferQuote : JsonApiStruct
{
	int quoteId;
	string fromNumber;
	string targetNumber;
	string targetBankName;
	int amount;
	int feeCents;

	//------------------------------------------------------------------------------------------------
	void ELIFE_ClerkTransferQuote()
	{
		RegV("quoteId");
		RegV("fromNumber");
		RegV("targetNumber");
		RegV("targetBankName");
		RegV("amount");
		RegV("feeCents");
	}
}

//------------------------------------------------------------------------------------------------
//! Server-only transfers for the clerk's current customer: their accounts, a quote checked against the backend, then the booking.
class ELIFE_BankClerkTransfers : Managed
{
	protected static const int QUOTE_LIFETIME_MS = 60000;
	protected static const int HTTP_NOT_FOUND = 404;
	protected static const int HTTP_CLIENT_ERROR_MIN = 400;
	protected static const int HTTP_SERVER_ERROR_MIN = 500;
	protected static const int MAX_RECENT = 5;

	protected ELIFE_BankClerkComponent m_Clerk;

	protected int m_iPlayerId;
	protected string m_sCharacterId;
	protected ref array<ref ELIFE_BankAccountSummary> m_aAccounts = {};
	protected ref ELIFE_BankListDto m_Banks;
	protected ref ELIFE_BaseRestCallback m_LoadCallback;
	protected bool m_bLoading;
	//! Outgoing transfers of every account, as "occurredAt|number" so they sort by time.
	protected ref array<string> m_aSentTo = {};
	protected int m_iHistoryIndex;

	protected ref ELIFE_BankAccountSummaryCallback m_TargetCallback;
	protected bool m_bQuoteInFlight;
	protected int m_iNextQuoteId = 1;
	protected ref ELIFE_ClerkTransferQuote m_Quote;
	protected int m_iQuoteTick;

	protected ref ELIFE_TransactionResultCallback m_TransferCallback;
	protected int m_iBookingPlayerId;
	protected string m_sBookingFrom;

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
		m_bQuoteInFlight = false;
		m_Quote = null;
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
		m_LoadCallback = api.ListCharacterBankAccounts(characterId, this, "OnLoadAccounts");
	}

	//------------------------------------------------------------------------------------------------
	void OnLoadAccounts(ELIFE_EApiStatusCode status, JsonApiStruct data, string characterId)
	{
		if (!m_bLoading || characterId != m_sCharacterId)
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

		m_LoadCallback = api.ListBanks(this, "OnLoadBanks");
	}

	//------------------------------------------------------------------------------------------------
	//! Every account's fee depends on its own bank, not on the clerk's.
	void OnLoadBanks(ELIFE_EApiStatusCode status, JsonApiStruct data)
	{
		if (!m_bLoading)
			return;

		m_bLoading = false;
		if (status != ELIFE_EApiStatusCode.SUCCESS || !m_LoadCallback)
		{
			SendAccounts(m_iPlayerId, ELIFE_EClerkReason.UNAVAILABLE);
			return;
		}

		m_Banks = new ELIFE_BankListDto();
		m_Banks.ExpandFromArray(m_LoadCallback.GetData());

		m_aSentTo.Clear();
		m_iHistoryIndex = 0;
		m_bLoading = true;
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

		m_LoadCallback = api.ListBankAccountTransactions(m_aAccounts[m_iHistoryIndex].number, this, "OnLoadHistory");
	}

	//------------------------------------------------------------------------------------------------
	void OnLoadHistory(ELIFE_EApiStatusCode status, JsonApiStruct data, string accountNumber)
	{
		if (!m_bLoading || !m_aAccounts.IsIndexValid(m_iHistoryIndex) || accountNumber != m_aAccounts[m_iHistoryIndex].number)
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
				ELIFE_BankSummary bank = FindBank(account.bankId);
				if (!bank)
					continue;

				ELIFE_ClerkAccount choice = new ELIFE_ClerkAccount();
				choice.number = account.number;
				choice.balanceCents = ELIFE_PhoneBankingService.ToCents(account.balance);
				choice.bankName = bank.name;
				choice.feeBaseCents = ELIFE_PhoneBankingService.ToCents(bank.transactionFeeBase);
				choice.feeMultiplier = bank.transactionFeeMultiplier;
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
		ELIFE_BankSummary bank = FindBank(from.bankId);

		m_Quote = new ELIFE_ClerkTransferQuote();
		m_Quote.fromNumber = fromNumber;
		m_Quote.targetNumber = ELIFE_BankAccountNumber.Normalize(targetText);
		m_Quote.amount = amount;
		m_Quote.feeCents = GetFeeCents(bank, amount);
		m_bQuoteInFlight = true;
		m_TargetCallback = api.GetBankAccount(m_Quote.targetNumber, this, "OnTargetLoaded");
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_EClerkReason ValidateQuote(int playerId, string fromNumber, string targetText, int amount)
	{
		if (playerId != m_iPlayerId || m_bLoading)
			return ELIFE_EClerkReason.UNAVAILABLE;

		ELIFE_BankAccountSummary from = FindAccount(fromNumber);
		if (!from || !FindBank(from.bankId))
			return ELIFE_EClerkReason.UNAVAILABLE;

		if (amount <= 0 || amount > ELIFE_ATMComponent.MAX_BOOKING_AMOUNT)
			return ELIFE_EClerkReason.INVALID_AMOUNT;

		string target = ELIFE_BankAccountNumber.Normalize(targetText);
		if (!ELIFE_BankAccountNumber.IsValid(target))
			return ELIFE_EClerkReason.INVALID_TARGET;

		if (target == fromNumber)
			return ELIFE_EClerkReason.SAME_ACCOUNT;

		ELIFE_BankSummary bank = FindBank(from.bankId);
		int totalCents = amount * 100 + GetFeeCents(bank, amount);
		if (totalCents > ELIFE_PhoneBankingService.ToCents(from.balance))
			return ELIFE_EClerkReason.NOT_ENOUGH_MONEY;

		return ELIFE_EClerkReason.NONE;
	}

	//------------------------------------------------------------------------------------------------
	//! Rounded to whole cents, the same as ELIFE_ClerkAccount.GetFeeCents() on the slip.
	protected static int GetFeeCents(notnull ELIFE_BankSummary bank, int amount)
	{
		return ELIFE_PhoneBankingService.ToCents(bank.transactionFeeBase) + Math.Round(amount * 100 * bank.transactionFeeMultiplier);
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
			copy.feeCents = quote.feeCents;
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
			SendResult(playerId, ELIFE_EClerkReason.ACCOUNT_BUSY, 0, 0, 0);
			return;
		}

		if (!m_Quote || m_Quote.quoteId != quoteId || playerId != m_iPlayerId || System.GetTickCount() - m_iQuoteTick > QUOTE_LIFETIME_MS)
		{
			SendResult(playerId, ELIFE_EClerkReason.QUOTE_EXPIRED, 0, 0, 0);
			return;
		}

		ELIFE_ClerkTransferQuote quote = m_Quote;
		m_Quote = null;

		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (!api)
		{
			SendResult(playerId, ELIFE_EClerkReason.UNAVAILABLE, 0, 0, 0);
			return;
		}

		if (!ELIFE_ATMComponent.TryLockAccount(quote.fromNumber))
		{
			SendResult(playerId, ELIFE_EClerkReason.ACCOUNT_BUSY, 0, 0, 0);
			return;
		}

		ELIFE_TransferRequest body = new ELIFE_TransferRequest();
		body.amount = quote.amount;
		body.characterId = m_sCharacterId;
		body.targetNumber = quote.targetNumber;

		m_iBookingPlayerId = playerId;
		m_sBookingFrom = quote.fromNumber;
		Print(string.Format("ELIFE_BankClerkTransfers | transfer %1 from %2 to %3 for player %4", quote.amount, quote.fromNumber, quote.targetNumber, playerId), LogLevel.NORMAL);
		m_TransferCallback = api.TransferBankAccountFunds(body, quote.fromNumber, this, "OnTransferred");
	}

	//------------------------------------------------------------------------------------------------
	//! A refusal (4xx) booked nothing. Anything else may or may not have booked, and there's no safe retry yet, so the player is told to check.
	void OnTransferred(ELIFE_EApiStatusCode status, JsonApiStruct data, string fromNumber)
	{
		if (fromNumber != m_sBookingFrom)
			return;

		ELIFE_ATMComponent.UnlockAccount(fromNumber);
		int playerId = m_iBookingPlayerId;
		m_sBookingFrom = "";
		m_iBookingPlayerId = 0;

		ELIFE_TransactionResult result = ELIFE_TransactionResult.Cast(data);
		if (status == ELIFE_EApiStatusCode.SUCCESS && result)
		{
			Print(string.Format("ELIFE_BankClerkTransfers | transferred %1, fee %2, from %3", result.amount, result.fee, fromNumber), LogLevel.NORMAL);
			int amountCents = ELIFE_PhoneBankingService.ToCents(result.amount);
			int balanceCents = ELIFE_PhoneBankingService.ToCents(result.newBalance);
			ELIFE_BankAccountSummary from = FindAccount(fromNumber);
			if (from)
				from.balance = result.newBalance;

			SendResult(playerId, ELIFE_EClerkReason.NONE, amountCents, ELIFE_PhoneBankingService.ToCents(result.fee), balanceCents);
			return;
		}

		int httpCode;
		if (m_TransferCallback)
			httpCode = m_TransferCallback.GetHttpCode();

		Print(string.Format("ELIFE_BankClerkTransfers | transfer from %1 failed, HTTP %2", fromNumber, httpCode), LogLevel.WARNING);
		if (httpCode >= HTTP_CLIENT_ERROR_MIN && httpCode < HTTP_SERVER_ERROR_MIN)
			SendResult(playerId, ELIFE_EClerkReason.FAILED, 0, 0, 0);
		else
			SendResult(playerId, ELIFE_EClerkReason.UNKNOWN_OUTCOME, 0, 0, 0);
	}

	//------------------------------------------------------------------------------------------------
	protected void SendResult(int playerId, ELIFE_EClerkReason reason, int amountCents, int feeCents, int balanceCents)
	{
		ELIFE_BankClerkCharacterComponent link = m_Clerk.GetLink(playerId);
		if (link)
			link.SendTransferResult(m_Clerk.GetRplId(), reason, amountCents, feeCents, balanceCents);
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
}
