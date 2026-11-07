//------------------------------------------------------------------------------------------------
[EntityEditorProps(category: "ELifeRPG/Banking", description: "A bank clerk who serves one customer at a time on behalf of one bank")]
class ELIFE_BankClerkComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
enum ELIFE_EClerkReason
{
	NONE,
	BUSY,
	OUT_OF_RANGE,
	//! The backend or the bank list can't be reached, or the clerk's bank code matches no bank.
	UNAVAILABLE,
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
//! Server-side service desk: the customer lock and the bank the desk belongs to. Requests arrive through ELIFE_BankClerkCharacterComponent.
class ELIFE_BankClerkComponent : ScriptComponent
{
	protected static const int TICK_INTERVAL_MS = 1000;
	//! Same as the ATM: without input for this long, the customer has walked away from the keyboard.
	protected static const int AFK_TIMEOUT_MS = 300000;
	//! Before the idle starts: after spawning (the controller isn't ready to animate yet) and after a customer leaves.
	protected static const int IDLE_START_DELAY_MS = 1000;

	//! Set by the ELIFE_BankClerkSpot that spawned him. Server-only.
	protected string m_sBankCode = "FEBK";

	[Attribute("2.5", UIWidgets.Slider, "How far from the clerk a customer can stand, in metres", "0.5 10 0.1")]
	protected float m_fUseRange;

	[RplProp()]
	protected int m_iCustomerPlayerId;

	//! Public, so it's replicated for every client's dialog title.
	[RplProp()]
	protected string m_sBankName;
	//! The bank's booking fee: a base plus a share of the amount. Public terms, shown before opening an account.
	[RplProp()]
	protected int m_iFeeBaseCents;
	[RplProp()]
	protected float m_fFeeMultiplier;

	protected IEntity m_Customer;
	protected int m_iLastRequestTick;

	protected string m_sBankId;
	protected ref ELIFE_BaseRestCallback m_BankCallback;
	protected bool m_bBankLoadInFlight;
	//! Customers who asked while the bank list was loading; answered when it arrives.
	protected ref array<int> m_aWaitingPlayerIds = {};

	protected ref ELIFE_BankAccountSummaryCallback m_OpenCallback;
	protected int m_iOpeningPlayerId;
	protected bool m_bOpenedThisVisit;

	protected ref ELIFE_BankClerkTransfers m_Transfers;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		m_Transfers = new ELIFE_BankClerkTransfers(this);

		//! The damage manager initialises after this component; disabled in the next frame.
		GetGame().GetCallqueue().CallLater(DisableDamage, 0);

		if (Replication.IsServer() && GetGame().InPlayMode())
			GetGame().GetCallqueue().CallLater(StartIdle, IDLE_START_DELAY_MS);
	}

	//------------------------------------------------------------------------------------------------
	//! Vanilla's standing loiter (the "Wait" emote), started by the server so every client sees it. Only while nobody is served.
	protected void StartIdle()
	{
		SCR_CharacterControllerComponent controller = GetController();
		if (!controller || m_iCustomerPlayerId != 0 || controller.IsLoitering())
			return;

		if (!controller.CanPlayLoiterAnimation(ELoiteringType.LOITERING))
		{
			Print("ELIFE_BankClerkComponent | the idle animation can't start", LogLevel.WARNING);
			return;
		}

		vector noTarget[4];
		controller.StartLoitering(null, ELoiteringType.LOITERING, false, true, false, noTarget);
	}

	//------------------------------------------------------------------------------------------------
	//! While serving he stands upright, attentive. The fast stop finishes at once: the normal exit waits for an animation tag the server can't read.
	protected void StopIdle()
	{
		GetGame().GetCallqueue().Remove(StartIdle);

		SCR_CharacterControllerComponent controller = GetController();
		if (controller && controller.IsLoitering())
			controller.StopLoitering(true);
	}

	//------------------------------------------------------------------------------------------------
	protected SCR_CharacterControllerComponent GetController()
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(GetOwner());
		if (!character)
			return null;

