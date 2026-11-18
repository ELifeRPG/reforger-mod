//------------------------------------------------------------------------------------------------
//! What a bank paper is. Its name, summary and paper view follow from it.
enum ELIFE_EBankPaperKind
{
	TRANSFER_RECEIPT,
	ACCOUNT_PAPERS,
	ATM_WITHDRAWAL,
	ATM_DEPOSIT
}

//------------------------------------------------------------------------------------------------
//! The contents of a bank paper. Written only by the server from a real booking or opening, so it can serve as proof.
//! Money fields are the Bridge's display text; empty fields are left off the paper. No balance: papers get shown
//! to others as proof, so they must not reveal what the account holds.
class ELIFE_BankPaper : JsonApiStruct
{
	int kind;
	string bankName;
	//! The account it is about: the new one, the one paid from, or the one the ATM booked on.
	string accountNumber;
	string targetNumber;
	string targetBankName;
	string amount;
	string fee;
	//! The purpose's string key, "" when none was asked.
	string purpose;
	string feeTerms;
	//! The backend's transaction id, "" for account papers.
	string bookingRef;
	//! In-game date and time, "DD.MM.YYYY HH:MM".
	string issuedAt;

	//------------------------------------------------------------------------------------------------
	void ELIFE_BankPaper()
	{
		RegV("kind");
		RegV("bankName");
		RegV("accountNumber");
		RegV("targetNumber");
		RegV("targetBankName");
		RegV("amount");
		RegV("fee");
		RegV("purpose");
		RegV("feeTerms");
		RegV("bookingRef");
		RegV("issuedAt");
	}

	//------------------------------------------------------------------------------------------------
	//! Server-side. Now, in game time.
	void StampNow()
	{
		ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
		TimeAndWeatherManagerEntity time;
		if (world)
			time = world.GetTimeAndWeatherManager();

		if (!time)
			return;

		int hour, minute, second;
		time.GetHoursMinutesSeconds(hour, minute, second);
		issuedAt = string.Format("%1.%2.%3 %4:%5", Pad2(time.GetDay()), Pad2(time.GetMonth()), time.GetYear(), Pad2(hour), Pad2(minute));
	}

	//------------------------------------------------------------------------------------------------
	//! issuedAt split into its "DD.MM.YYYY" and "HH:MM" parts.
	void SplitIssuedAt(out string date, out string time)
	{
		date = issuedAt;
		time = "";
		int space = issuedAt.IndexOf(" ");
		if (space <= 0)
			return;

		date = issuedAt.Substring(0, space);
		time = issuedAt.Substring(space + 1, issuedAt.Length() - space - 1);
	}

	//------------------------------------------------------------------------------------------------
	protected static string Pad2(int value)
	{
		if (value < 10)
			return "0" + value.ToString();

		return value.ToString();
	}

	//------------------------------------------------------------------------------------------------
	//! Papers handed over at the counter: a stamped letter. The rest come out of a machine as a printed slip.
	static bool IsFromClerk(int paperKind)
	{
		return paperKind == ELIFE_EBankPaperKind.TRANSFER_RECEIPT || paperKind == ELIFE_EBankPaperKind.ACCOUNT_PAPERS;
	}

	//------------------------------------------------------------------------------------------------
	//! The paper's title, also its inventory name.
	static string GetTitle(int paperKind)
	{
		switch (paperKind)
		{
			case ELIFE_EBankPaperKind.TRANSFER_RECEIPT: return "#ELIFE-Paper_TransferReceipt";
			case ELIFE_EBankPaperKind.ACCOUNT_PAPERS: return "#ELIFE-Paper_AccountPapers";
			case ELIFE_EBankPaperKind.ATM_WITHDRAWAL: return "#ELIFE-Paper_AtmWithdrawal";
			case ELIFE_EBankPaperKind.ATM_DEPOSIT: return "#ELIFE-Paper_AtmDeposit";
		}

		return "#ELIFE-Item_BankPaper_Name";
	}
}

//------------------------------------------------------------------------------------------------
class ELIFE_BankPaperComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! A receipt or account papers from the bank, as an item. The server writes it once when issuing it.
class ELIFE_BankPaperComponent : ScriptComponent
{
	protected static const ResourceName RECEIPT_PREFAB = "{3E5B1C9A7D24F860}Prefabs/Items/Banking/BankPaper.et";
	protected static const ResourceName LETTER_PREFAB = "{BF35C432AA315D23}Prefabs/Items/Banking/BankLetter.et";

	[RplProp()]
	protected string m_sPaper;

	//! Client-side cache of m_sPaper, unpacked.
	protected ref ELIFE_BankPaper m_Paper;
	protected string m_sUnpacked;

