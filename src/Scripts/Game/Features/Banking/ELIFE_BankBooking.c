//------------------------------------------------------------------------------------------------
enum ELIFE_EBankBookingKind
{
	DEPOSIT,
	WITHDRAW,
	TRANSFER
}

//------------------------------------------------------------------------------------------------
//! Only UNKNOWN leaves the outcome open; every other one is definite.
enum ELIFE_EBankBookingOutcome
{
	BOOKED,
	//! 409 to the end: nothing was booked. Too little money, or the account stayed busy.
	CONFLICT,
	//! 403/404: not allowed, or no such account. Nothing was booked.
	DENIED,
	//! 400/422: the request itself was refused. Nothing was booked; a bug, not a player error.
	INVALID,
	//! No answer that settles it, even after every resend.
	UNKNOWN
}

//------------------------------------------------------------------------------------------------
//! Server-only. One booking, resent under the same Idempotency-Key until definite, so a resend never books twice.
class ELIFE_BankBooking : Managed
{
	static const string CODE_INSUFFICIENT_BALANCE = "insufficient_balance";

	protected static const int HTTP_BAD_REQUEST = 400;
	protected static const int HTTP_FORBIDDEN = 403;
	protected static const int HTTP_NOT_FOUND = 404;
	protected static const int HTTP_CONFLICT = 409;
	protected static const int HTTP_UNPROCESSABLE = 422;
	//! Resends wait 2.5, 5, 10 and 20 s.
	protected static const int MAX_RESENDS = 4;
	protected static const int FIRST_RESEND_DELAY_MS = 2500;

	//! Keys are scoped to the game server for 24 hours, so they must differ per server start.
	protected static int s_iServerStart;
	protected static int s_iCounter;

	protected ELIFE_EBankBookingKind m_eKind;
	protected string m_sAccountNumber;
	protected string m_sKey;
	protected ref ELIFE_DepositRequest m_Deposit;
	protected ref ELIFE_WithdrawRequest m_Withdraw;
	protected ref ELIFE_TransferRequest m_Transfer;

	protected Managed m_Owner;
	protected string m_sDoneMethod;
	protected string m_sResendMethod;
	protected ref ELIFE_BaseRestCallback m_Callback;
	protected int m_iAttempts;
	protected bool m_bDone;

	protected ELIFE_EBankBookingOutcome m_eOutcome = ELIFE_EBankBookingOutcome.UNKNOWN;
	protected ref ELIFE_TransactionResult m_Result;
	protected int m_iHttpCode;
	protected int m_iRestResult;
	protected string m_sProblemCode;

	//------------------------------------------------------------------------------------------------
	//! The generated client joins headers with commas, so the key can't contain one.
	void ELIFE_BankBooking(ELIFE_EBankBookingKind kind, string accountNumber, int amountCents, string characterId, string source, string targetNumber = "")
	{
		m_eKind = kind;
		m_sAccountNumber = accountNumber;

		if (s_iServerStart == 0)
			s_iServerStart = System.GetUnixTime();

		s_iCounter++;
		source.Replace(",", "_");
		m_sKey = string.Format("%1-%2-%3", source, s_iServerStart, s_iCounter);

		switch (kind)
		{
			case ELIFE_EBankBookingKind.DEPOSIT:
				m_Deposit = new ELIFE_DepositRequest();
				m_Deposit.amountCents = amountCents;
				m_Deposit.characterId = characterId;
				m_Deposit.source = source;
				break;

			case ELIFE_EBankBookingKind.WITHDRAW:
				m_Withdraw = new ELIFE_WithdrawRequest();
				m_Withdraw.amountCents = amountCents;
				m_Withdraw.characterId = characterId;
				m_Withdraw.source = source;
				break;

			case ELIFE_EBankBookingKind.TRANSFER:
				m_Transfer = new ELIFE_TransferRequest();
				m_Transfer.amountCents = amountCents;
				m_Transfer.characterId = characterId;
				m_Transfer.targetNumber = targetNumber;
				m_Transfer.source = source;
				break;
		}
	}

	//------------------------------------------------------------------------------------------------
	void Start(notnull Managed owner, string doneMethod, string resendMethod = "")
	{
		m_Owner = owner;
		m_sDoneMethod = doneMethod;
		m_sResendMethod = resendMethod;
		Send();
	}