		return SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
	}

	//------------------------------------------------------------------------------------------------
	//! The clerk can't be hurt, so he never goes down and can't be carried or searched either.
	protected void DisableDamage()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(owner.FindComponent(SCR_CharacterDamageManagerComponent));
		if (damage)
			damage.EnableDamageHandling(false);
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
	int GetFeeBaseCents()
	{
		return m_iFeeBaseCents;
	}

	//------------------------------------------------------------------------------------------------
	float GetFeeMultiplier()
	{
		return m_fFeeMultiplier;
	}

	//------------------------------------------------------------------------------------------------
	int GetCustomerPlayerId()
	{
		return m_iCustomerPlayerId;
	}

	//------------------------------------------------------------------------------------------------
	bool IsUsableBy(IEntity user)
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(user);
		if (!character || character == GetOwner())
			return false;

		CharacterControllerComponent controller = character.GetCharacterController();
		if (!controller || controller.IsDead())
			return false;

		return vector.Distance(character.GetOrigin(), GetOwner().GetOrigin()) <= m_fUseRange;
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only.
	void RequestOpen(int playerId, notnull IEntity character, notnull ELIFE_BankClerkCharacterComponent link)
	{
		if (m_iCustomerPlayerId != 0 && (m_iCustomerPlayerId != playerId || character != m_Customer))
		{
			link.SendGrant(GetRplId(), false, ELIFE_EClerkReason.BUSY);
			return;
		}

		if (!IsUsableBy(character))
		{
			Print(string.Format("ELIFE_BankClerkComponent | open refused, player %1 is dead or out of range", playerId), LogLevel.WARNING);
			link.SendGrant(GetRplId(), false, ELIFE_EClerkReason.OUT_OF_RANGE);
			return;
		}

		if (ELIFE_CharacterIdentity.GetCharacterId(playerId) == "")
		{
			Print(string.Format("ELIFE_BankClerkComponent | open refused, player %1 has no character id", playerId), LogLevel.WARNING);
			link.SendGrant(GetRplId(), false, ELIFE_EClerkReason.UNAVAILABLE);
			return;
		}

		if (m_iCustomerPlayerId == 0)
		{
			m_iCustomerPlayerId = playerId;
			m_Customer = character;
			Replication.BumpMe();
			StopIdle();
			GetGame().GetCallqueue().CallLater(Tick, TICK_INTERVAL_MS, true);
			Print(string.Format("ELIFE_BankClerkComponent | %1 serving player %2", m_sBankCode, playerId), LogLevel.NORMAL);
		}

		m_iLastRequestTick = System.GetTickCount();

		if (m_sBankId != "")
		{
			link.SendGrant(GetRplId(), true, ELIFE_EClerkReason.NONE);
			return;
		}

		if (!m_aWaitingPlayerIds.Contains(playerId))
			m_aWaitingPlayerIds.Insert(playerId);

		LoadBank();
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only.
	void RequestClose(int playerId)
	{
		if (playerId != 0 && playerId == m_iCustomerPlayerId)
			Release("closed by the customer");
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. Any request from the customer counts as input.
	void KeepAlive(int playerId, notnull IEntity character)
	{
		if (IsCustomer(playerId, character))
			m_iLastRequestTick = System.GetTickCount();
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. A personal account for the customer at this desk's bank.
	void RequestOpenAccount(int playerId, notnull IEntity character, notnull ELIFE_BankClerkCharacterComponent link)
	{
		if (!IsCustomer(playerId, character) || !IsUsableBy(character))
		{
			link.SendAccountOpened(GetRplId(), false, ELIFE_EClerkReason.OUT_OF_RANGE, "");
			return;
		}

		m_iLastRequestTick = System.GetTickCount();

		//! The same customer's earlier request still answers; anyone else is told instead of left waiting.
		if (m_iOpeningPlayerId == playerId)
			return;

		if (m_iOpeningPlayerId != 0)
		{
			link.SendAccountOpened(GetRplId(), false, ELIFE_EClerkReason.UNAVAILABLE, "");
			return;
		}

		if (m_bOpenedThisVisit)
		{
			link.SendAccountOpened(GetRplId(), false, ELIFE_EClerkReason.ALREADY_OPENED, "");
			return;
		}

		string characterId = ELIFE_CharacterIdentity.GetCharacterId(playerId);
		ELIFE_Api api = ELIFE_Api.GetInstance();
		if (m_sBankId == "" || characterId == "" || !api)
		{
			link.SendAccountOpened(GetRplId(), false, ELIFE_EClerkReason.UNAVAILABLE, "");
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
			m_bOpenedThisVisit = true;

		if (opened)
			Print(string.Format("ELIFE_BankClerkComponent | %1 opened account %2 for player %3", m_sBankCode, account.number, playerId), LogLevel.NORMAL);
		else
			Print(string.Format("ELIFE_BankClerkComponent | %1 failed to open an account for player %2", m_sBankCode, playerId), LogLevel.WARNING);

		ELIFE_BankClerkCharacterComponent link = GetLink(playerId);
		if (!link)
			return;

		if (opened)
			link.SendAccountOpened(GetRplId(), true, ELIFE_EClerkReason.NONE, account.number);
		else
			link.SendAccountOpened(GetRplId(), false, ELIFE_EClerkReason.FAILED, "");
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. The customer's own accounts for the transfer form.
	void RequestTransferAccounts(int playerId, notnull IEntity character, notnull ELIFE_BankClerkCharacterComponent link)
	{
		if (!IsCustomer(playerId, character) || !IsUsableBy(character))
		{
			link.SendClerkAccounts(GetRplId(), ELIFE_EClerkReason.OUT_OF_RANGE, "");
			return;
		}

		m_iLastRequestTick = System.GetTickCount();
		m_Transfers.RequestAccounts(playerId, ELIFE_CharacterIdentity.GetCharacterId(playerId));
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only.
	void RequestTransferQuote(int playerId, notnull IEntity character, notnull ELIFE_BankClerkCharacterComponent link, string fromNumber, string targetNumber, int amount)
	{
		if (!IsCustomer(playerId, character) || !IsUsableBy(character))
		{
			link.SendTransferQuote(GetRplId(), ELIFE_EClerkReason.OUT_OF_RANGE, "");
			return;
		}

		m_iLastRequestTick = System.GetTickCount();
		m_Transfers.RequestQuote(playerId, fromNumber, targetNumber, amount);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only.
	void RequestTransferConfirm(int playerId, notnull IEntity character, notnull ELIFE_BankClerkCharacterComponent link, int quoteId)
	{
		if (!IsCustomer(playerId, character) || !IsUsableBy(character))
		{
			link.SendTransferResult(GetRplId(), ELIFE_EClerkReason.OUT_OF_RANGE, 0, 0, 0);
			return;
		}

		m_iLastRequestTick = System.GetTickCount();
		m_Transfers.RequestConfirm(playerId, quoteId);
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsCustomer(int playerId, IEntity character)
	{
		return playerId != 0 && playerId == m_iCustomerPlayerId && character == m_Customer;
	}

	//------------------------------------------------------------------------------------------------
	//! The bank never changes for a desk, so it's looked up once and kept.
	protected void LoadBank()
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
				m_iFeeBaseCents = ELIFE_PhoneBankingService.ToCents(bank.transactionFeeBase);
				m_fFeeMultiplier = bank.transactionFeeMultiplier;
				Replication.BumpMe();
				AnswerWaiting(true);
				return;
			}
		}

		Print(string.Format("ELIFE_BankClerkComponent | no bank with code %1", m_sBankCode), LogLevel.ERROR);
		AnswerWaiting(false);
	}

	//------------------------------------------------------------------------------------------------
	//! Only the current customer gets a grant; a refusal releases the desk so the next one can try again.
	protected void AnswerWaiting(bool available)
	{
		array<int> waiting = {};
		waiting.Copy(m_aWaitingPlayerIds);
		m_aWaitingPlayerIds.Clear();

		foreach (int playerId : waiting)
		{
			ELIFE_BankClerkCharacterComponent link = GetLink(playerId);
			if (!link)
				continue;

			if (playerId != m_iCustomerPlayerId)
				link.SendGrant(GetRplId(), false, ELIFE_EClerkReason.BUSY);
			else if (available)
				link.SendGrant(GetRplId(), true, ELIFE_EClerkReason.NONE);
			else
				link.SendGrant(GetRplId(), false, ELIFE_EClerkReason.UNAVAILABLE);
		}

		if (!available && m_iCustomerPlayerId != 0)
			Release("the bank is unavailable");
	}

	//------------------------------------------------------------------------------------------------
	protected void Tick()
	{
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager || !m_Customer || !playerManager.IsPlayerConnected(m_iCustomerPlayerId) || playerManager.GetPlayerControlledEntity(m_iCustomerPlayerId) != m_Customer)
		{
			Release("the customer disconnected or the character is gone");
			return;
		}

		if (!IsUsableBy(m_Customer))
		{
			Release("the customer died or walked away");
			return;
		}

		if (System.GetTickCount() - m_iLastRequestTick > AFK_TIMEOUT_MS)
			Release("no input from the customer");
	}

	//------------------------------------------------------------------------------------------------
	protected void Release(string why)
	{
		GetGame().GetCallqueue().Remove(Tick);
		Print(string.Format("ELIFE_BankClerkComponent | %1 released player %2: %3", m_sBankCode, m_iCustomerPlayerId, why), LogLevel.NORMAL);

		m_iCustomerPlayerId = 0;
		m_Customer = null;
		m_bOpenedThisVisit = false;
		m_Transfers.Reset();
		Replication.BumpMe();
		GetGame().GetCallqueue().CallLater(StartIdle, IDLE_START_DELAY_MS);
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_BankClerkCharacterComponent GetLink(int playerId)
	{
		IEntity character = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (!character)
			return null;

		return ELIFE_BankClerkCharacterComponent.Cast(character.FindComponent(ELIFE_BankClerkCharacterComponent));
	}

	//------------------------------------------------------------------------------------------------
	RplId GetRplId()
	{
		RplComponent rpl = RplComponent.Cast(GetOwner().FindComponent(RplComponent));
		if (!rpl)
			return RplId.Invalid();

		return rpl.Id();
	}

	//------------------------------------------------------------------------------------------------
	void ~ELIFE_BankClerkComponent()
	{
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(Tick);
			GetGame().GetCallqueue().Remove(DisableDamage);
			GetGame().GetCallqueue().Remove(StartIdle);
		}
	}
}
