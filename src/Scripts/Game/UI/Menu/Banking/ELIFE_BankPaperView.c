//------------------------------------------------------------------------------------------------
//! A bank paper read on screen: the paper itself over the inventory, shown by Inspect and closed by it again.
class ELIFE_BankPaperView
{
	protected static const ResourceName LAYOUT = "{9B47E2D51C08A36F}UI/layouts/Menus/Banking/BankPaper.layout";
	//! The cashier's stamp on papers from the counter.
	protected static const ResourceName STAMP_TEXTURE = "{F4F8F8ED8AB56052}UI/Textures/Banking/ELIFE_PaperStamp.edds";

	protected Widget m_wRoot;

	//------------------------------------------------------------------------------------------------
	//! Null if the layout couldn't be made.
	static ELIFE_BankPaperView Create(notnull Widget parent, notnull ELIFE_BankPaper paper)
	{
		Widget root = GetGame().GetWorkspace().CreateWidgets(LAYOUT, parent);
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
		view.Fill(paper);
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
	protected void Fill(notnull ELIFE_BankPaper paper)
	{
		SetText("Title", ELIFE_BankPaper.GetTitle(paper.kind));
		SetText("BankName", paper.bankName);
		SetText("Body", GetBody(paper));

		string footer;
		if (paper.bookingRef != "")
			footer = ELIFE_DialogueDetails.FormatLine("#ELIFE-Paper_Reference", paper.bookingRef);

		if (paper.issuedAt != "")
		{
			if (footer != "")
				footer += "\n";
			footer += paper.issuedAt;
		}

		SetText("Footer", footer);
		SetText("Hint", "#ELIFE-Paper_Hint");
		ShowStamp(IsFromClerk(paper.kind));
	}

	//------------------------------------------------------------------------------------------------
	//! Only papers handed over at the counter carry the cashier's stamp; a machine has nobody to stamp them.
	protected static bool IsFromClerk(int kind)
	{
		return kind == ELIFE_EBankPaperKind.TRANSFER_RECEIPT || kind == ELIFE_EBankPaperKind.ACCOUNT_PAPERS;
	}

	//------------------------------------------------------------------------------------------------
	//! Hidden if the texture can't be loaded, so it never shows as a white square.
	protected void ShowStamp(bool stamped)
	{
		ImageWidget stamp = ImageWidget.Cast(m_wRoot.FindAnyWidget("Stamp"));
		if (!stamp)
			return;

		if (!stamped)
		{
			stamp.SetVisible(false);
			return;
		}

		bool loaded = stamp.LoadImageTexture(0, STAMP_TEXTURE);
		if (loaded)
			stamp.SetImage(0);

		stamp.SetVisible(loaded);
	}

	//------------------------------------------------------------------------------------------------
	//! One "Label: value" per line, only the fields the paper has.
	protected static string GetBody(notnull ELIFE_BankPaper paper)
	{
		array<string> lines = {};
		switch (paper.kind)
		{
			case ELIFE_EBankPaperKind.TRANSFER_RECEIPT:
			{
				AddLine(lines, "#ELIFE-Paper_From", ELIFE_PhoneBankingService.GroupIban(paper.accountNumber));
				string target = ELIFE_PhoneBankingService.GroupIban(paper.targetNumber);
				if (paper.targetBankName != "")
					target += " (" + paper.targetBankName + ")";

				AddLine(lines, "#ELIFE-Clerk_Transfer_To", target);
				AddLine(lines, "#ELIFE-Clerk_Transfer_Amount", paper.amount);
				AddLine(lines, "#ELIFE-Clerk_Label_Fee", paper.fee);
				if (paper.purpose != "")
					AddLine(lines, "#ELIFE-Clerk_Label_Purpose", WidgetManager.Translate(paper.purpose));

				break;
			}
			case ELIFE_EBankPaperKind.ACCOUNT_PAPERS:
			{
				AddLine(lines, "#ELIFE-Clerk_Transfer_Number", ELIFE_PhoneBankingService.GroupIban(paper.accountNumber));
				AddLine(lines, "#ELIFE-Clerk_Label_BookingFee", paper.feeTerms);
				break;
			}
			case ELIFE_EBankPaperKind.ATM_WITHDRAWAL:
			case ELIFE_EBankPaperKind.ATM_DEPOSIT:
			{
				AddLine(lines, "#ELIFE-Paper_Account", ELIFE_PhoneBankingService.GroupIban(paper.accountNumber));
				AddLine(lines, "#ELIFE-Clerk_Transfer_Amount", paper.amount);
				AddLine(lines, "#ELIFE-Clerk_Label_Fee", paper.fee);
				break;
			}
		}

		string text;
		foreach (int i, string line : lines)
		{
			if (i > 0)
				text += "\n";
			text += line;
		}

		return text;
	}

	//------------------------------------------------------------------------------------------------
	protected static void AddLine(notnull array<string> lines, string labelKey, string value)
	{
		if (value != "")
			lines.Insert(ELIFE_DialogueDetails.FormatLine(labelKey, value));
	}

	//------------------------------------------------------------------------------------------------
	protected void SetText(string name, string text)
	{
		TextWidget widget = TextWidget.Cast(m_wRoot.FindAnyWidget(name));
		if (widget)
		{
			widget.SetText(text);
			widget.SetVisible(text != "");
			return;
		}

		RichTextWidget rich = RichTextWidget.Cast(m_wRoot.FindAnyWidget(name));
		if (rich)
		{
			rich.SetText(text);
			rich.SetVisible(text != "");
		}
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