	//------------------------------------------------------------------------------------------------
	//! Stops resending; the owner hears nothing more. The outcome stays whatever it was.
	void Cancel()
	{
		m_bDone = true;
		m_Owner = null;
		GetGame().GetCallqueue().Remove(Send);
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_EBankBookingOutcome GetOutcome()
	{
		return m_eOutcome;
	}

	//------------------------------------------------------------------------------------------------
	//! Set only when BOOKED.
	ELIFE_TransactionResult GetResult()
	{
		return m_Result;
	}

	//------------------------------------------------------------------------------------------------
	//! The problem's code from the last refusal, if the error body could be read. Empty otherwise.
	string GetProblemCode()
	{
		return m_sProblemCode;
	}

	//------------------------------------------------------------------------------------------------
	string Describe()
	{
		return string.Format("%1 on %2, key %3, attempts %4, outcome %5, last HTTP=%6 RestResult=%7 code '%8'", typename.EnumToString(ELIFE_EBankBookingKind, m_eKind), m_sAccountNumber, m_sKey, m_iAttempts, typename.EnumToString(ELIFE_EBankBookingOutcome, m_eOutcome), m_iHttpCode, m_iRestResult, m_sProblemCode);
	}

	//------------------------------------------------------------------------------------------------
	//! Every attempt sends the same body under the same key.
	protected void Send()
	{
		if (m_bDone)
			return;

		m_iAttempts++;
		m_Callback = null;

		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (!api)
		{
				OnAnswer(ELIFE_EApiStatusCode.ERROR, null, m_sAccountNumber);
			return;
		}

		switch (m_eKind)
		{
			case ELIFE_EBankBookingKind.DEPOSIT:
				m_Callback = api.DepositToBankAccount(m_Deposit, m_sAccountNumber, m_sKey, this, "OnAnswer");
				break;

			case ELIFE_EBankBookingKind.WITHDRAW:
				m_Callback = api.WithdrawFromBankAccount(m_Withdraw, m_sAccountNumber, m_sKey, this, "OnAnswer");
				break;

			case ELIFE_EBankBookingKind.TRANSFER:
				m_Callback = api.TransferBankAccountFunds(m_Transfer, m_sAccountNumber, m_sKey, this, "OnAnswer");
				break;
		}
	}

	//------------------------------------------------------------------------------------------------
	void OnAnswer(ELIFE_EApiStatusCode status, JsonApiStruct data, string accountNumber)
	{
		if (m_bDone)
			return;

		m_iHttpCode = 0;
		m_iRestResult = 0;
		m_sProblemCode = "";
		if (m_Callback)
		{
			m_iHttpCode = m_Callback.GetHttpCode();
			m_iRestResult = m_Callback.GetRestResult();
		}

		ELIFE_TransactionResult result = ELIFE_TransactionResult.Cast(data);
		if (status == ELIFE_EApiStatusCode.SUCCESS && result && result.transactionId != "")
		{
			m_Result = result;
			Finish(ELIFE_EBankBookingOutcome.BOOKED);
			return;
		}

		bool retryable;
		if (status != ELIFE_EApiStatusCode.SUCCESS)
			retryable = ReadProblem();

		switch (m_iHttpCode)
		{
			case HTTP_FORBIDDEN:
			case HTTP_NOT_FOUND:
				Finish(ELIFE_EBankBookingOutcome.DENIED);
				return;

			case HTTP_BAD_REQUEST:
			case HTTP_UNPROCESSABLE:
				Print(string.Format("ELIFE_BankBooking | request refused as invalid: %1", Describe()), LogLevel.ERROR);
				Finish(ELIFE_EBankBookingOutcome.INVALID);
				return;

			case HTTP_CONFLICT:
				//! A refused booking isn't remembered under its key, so resending a 409 is safe.
				if (m_sProblemCode == CODE_INSUFFICIENT_BALANCE || (m_sProblemCode != "" && !retryable))
				{
					Finish(ELIFE_EBankBookingOutcome.CONFLICT);
					return;
				}

				ResendOr(ELIFE_EBankBookingOutcome.CONFLICT);
				return;
		}

		ResendOr(ELIFE_EBankBookingOutcome.UNKNOWN);
	}

	//------------------------------------------------------------------------------------------------
	//! The engine may not keep the error body; then code stays empty.
	protected bool ReadProblem()
	{
		if (!m_Callback)
			return false;

		string body = m_Callback.GetData();
		if (!body.StartsWith("{"))
			return false;

		ELIFE_ProblemDetails problem = new ELIFE_ProblemDetails();
		problem.ExpandFromRAW(body);

		m_sProblemCode = problem.code;
		return problem.retryable;
	}

	//------------------------------------------------------------------------------------------------
	protected void ResendOr(ELIFE_EBankBookingOutcome finalOutcome)
	{
		int resend = m_iAttempts - 1;
		if (resend >= MAX_RESENDS)
		{
			Finish(finalOutcome);
			return;
		}

		Print(string.Format("ELIFE_BankBooking | resending: %1", Describe()), LogLevel.WARNING);
		if (m_Owner && m_sResendMethod != "")
			GetGame().GetCallqueue().CallByName(m_Owner, m_sResendMethod, this);

		GetGame().GetCallqueue().CallLater(Send, FIRST_RESEND_DELAY_MS << resend);
	}

	//------------------------------------------------------------------------------------------------
	protected void Finish(ELIFE_EBankBookingOutcome outcome)
	{
		m_eOutcome = outcome;
		m_bDone = true;
		m_Callback = null;

		LogLevel level = LogLevel.NORMAL;
		if (outcome == ELIFE_EBankBookingOutcome.UNKNOWN || outcome == ELIFE_EBankBookingOutcome.INVALID)
			level = LogLevel.ERROR;

		Print(string.Format("ELIFE_BankBooking | done: %1", Describe()), level);

		if (m_Owner && m_sDoneMethod != "")
			GetGame().GetCallqueue().CallByName(m_Owner, m_sDoneMethod, this);
	}

	//------------------------------------------------------------------------------------------------
	void ~ELIFE_BankBooking()
	{
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(Send);
	}
}