	//------------------------------------------------------------------------------------------------
	//! Server-side. Into the character's inventory, or onto the ground in front of them when there's no room.
	//! Null if it couldn't be made.
	static IEntity Issue(notnull IEntity character, notnull ELIFE_BankPaper paper, out bool onGround)
	{
		onGround = false;
		if (!Replication.IsServer())
			return null;

		ResourceName prefab = RECEIPT_PREFAB;
		if (ELIFE_BankPaper.IsFromClerk(paper.kind))
			prefab = LETTER_PREFAB;

		Resource resource = Resource.Load(prefab);
		if (!resource.IsValid())
		{
			Print("ELIFE_BankPaperComponent | paper prefab missing", LogLevel.ERROR);
			return null;
		}

		//! Spawned where it would land, as the engine's placement leaves thin items hovering.
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		bool placed = ELIFE_CashInventoryItemComponent.FindDropTransform(character, params.Transform);
		if (!placed)
			character.GetWorldTransform(params.Transform);

		IEntity item = GetGame().SpawnEntityPrefab(resource, character.GetWorld(), params);
		if (!item)
			return null;

		ELIFE_BankPaperComponent component = ELIFE_BankPaperComponent.Cast(item.FindComponent(ELIFE_BankPaperComponent));
		if (!component)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(item);
			return null;
		}

		//! Same frame as the spawn, so the first replicated state already carries it.
		component.SetPaper(paper);

		SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(character.FindComponent(SCR_InventoryStorageManagerComponent));
		if (inventory && inventory.TryInsertItem(item))
			return item;

		InventoryItemComponent inventoryItem = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
		if (inventoryItem && !placed)
			inventoryItem.PlaceOnGround(character);

		onGround = true;
		return item;
	}

	//------------------------------------------------------------------------------------------------
	//! Server-side, once.
	protected void SetPaper(notnull ELIFE_BankPaper paper)
	{
		paper.Pack();
		m_sPaper = paper.AsString();
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	//! Null until the replicated contents arrive.
	ELIFE_BankPaper GetPaper()
	{
		if (m_sPaper == "")
			return null;

		if (!m_Paper || m_sUnpacked != m_sPaper)
		{
			m_Paper = new ELIFE_BankPaper();
			m_Paper.ExpandFromRAW(m_sPaper);
			m_sUnpacked = m_sPaper;
		}

		return m_Paper;
	}

	//------------------------------------------------------------------------------------------------
	static ELIFE_BankPaperComponent Find(IEntity item)
	{
		if (!item)
			return null;

		return ELIFE_BankPaperComponent.Cast(item.FindComponent(ELIFE_BankPaperComponent));
	}
}

//------------------------------------------------------------------------------------------------
class ELIFE_BankPaperInventoryItemComponentClass : ELIFE_CashInventoryItemComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! Dropped papers lie flat like dropped cash.
class ELIFE_BankPaperInventoryItemComponent : ELIFE_CashInventoryItemComponent
{
}

//------------------------------------------------------------------------------------------------
//! Per-instance name and summary from the paper's contents. Prefab UIInfo is shared — never SetName on it.
class ELIFE_BankPaperInventoryUIInfo : SCR_InventoryUIInfo
{
	//! U+00A0.
	protected static const string NO_BREAK_SPACE = " ";

	//------------------------------------------------------------------------------------------------
	override string GetInventoryItemName(InventoryItemComponent item)
	{
		ELIFE_BankPaper paper = GetPaper(item);
		if (!paper)
			return super.GetInventoryItemName(item);

		return WidgetManager.Translate(ELIFE_BankPaper.GetTitle(paper.kind));
	}

	//------------------------------------------------------------------------------------------------
	//! The amount and the day, short enough for the narrow tooltip; the paper view (Inspect) shows the rest.
	override string GetInventoryItemDescription(InventoryItemComponent item)
	{
		ELIFE_BankPaper paper = GetPaper(item);
		if (!paper)
			return super.GetInventoryItemDescription(item);

		string text;
		if (paper.kind != ELIFE_EBankPaperKind.ACCOUNT_PAPERS && paper.amount != "")
		{
			// No-break spaces: the tooltip wrapped "1.00 Ŧ" before the symbol.
			string amount = paper.amount;
			amount.Replace(" ", NO_BREAK_SPACE);
			text = amount + "\n";
		}

		string date, time;
		paper.SplitIssuedAt(date, time);
		if (date != "")
			text += date + "\n";

		return text + WidgetManager.Translate("#ELIFE-Item_BankPaper_InspectHint");
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_BankPaper GetPaper(InventoryItemComponent item)
	{
		if (!item)
			return null;

		ELIFE_BankPaperComponent component = ELIFE_BankPaperComponent.Find(item.GetOwner());
		if (!component)
			return null;

		return component.GetPaper();
	}
}
