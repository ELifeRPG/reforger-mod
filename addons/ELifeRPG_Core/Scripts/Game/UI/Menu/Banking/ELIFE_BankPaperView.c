//------------------------------------------------------------------------------------------------
//! A bank paper read on screen: the paper itself over the inventory, shown by Inspect and closed by it again.
//! Each looks like its 3D model: papers from the counter are a stamped letter, ATM papers a printed slip.
class ELIFE_BankPaperView
{
	protected static const ResourceName LETTER_LAYOUT = "{9B47E2D51C08A36F}UI/layouts/Menus/Banking/BankPaper.layout";
	protected static const ResourceName RECEIPT_LAYOUT = "{1EF52AF412A322A1}UI/layouts/Menus/Banking/BankReceipt.layout";
	protected static const ResourceName ROW_LAYOUT = "{97B173CC6F40EB02}UI/layouts/Menus/Banking/BankReceiptRow.layout";
	protected static const ResourceName EMBLEM_TEXTURE = "{EE73F489EC2A3F82}UI/Textures/Banking/ELIFE_PaperEmblem.edds";
	//! The cashier's stamp on papers from the counter.
	protected static const ResourceName STAMP_TEXTURE = "{F4F8F8ED8AB56052}UI/Textures/Banking/ELIFE_PaperStamp.edds";

	protected Widget m_wRoot;

	//------------------------------------------------------------------------------------------------
	//! Null if the layout couldn't be made.
	static ELIFE_BankPaperView Create(notnull Widget parent, notnull ELIFE_BankPaper paper)
	{
		bool letter = ELIFE_BankPaper.IsFromClerk(paper.kind);
		ResourceName layout = RECEIPT_LAYOUT;
		if (letter)
			layout = LETTER_LAYOUT;

		Widget root = GetGame().GetWorkspace().CreateWidgets(layout, parent);
		if (!root)
			return null;

		//! Added by hand, not by the menu manager, so nothing stretches it: without this it is a tiny box in the corner.
		FrameSlot.SetAnchorMin(root, 0, 0);
		FrameSlot.SetAnchorMax(root, 1, 1);
		FrameSlot.SetOffsets(root, 0, 0, 0, 0);
		FrameSlot.SetSizeToContent(root, false);
		//! Above the inventory's panels.
		root.SetZOrder(1000);

		ELIFE_BankPaperView view = new ELIFE_BankPaperView();
		view.m_wRoot = root;
		if (letter)
			view.FillLetter(paper);
		else
			view.FillReceipt(paper);

		return view;
	}

	//------------------------------------------------------------------------------------------------
	void Destroy()
	{
		if (m_wRoot)
			m_wRoot.RemoveFromHierarchy();

		m_wRoot = null;
	}

	//------------------------------------------------------------------------------------------------
	//! An A4 letter like its 3D model: letterhead, date, subject, a short letter around the details, the cashier's stamp.
	protected void FillLetter(notnull ELIFE_BankPaper paper)
	{
		ShowImage("Emblem", EMBLEM_TEXTURE);
		SetText("BankName", paper.bankName);
		string date, time;
		paper.SplitIssuedAt(date, time);
		SetText("Date", date);
		SetText("Title", ELIFE_BankPaper.GetTitle(paper.kind));

		Widget rows = m_wRoot.FindAnyWidget("Rows");
		if (paper.kind == ELIFE_EBankPaperKind.ACCOUNT_PAPERS)
		{
			SetText("Intro", "#ELIFE-Paper_Intro_Account");
			AddRow(rows, "#ELIFE-Clerk_Transfer_Number", ELIFE_PhoneBankingService.GroupIban(paper.accountNumber), false, false);
			AddRow(rows, "#ELIFE-Clerk_Label_BookingFee", paper.feeTerms, false, false);
		}
		else
		{
			SetText("Intro", "#ELIFE-Paper_Intro_Transfer");
			AddTransferRows(rows, paper, false);
			AddRow(rows, "#ELIFE-Clerk_Transfer_Amount", paper.amount, false, false);
			AddRow(rows, "#ELIFE-Clerk_Label_Fee", paper.fee, false, false);
		}

		AddRow(m_wRoot.FindAnyWidget("Details"), "#ELIFE-Paper_Reference", paper.bookingRef, false, false);
		SetText("Hint", "#ELIFE-Paper_Hint");
		ShowImage("Stamp", STAMP_TEXTURE);
	}

	//------------------------------------------------------------------------------------------------
	//! Printed like the slip: capitals, one label and value per row, the amount on its own.
	protected void FillReceipt(notnull ELIFE_BankPaper paper)
	{
		ShowImage("Emblem", EMBLEM_TEXTURE);
		SetText("Title", Caps(WidgetManager.Translate(ELIFE_BankPaper.GetTitle(paper.kind))));
		SetText("BankName", Caps(paper.bankName));

		Widget rows = m_wRoot.FindAnyWidget("Rows");
		if (paper.kind == ELIFE_EBankPaperKind.TRANSFER_RECEIPT)
			AddTransferRows(rows, paper, true);
		else
			AddRow(rows, "#ELIFE-Paper_Account", ELIFE_PhoneBankingService.GroupIban(paper.accountNumber), true, false);

		SetText("AmountLabel", Caps(WidgetManager.Translate("#ELIFE-Clerk_Transfer_Amount")));
		SetText("AmountValue", paper.amount);
		AddRow(m_wRoot.FindAnyWidget("Charges"), "#ELIFE-Clerk_Label_Fee", paper.fee, true, false);

		string date, time;
		paper.SplitIssuedAt(date, time);
		Widget details = m_wRoot.FindAnyWidget("Details");
		AddRow(details, "#ELIFE-Paper_Date", date, true, false);
		AddRow(details, "#ELIFE-Paper_Time", time, true, false);
		// Its own line: the booking id is too long to share one with its label on the narrow slip.
		AddRow(details, "#ELIFE-Paper_Reference", paper.bookingRef, true, true);

		SetText("Hint", "#ELIFE-Paper_Hint");
	}

	//------------------------------------------------------------------------------------------------
	protected static void AddTransferRows(Widget list, notnull ELIFE_BankPaper paper, bool caps)
	{
		AddRow(list, "#ELIFE-Paper_From", ELIFE_PhoneBankingService.GroupIban(paper.accountNumber), caps, false);
		AddRow(list, "#ELIFE-Clerk_Transfer_To", ELIFE_PhoneBankingService.GroupIban(paper.targetNumber), caps, false);
		// Under the number, unlabelled, so a long bank name doesn't push the number off the paper.
		string bank = paper.targetBankName;
		if (caps)
			bank = Caps(bank);

		AddRow(list, "", bank, caps, false);
		if (paper.purpose == "")
			return;

		string purpose = WidgetManager.Translate(paper.purpose);
		if (caps)
			purpose = Caps(purpose);

		AddRow(list, "#ELIFE-Clerk_Label_Purpose", purpose, caps, false);
	}

	//------------------------------------------------------------------------------------------------
	//! A label and value row; skipped when there's no value. An empty labelKey leaves the label blank.
	//! ownLine puts the value on the line under its label.
	protected static void AddRow(Widget list, string labelKey, string value, bool caps, bool ownLine)
	{
		if (!list || value == "")
			return;

		string label;
		if (labelKey != "")
			label = WidgetManager.Translate(labelKey);

		if (caps)
			label = Caps(label);

		if (!ownLine)
		{
			CreateRow(list, label, value);
			return;
		}

		CreateRow(list, label, "");
		CreateRow(list, "", value);
	}

	//------------------------------------------------------------------------------------------------
	protected static void CreateRow(notnull Widget list, string label, string value)
	{
		Widget row = GetGame().GetWorkspace().CreateWidgets(ROW_LAYOUT, list);
		if (!row)
			return;

		LayoutSlot.SetHorizontalAlign(row, LayoutHorizontalAlign.Stretch);
		TextWidget labelText = TextWidget.Cast(row.FindAnyWidget("Label"));
		if (labelText)
			labelText.SetText(label);

		TextWidget valueText = TextWidget.Cast(row.FindAnyWidget("Value"));
		if (valueText)
			valueText.SetText(value);
	}

	//------------------------------------------------------------------------------------------------
	//! Receipt capitals. The umlauts are fixed by hand in case ToUpper only maps ASCII.
	protected static string Caps(string text)
	{
		string caps = text;
		caps.ToUpper();
		caps.Replace("ä", "Ä");
		caps.Replace("ö", "Ö");
		caps.Replace("ü", "Ü");
		return caps;
	}

	//------------------------------------------------------------------------------------------------
	//! Hidden if the texture can't be loaded, so it never shows as a white square.
	protected void ShowImage(string name, ResourceName texture)
	{
		ImageWidget image = ImageWidget.Cast(m_wRoot.FindAnyWidget(name));
		if (!image)
			return;

		bool loaded = image.LoadImageTexture(0, texture);
		if (loaded)
			image.SetImage(0);

		image.SetVisible(loaded);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetText(string name, string text)
	{
		TextWidget widget = TextWidget.Cast(m_wRoot.FindAnyWidget(name));
		if (!widget)
			return;

		widget.SetText(text);
		widget.SetVisible(text != "");
	}
}

//------------------------------------------------------------------------------------------------
//! Inspecting a bank paper in the inventory reads it; the footer's Use button, labelled "Tear up", gets rid of it.
modded class SCR_InventoryMenuUI
{
	//! Played on this machine at the player, so deleting the paper can't cut it off.
	protected static const ResourceName ELIFE_PAPER_SOUNDS = "{EA1FE2A4670186B4}Sounds/Items/Banking/ELIFE_BankPaper_Sfx.acp";
	protected static const string ELIFE_TEAR_SOUND = "SOUND_ELIFE_PAPER_TEAR";
	//! Most of the tear (ELIFE_Paper_Tear.wav, 0.52 s) before the paper is gone.
	protected static const int ELIFE_TEAR_DELAY_MS = 350;
	protected static const float ELIFE_TEAR_FADE_OPACITY = 0.15;
	protected static const float ELIFE_TEAR_FADE_SPEED = 3;

	protected ref ELIFE_BankPaperView m_ELIFE_PaperView;
	//! The paper being torn up, so a second hold during the tear does nothing.
	protected IEntity m_ELIFE_TearingPaper;
	protected Widget m_wELIFE_TearingSlot;

	//------------------------------------------------------------------------------------------------
	//! True if the focused item is a bank paper this player can tear up.
	protected bool ELIFE_CanTearUpFocused()
	{
		if (!m_pFocusedSlotUI || !m_Player)
			return false;

		InventoryItemComponent item = m_pFocusedSlotUI.GetInventoryItemComponent();
		if (!item || item.GetOwner() == m_ELIFE_TearingPaper)
			return false;

		ELIFE_BankPaperCharacterComponent requests = ELIFE_BankPaperCharacterComponent.Cast(m_Player.FindComponent(ELIFE_BankPaperCharacterComponent));
		return requests && requests.CanTearUp(item.GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	protected bool ELIFE_IsPaperFocused()
	{
		if (!m_pFocusedSlotUI)
			return false;

		InventoryItemComponent item = m_pFocusedSlotUI.GetInventoryItemComponent();
		return item && ELIFE_BankPaperComponent.Find(item.GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	protected void ELIFE_TearUpFocused()
	{
		if (!m_pFocusedSlotUI || !m_Player)
			return;

		InventoryItemComponent item = m_pFocusedSlotUI.GetInventoryItemComponent();
		if (!item || !ELIFE_BankPaperComponent.Find(item.GetOwner()))
			return;

		ELIFE_BankPaperCharacterComponent requests = ELIFE_BankPaperCharacterComponent.Cast(m_Player.FindComponent(ELIFE_BankPaperCharacterComponent));
		if (!requests || !requests.CanTearUp(item.GetOwner()))
			return;

		IEntity paper = item.GetOwner();
		if (paper == m_ELIFE_TearingPaper)
			return;

		//! The server removes it once the sound has mostly played, so it doesn't just vanish.
		m_ELIFE_TearingPaper = paper;
		ELIFE_ClosePaper();
		HideItemInfo();
		NavigationBarUpdate();

		m_wELIFE_TearingSlot = m_pFocusedSlotUI.GetButtonWidget();
		if (m_wELIFE_TearingSlot)
			AnimateWidget.Opacity(m_wELIFE_TearingSlot, ELIFE_TEAR_FADE_OPACITY, ELIFE_TEAR_FADE_SPEED);

		vector transform[4];
		m_Player.GetWorldTransform(transform);
		AudioSystem.PlayEvent(ELIFE_PAPER_SOUNDS, ELIFE_TEAR_SOUND, transform);

		//! Through the character's component, so the request still goes if the inventory closes meanwhile.
		GetGame().GetCallqueue().CallLater(requests.RequestTearUp, ELIFE_TEAR_DELAY_MS, false, paper);
	}

	//------------------------------------------------------------------------------------------------
	//! The server kept the paper: undo the fade so it can be used again.
	void ELIFE_OnTearUpRefused(IEntity paper)
	{
		if (!paper || paper != m_ELIFE_TearingPaper)
			return;

		m_ELIFE_TearingPaper = null;
		if (m_wELIFE_TearingSlot)
			AnimateWidget.Opacity(m_wELIFE_TearingSlot, 1, ELIFE_TEAR_FADE_SPEED);

		m_wELIFE_TearingSlot = null;
		NavigationBarUpdate();
	}

	//------------------------------------------------------------------------------------------------
	//! Vanilla's Use button (hold) becomes "Tear up" on a paper, so the footer shows the right key on every device.
	//! Runs after vanilla's own pass, keyboard and gamepad alike. Vanilla never relabels Use, so other items get theirs back here.
	override void NavigationBarUpdate()
	{
		super.NavigationBarUpdate();

		if (!m_pNavigationBar)
			return;

		if (ELIFE_CanTearUpFocused())
		{
			m_pNavigationBar.SetButtonEnabled(BUTTON_USE, true);
			m_pNavigationBar.SetButtonActionName(BUTTON_USE, "#ELIFE-Paper_TearUp");
			return;
		}

		m_pNavigationBar.SetButtonActionName(BUTTON_USE, "#AR-Inventory_Use");
	}

	//------------------------------------------------------------------------------------------------
	//! Vanilla refreshes the storages but leaves the tooltip and footer of a focused item that is gone until the
	//! mouse moves. A torn-up paper is the usual case.
	override protected void OnItemRemovedListener(IEntity item, notnull BaseInventoryStorageComponent storage)
	{
		super.OnItemRemovedListener(item, storage);

		if (!item || !m_pFocusedSlotUI)
			return;

		InventoryItemComponent focused = m_pFocusedSlotUI.GetInventoryItemComponent();
		if (focused && focused.GetOwner() != item)
			return;

		HideItemInfo();
		m_pFocusedSlotUI = null;
		NavigationBarUpdate();
	}

	//------------------------------------------------------------------------------------------------
	//! A paper is torn up rather than used; everything else is vanilla's.
	override protected void Action_UseItem()
	{
		//! Never vanilla's Use on a paper, also not while it is being torn.
		if (ELIFE_IsPaperFocused())
		{
			ELIFE_TearUpFocused();
			return;
		}

		super.Action_UseItem();
	}

	//------------------------------------------------------------------------------------------------
	override protected bool CanInspectItemInInventory(notnull IEntity item, notnull InventoryItemComponent iic)
	{
		return ELIFE_BankPaperComponent.Find(item) || super.CanInspectItemInInventory(item, iic);
	}

	//------------------------------------------------------------------------------------------------
	override void Action_Inspect()
	{
		if (m_ELIFE_PaperView)
		{
			ELIFE_ClosePaper();
			SCR_UISoundEntity.SoundEvent(SCR_SoundEvent.SOUND_INV_WEAPON_INSPECT);
			return;
		}

		ELIFE_BankPaper paper = ELIFE_GetFocusedPaper();
		if (!paper)
		{
			super.Action_Inspect();
			return;
		}

		m_ELIFE_PaperView = ELIFE_BankPaperView.Create(GetRootWidget(), paper);
		SCR_UISoundEntity.SoundEvent(SCR_SoundEvent.SOUND_INV_WEAPON_INSPECT);
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuClose()
	{
		ELIFE_ClosePaper();

		super.OnMenuClose();
	}

	//------------------------------------------------------------------------------------------------
	protected void ELIFE_ClosePaper()
	{
		if (m_ELIFE_PaperView)
			m_ELIFE_PaperView.Destroy();

		m_ELIFE_PaperView = null;
	}

	//------------------------------------------------------------------------------------------------
	protected ELIFE_BankPaper ELIFE_GetFocusedPaper()
	{
		if (!m_pFocusedSlotUI)
			return null;

		InventoryItemComponent item = m_pFocusedSlotUI.GetInventoryItemComponent();
		if (!item)
			return null;

		ELIFE_BankPaperComponent component = ELIFE_BankPaperComponent.Find(item.GetOwner());
		if (!component)
			return null;

		return component.GetPaper();
	}
}
