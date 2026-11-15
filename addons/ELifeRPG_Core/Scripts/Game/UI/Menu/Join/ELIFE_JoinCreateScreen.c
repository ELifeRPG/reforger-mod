//------------------------------------------------------------------------------------------------
//! Create screen (spec 5.5, 4): first and last name, skin tone swatches, head cards, a full-body preview of the chosen
//! character, and Create. Back shows only when the server allows it (canGoBack). The server validates the names and the
//! appearance; the client only caps the names at NAME_MAX. A tone without a head is not offered. The heads of a tone
//! are paged, HEADS_PER_PAGE (3, one row of HEAD_COLUMNS) per page, with a pager when there are more than one page.
class ELIFE_JoinCreateScreen
{
	protected const int NAME_MAX = 20;
	protected const string ERROR_KEY_PREFIX = "ELIFE-Join_";
	protected const string ERROR_APPEARANCE_INVALID = "#ELIFE-Join_AppearanceInvalid";

	protected const ResourceName LAYOUT_SWATCH = "{95C5E5F66D654CE5}UI/layouts/Menus/Join/JoinSkinSwatch.layout";
	protected const ResourceName LAYOUT_HEAD = "{060CBDC4B41F9245}UI/layouts/Menus/Join/JoinHeadCard.layout";
	protected const ResourceName LAYOUT_ROW = "{8C33390AEF438F42}UI/layouts/Menus/Join/JoinCharacterRow.layout";

	//! Head cards per row of the head list.
	protected const int HEAD_COLUMNS = 3;
	//! Head cards per page of the head list: one row.
	protected const int HEADS_PER_PAGE = 3;
	//! Head card width in pixels; the head preview box is centred on it.
	protected const float HEAD_CARD_WIDTH = 160;
	//! Head preview box: height in pixels, width = height * aspect (the character prefab's preview aspect), top offset
	//! relative to the card top. The card clips the box, so the head shows.
	protected const float HEAD_PREVIEW_HEIGHT = 1500;
	protected const float HEAD_PREVIEW_ASPECT = 0.48;
	protected const float HEAD_PREVIEW_TOP = -176;
	//! Horizontal nudge of the head crop; positive moves the head right.
	protected const float HEAD_PREVIEW_LEFT = 8;

	protected Widget m_wRoot;
	protected Widget m_wFirstName;
	protected Widget m_wLastName;
	protected Widget m_wSkinRow;
	protected TextWidget m_wSkinName;
	protected Widget m_wHeadRow;
	protected Widget m_wHeadPager;
	protected Widget m_wHeadPrev;
	protected Widget m_wHeadNext;
	protected TextWidget m_wHeadPageLabel;
	protected Widget m_wBack;
	protected Widget m_wCreate;
	protected Widget m_wPreview;
	protected Widget m_wErrorRow;
	protected TextWidget m_wError;
	protected SCR_EditBoxComponent m_FirstName;
	protected SCR_EditBoxComponent m_LastName;
	protected SCR_ButtonTextComponent m_BackButton;
	protected SCR_ButtonTextComponent m_CreateButton;
	protected SCR_ButtonTextComponent m_HeadPrevButton;
	protected SCR_ButtonTextComponent m_HeadNextButton;

	protected ref array<ref ELIFE_JoinAppearanceClick> m_aSkinClicks = {};
	protected ref array<ref ELIFE_JoinAppearanceClick> m_aHeadClicks = {};
	//! Rows of the head list, HEAD_COLUMNS cards each. Removed with the cards in ClearHeads.
	protected ref array<Widget> m_aHeadRows = {};
	//! Config indexes of the tones that have at least one head, in config order. The swatch row shows only these.
	protected ref array<int> m_aTones = {};
	protected ELIFE_AppearanceConfig m_Config;
	//! Config index of the chosen tone, -1 when no tone is valid.
	protected int m_iSkin = -1;
	//! Index into the chosen tone's head list.
	protected int m_iHead;
	//! Page of the head cards of the chosen tone (0-based). Heads are selected by index, not by page.
	protected int m_iHeadPage;
	//! Full-body preview of the chosen pair. Owned by this screen: deleted on change and when the screen hides.
	protected IEntity m_PreviewEntity;

	protected ELIFE_PlayerController m_Controller;
	protected bool m_bBusy;

	//------------------------------------------------------------------------------------------------
	//! Binds the widgets of an already created JoinCreate layout.
	void Bind(Widget root)
	{
		m_wRoot = root;
		m_wFirstName = m_wRoot.FindAnyWidget("FirstName");
		m_wLastName = m_wRoot.FindAnyWidget("LastName");
		m_wSkinRow = m_wRoot.FindAnyWidget("SkinToneRow");
		m_wSkinName = TextWidget.Cast(m_wRoot.FindAnyWidget("SkinToneName"));
		m_wHeadRow = m_wRoot.FindAnyWidget("HeadRow");
		m_wHeadPager = m_wRoot.FindAnyWidget("HeadPager");
		m_wHeadPrev = m_wRoot.FindAnyWidget("HeadPrev");
		m_wHeadNext = m_wRoot.FindAnyWidget("HeadNext");
		m_wHeadPageLabel = TextWidget.Cast(m_wRoot.FindAnyWidget("HeadPageLabel"));
		m_wBack = m_wRoot.FindAnyWidget("ButtonBack");
		m_wCreate = m_wRoot.FindAnyWidget("ButtonCreate");
		m_wPreview = m_wRoot.FindAnyWidget("Preview");
		m_wErrorRow = m_wRoot.FindAnyWidget("CreateErrorRow");
		m_wError = TextWidget.Cast(m_wRoot.FindAnyWidget("CreateError"));

		m_FirstName = SCR_EditBoxComponent.GetEditBoxComponent("FirstName", m_wRoot);
		if (m_FirstName)
			m_FirstName.m_OnChanged.Insert(OnNameChanged);

		m_LastName = SCR_EditBoxComponent.GetEditBoxComponent("LastName", m_wRoot);
		if (m_LastName)
			m_LastName.m_OnChanged.Insert(OnNameChanged);

		TextWidget title = TextWidget.Cast(m_wRoot.FindAnyWidget("CreateTitle"));
		if (title)
			title.SetText("#ELIFE-Join_CreateTitle");

		m_BackButton = SCR_ButtonTextComponent.GetButtonText("ButtonBack", m_wRoot);
		if (m_BackButton)
		{
			m_BackButton.SetText("#ELIFE-Join_Back");
			m_BackButton.m_OnClicked.Insert(OnBackClicked);
		}

		m_CreateButton = SCR_ButtonTextComponent.GetButtonText("ButtonCreate", m_wRoot);
		if (m_CreateButton)
		{
			m_CreateButton.SetText("#ELIFE-Join_Create");
			m_CreateButton.m_OnClicked.Insert(OnCreateClicked);
		}

		m_HeadPrevButton = SCR_ButtonTextComponent.GetButtonText("HeadPrev", m_wRoot);
		if (m_HeadPrevButton)
		{
			m_HeadPrevButton.SetText("<");
			m_HeadPrevButton.m_OnClicked.Insert(OnHeadPrevClicked);
		}

		m_HeadNextButton = SCR_ButtonTextComponent.GetButtonText("HeadNext", m_wRoot);
		if (m_HeadNextButton)
		{
			m_HeadNextButton.SetText(">");
			m_HeadNextButton.m_OnClicked.Insert(OnHeadNextClicked);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Shows or hides the screen. Hiding deletes the full-body preview character and the head cards with their characters.
	void SetVisible(bool visible)
	{
		if (!visible)
		{
			DeletePreview();
			ClearHeads();
		}

		if (m_wRoot)
			m_wRoot.SetVisible(visible);
	}

	//------------------------------------------------------------------------------------------------
	//! Shows the payload: names are pre-filled, Back follows canGoBack, and the appearance restarts at the first valid
	//! tone with head 0. The menu clears busy state before calling this.
	void Show(ELIFE_JoinCreatePayload payload, ELIFE_PlayerController controller)
	{
		if (!m_wRoot)
			return;

		m_Controller = controller;

		m_Config = null;
		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (manager)
			m_Config = manager.GetAppearanceConfig();

		if (m_FirstName)
			m_FirstName.SetValue(payload.firstName);

		if (m_LastName)
			m_LastName.SetValue(payload.lastName);

		if (m_wBack)
			m_wBack.SetVisible(payload.canGoBack == 1);

		SetError(GetErrorText(payload.error));

		RebuildSkinTones();
	}

	//------------------------------------------------------------------------------------------------
	//! Disables the inputs, buttons and appearance cards while a request is in flight. Set by ELIFE_JoinMenu.SetBusy.
	void SetBusy(bool busy)
	{
		m_bBusy = busy;

		if (m_wFirstName)
			m_wFirstName.SetEnabled(!busy);

		if (m_wLastName)
			m_wLastName.SetEnabled(!busy);

		foreach (ELIFE_JoinAppearanceClick click : m_aSkinClicks)
		{
			if (click.m_Button)
				click.m_Button.SetEnabled(!busy);
		}

		foreach (ELIFE_JoinAppearanceClick click : m_aHeadClicks)
		{
			if (click.m_Button)
				click.m_Button.SetEnabled(!busy);
		}

		RefreshHeadPager();

		if (m_BackButton)
			m_BackButton.SetEnabled(!busy);

		if (m_CreateButton)
			m_CreateButton.SetEnabled(!busy);
	}

	//------------------------------------------------------------------------------------------------
	//! Shows the inline error in vanilla warning style; an empty text hides the row.
	protected void SetError(string text)
	{
		if (m_wError)
			m_wError.SetText(text);

		if (m_wErrorRow)
			m_wErrorRow.SetVisible(text != "");
	}

	//------------------------------------------------------------------------------------------------
	//! Caps a name at NAME_MAX while it is typed. The server still validates the name.
	protected void OnNameChanged(SCR_EditBoxComponent comp, string text)
	{
		if (text.Length() > NAME_MAX)
			comp.SetValue(text.Substring(0, NAME_MAX));
	}

	//------------------------------------------------------------------------------------------------
	//! Lists the tones that have a head, rebuilds the swatch row, and selects the first of them with head 0.
	protected void RebuildSkinTones()
	{
		ClearClicks(m_aSkinClicks);
		m_aTones.Clear();

		if (m_Config && m_Config.m_aSkinTones)
		{
			for (int i = 0; i < m_Config.m_aSkinTones.Count(); i++)
			{
				if (ELIFE_AppearanceConfig.IsValid(m_Config, i, 0))
					m_aTones.Insert(i);
			}
		}

		for (int t = 0; t < m_aTones.Count(); t++)
		{
			AddSkinSwatch(m_aTones[t]);
		}

		m_iSkin = -1;
		if (m_aTones.Count() > 0)
			m_iSkin = m_aTones[0];

		RebuildHeads();
	}

	//------------------------------------------------------------------------------------------------
	//! Selects head 0 of the chosen tone and shows the first page of its heads. Used when the tone changes.
	protected void RebuildHeads()
	{
		m_iHead = 0;
		ShowHeadPage(0);
	}

	//------------------------------------------------------------------------------------------------
	//! Number of heads of the chosen tone, 0 without a valid tone.
	protected int GetHeadCount()
	{
		if (!m_Config || m_iSkin < 0)
			return 0;

		return m_Config.m_aSkinTones[m_iSkin].m_aHeads.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Shows one page of head cards of the chosen tone: HEADS_PER_PAGE heads, HEAD_COLUMNS per row. The cards of the
	//! previous page are removed and their characters deleted first, so only the visible page has characters. The
	//! selection is by head index, so the chosen head keeps its outline when its page is shown.
	protected void ShowHeadPage(int page)
	{
		int count = GetHeadCount();
		int pages = ELIFE_JoinPaging.PageCount(count, HEADS_PER_PAGE);
		if (page >= pages)
			page = pages - 1;
		if (page < 0)
			page = 0;

		m_iHeadPage = page;
		ClearHeads();

		if (m_Config && m_iSkin >= 0)
		{
			ELIFE_SkinTone tone = m_Config.m_aSkinTones[m_iSkin];
			int start = ELIFE_JoinPaging.PageStart(page, HEADS_PER_PAGE);
			int end = start + HEADS_PER_PAGE;
			if (end > count)
				end = count;

			for (int h = start; h < end; h++)
			{
				AddHeadCard(h, tone);
			}
		}

		RefreshSelection();
		RefreshHeadPager();
		RefreshAppearance();
	}

	//------------------------------------------------------------------------------------------------
	//! The pager shows only with more than HEADS_PER_PAGE heads; Prev and Next are off at the ends and while busy.
	protected void RefreshHeadPager()
	{
		int count = GetHeadCount();
		int pages = ELIFE_JoinPaging.PageCount(count, HEADS_PER_PAGE);
		if (m_wHeadPager)
			m_wHeadPager.SetVisible(count > HEADS_PER_PAGE);

		if (m_wHeadPageLabel)
			m_wHeadPageLabel.SetText(string.Format("%1 / %2", m_iHeadPage + 1, pages));

		if (m_HeadPrevButton)
			m_HeadPrevButton.SetEnabled(!m_bBusy && m_iHeadPage > 0);

		if (m_HeadNextButton)
			m_HeadNextButton.SetEnabled(!m_bBusy && m_iHeadPage < pages - 1);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnHeadPrevClicked()
	{
		if (m_bBusy || m_iHeadPage <= 0)
			return;

		ShowHeadPage(m_iHeadPage - 1);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnHeadNextClicked()
	{
		if (m_bBusy || m_iHeadPage >= ELIFE_JoinPaging.PageCount(GetHeadCount(), HEADS_PER_PAGE) - 1)
			return;

		ShowHeadPage(m_iHeadPage + 1);
	}

	//------------------------------------------------------------------------------------------------
	//! One swatch button in the skin row, filled with the tone's swatch colour.
	protected void AddSkinSwatch(int toneIndex)
	{
		if (!m_wSkinRow)
			return;

		Widget root = GetGame().GetWorkspace().CreateWidgets(LAYOUT_SWATCH, m_wSkinRow);
		if (!root)
			return;

		ELIFE_SkinTone tone = m_Config.m_aSkinTones[toneIndex];
		ImageWidget fill = ImageWidget.Cast(root.FindAnyWidget("SwatchFill"));
		if (fill && tone.m_Swatch)
			fill.SetColor(tone.m_Swatch);

		m_aSkinClicks.Insert(WrapClick(root, "SkinSwatch", true, toneIndex));
	}

	//------------------------------------------------------------------------------------------------
	//! One head card in the head row: a local character with the tone's body and this head, cropped by the card to the
	//! head. A card whose character can't be built stays empty.
	protected void AddHeadCard(int headIndex, ELIFE_SkinTone tone)
	{
		Widget row = GetHeadRow(headIndex);
		if (!row)
			return;

		Widget root = GetGame().GetWorkspace().CreateWidgets(LAYOUT_HEAD, row);
		if (!root)
			return;

		Widget preview = root.FindAnyWidget("HeadPreview");
		ELIFE_JoinAppearanceClick click = WrapClick(root, "HeadCard", false, headIndex);

		PlaceHeadPreview(preview);
		IEntity entity = ELIFE_JoinPreview.SpawnLocalCharacter(tone.m_sBody, tone.m_aHeads[headIndex]);
		if (ELIFE_JoinPreview.ShowLocalCharacter(preview, entity))
			click.m_Entity = entity;
		else
			ELIFE_JoinPreview.DeleteLocalCharacter(entity);

		m_aHeadClicks.Insert(click);
	}

	//------------------------------------------------------------------------------------------------
	//! The row a head card goes into: a new row before every HEAD_COLUMNS-th card, the last row otherwise. Null when a row
	//! can't be created.
	protected Widget GetHeadRow(int headIndex)
	{
		if (!m_wHeadRow)
			return null;

		if (headIndex % HEAD_COLUMNS == 0 || m_aHeadRows.Count() == 0)
		{
			Widget created = GetGame().GetWorkspace().CreateWidgets(LAYOUT_ROW, m_wHeadRow);
			if (!created)
				return null;

			m_aHeadRows.Insert(created);
		}

		return m_aHeadRows[m_aHeadRows.Count() - 1];
	}

	//------------------------------------------------------------------------------------------------
	//! Places the head preview box in its card (frame coordinates, anchored top-left): HEAD_PREVIEW_HEIGHT tall, width from
	//! the aspect, centred horizontally on the card, top at HEAD_PREVIEW_TOP. The card's clipping crops the rest.
	protected void PlaceHeadPreview(Widget preview)
	{
		if (!preview)
			return;

		float height = HEAD_PREVIEW_HEIGHT;
		float width = height * HEAD_PREVIEW_ASPECT;
		float left = (HEAD_CARD_WIDTH - width) * 0.5 + HEAD_PREVIEW_LEFT;

		FrameSlot.SetAnchor(preview, 0, 0);
		FrameSlot.SetAlignment(preview, 0, 0);
		FrameSlot.SetPos(preview, left, HEAD_PREVIEW_TOP);
		FrameSlot.SetSize(preview, width, height);
	}

	//------------------------------------------------------------------------------------------------
	//! Removes all head cards with their characters and the rows that hold them.
	protected void ClearHeads()
	{
		ClearClicks(m_aHeadClicks);

		foreach (Widget row : m_aHeadRows)
		{
			if (row)
				row.RemoveFromHierarchy();
		}

		m_aHeadRows.Clear();
	}

	//------------------------------------------------------------------------------------------------
	//! Builds the click target of one card. The button gets the click; a disabled button ignores it while busy.
	protected ELIFE_JoinAppearanceClick WrapClick(Widget root, string buttonName, bool skinTone, int index)
	{
		ELIFE_JoinAppearanceClick click = new ELIFE_JoinAppearanceClick();
		click.m_Screen = this;
		click.m_bSkinTone = skinTone;
		click.m_iIndex = index;
		click.m_Widget = root;
		click.m_Button = root.FindAnyWidget(buttonName);
	click.m_wSelected = root.FindAnyWidget("Selected");
	click.m_wCheck = root.FindAnyWidget("Check");

		SCR_ButtonTextComponent button = null;
		if (click.m_Button)
			button = SCR_ButtonTextComponent.FindButtonTextComponent(click.m_Button);

		if (button)
			button.m_OnClicked.Insert(click.OnClicked);

		if (click.m_Button)
			click.m_Button.SetEnabled(!m_bBusy);

		return click;
	}

	//------------------------------------------------------------------------------------------------
	//! Removes the cards of one row from the hierarchy, deletes their characters and forgets them.
	protected void ClearClicks(array<ref ELIFE_JoinAppearanceClick> clicks)
	{
		foreach (ELIFE_JoinAppearanceClick click : clicks)
		{
			if (click.m_Widget)
				click.m_Widget.RemoveFromHierarchy();

			ELIFE_JoinPreview.DeleteLocalCharacter(click.m_Entity);
			click.m_Entity = null;
		}

		clicks.Clear();
	}

	//------------------------------------------------------------------------------------------------
	//! A swatch picks its tone and rebuilds the heads at head 0; a head card picks that head. Ignored while busy.
	void OnAppearanceClicked(ELIFE_JoinAppearanceClick click)
	{
		if (m_bBusy || !click)
			return;

		if (click.m_bSkinTone)
		{
			if (click.m_iIndex == m_iSkin)
				return;

			m_iSkin = click.m_iIndex;
			RebuildHeads();
		}
		else
		{
			if (click.m_iIndex == m_iHead)
				return;

			m_iHead = click.m_iIndex;
			RefreshSelection();
			RefreshAppearance();
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Outlines the chosen swatch and head card and shows the chosen tone's name.
	protected void RefreshSelection()
	{
		foreach (ELIFE_JoinAppearanceClick click : m_aSkinClicks)
		{
			click.Refresh(click.m_iIndex == m_iSkin);
		}

		foreach (ELIFE_JoinAppearanceClick click : m_aHeadClicks)
		{
			click.Refresh(click.m_iIndex == m_iHead);
		}

		if (m_wSkinName)
		{
			string name = "";
			if (m_Config && m_iSkin >= 0)
				name = m_Config.m_aSkinTones[m_iSkin].m_sName;

			m_wSkinName.SetText(name);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Shows the full-body preview of the chosen pair: a local character with the body and head applied. The previous
	//! character is deleted once the new one is on screen. Without a local character the plain character prefab is shown.
	protected void RefreshAppearance()
	{
		IEntity previous = m_PreviewEntity;
		m_PreviewEntity = null;

		if (m_Config && m_iSkin >= 0)
		{
			ELIFE_SkinTone tone = m_Config.m_aSkinTones[m_iSkin];
			IEntity entity = ELIFE_JoinPreview.SpawnLocalCharacter(tone.m_sBody, tone.m_aHeads[m_iHead]);
			if (entity)
			{
				if (ELIFE_JoinPreview.ShowLocalCharacter(m_wPreview, entity))
					m_PreviewEntity = entity;
				else
					ELIFE_JoinPreview.DeleteLocalCharacter(entity);
			}
		}

		if (!m_PreviewEntity)
			ELIFE_JoinPreview.ShowPreview(m_wPreview, ELIFE_JoinPreview.GetCharacterPrefab());

		ELIFE_JoinPreview.DeleteLocalCharacter(previous);
	}

	//------------------------------------------------------------------------------------------------
	//! Deletes the full-body preview character of this screen.
	protected void DeletePreview()
	{
		ELIFE_JoinPreview.DeleteLocalCharacter(m_PreviewEntity);
		m_PreviewEntity = null;
	}

	//------------------------------------------------------------------------------------------------
	//! Tab (dir 1) and Ctrl+Tab (dir -1) move the focus to the next or previous usable control, wrapping. Usable means
	//! visible and enabled in the hierarchy, so a hidden Back is skipped. Ignored while busy or when the screen is hidden.
	void MoveFocus(int dir)
	{
		if (m_bBusy || !m_wRoot || !m_wRoot.IsVisibleInHierarchy())
			return;

		array<Widget> order = {};
		CollectFocusOrder(order);

		int count = order.Count();
		if (count == 0)
			return;

		//! Without a focused control in the list, Tab starts at the first and Ctrl+Tab at the last.
		int current = order.Find(GetGame().GetWorkspace().GetFocusedWidget());
		if (current < 0)
		{
			if (dir > 0)
				current = count - 1;
			else
				current = 0;
		}

		for (int step = 0; step < count; step++)
		{
			current = (current + dir + count) % count;
			Widget candidate = order[current];
			if (candidate.IsVisibleInHierarchy() && candidate.IsEnabledInHierarchy())
			{
				GetGame().GetWorkspace().SetFocusedWidget(candidate);
				return;
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The focus order: First name, Last name, skin swatches in row order, head cards of the visible page, head Prev and
	//! Next, Back, Create.
	//! Edit boxes are focused on their edit widget, so typing goes straight into the field.
	protected void CollectFocusOrder(array<Widget> order)
	{
		AddFocus(order, GetEditFocus(m_FirstName, m_wFirstName));
		AddFocus(order, GetEditFocus(m_LastName, m_wLastName));

		foreach (ELIFE_JoinAppearanceClick click : m_aSkinClicks)
		{
			AddFocus(order, click.m_Button);
		}

		foreach (ELIFE_JoinAppearanceClick click : m_aHeadClicks)
		{
			AddFocus(order, click.m_Button);
		}

		//! The head pager comes after the visible head cards, and only its visible and enabled buttons are listed.
		if (m_wHeadPrev && m_wHeadPrev.IsVisibleInHierarchy() && m_wHeadPrev.IsEnabledInHierarchy())
			AddFocus(order, m_wHeadPrev);

		if (m_wHeadNext && m_wHeadNext.IsVisibleInHierarchy() && m_wHeadNext.IsEnabledInHierarchy())
			AddFocus(order, m_wHeadNext);

		AddFocus(order, m_wBack);
		AddFocus(order, m_wCreate);
	}

	//------------------------------------------------------------------------------------------------
	//! The edit widget inside an edit box component, or the fallback root when the component is missing.
	protected Widget GetEditFocus(SCR_EditBoxComponent edit, Widget fallback)
	{
		if (edit && edit.m_wEditBox)
			return edit.m_wEditBox;

		return fallback;
	}

	//------------------------------------------------------------------------------------------------
	protected void AddFocus(array<Widget> order, Widget w)
	{
		if (w)
			order.Insert(w);
	}

	//------------------------------------------------------------------------------------------------
	//! A second click while busy does nothing, so a double-click on Create makes one character. Sends the config index of
	//! the chosen tone and the head index within that tone.
	protected void OnCreateClicked()
	{
		if (m_bBusy || !m_Controller || !m_FirstName || !m_LastName)
			return;

		string firstName = m_FirstName.GetValue();
		string lastName = m_LastName.GetValue();

		if (firstName.Length() > NAME_MAX || lastName.Length() > NAME_MAX)
		{
			SetError("#ELIFE-Join_NameTooLong");
			return;
		}

		if (m_iSkin < 0)
		{
			SetError(ERROR_APPEARANCE_INVALID);
			return;
		}

		SetBusy(true);
		m_Controller.ELIFE_RequestCreate(firstName, lastName, m_iSkin, m_iHead);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnBackClicked()
	{
		if (m_bBusy || !m_Controller)
			return;

		SetBusy(true);
		m_Controller.ELIFE_RequestBackToSelect();
	}

	//------------------------------------------------------------------------------------------------
	//! Error keys with the ELIFE-Join_ prefix are shown; anything else is ignored.
	protected string GetErrorText(string messageKey)
	{
		if (messageKey.Length() > ERROR_KEY_PREFIX.Length() && messageKey.Substring(0, ERROR_KEY_PREFIX.Length()) == ERROR_KEY_PREFIX)
			return "#" + messageKey;

		return "";
	}
}

//------------------------------------------------------------------------------------------------
//! One swatch or head card's click target. Kept in the screen's array so the ScriptInvoker callbacks stay alive.
class ELIFE_JoinAppearanceClick
{
	ELIFE_JoinCreateScreen m_Screen;
	//! True for a skin tone swatch, false for a head card.
	bool m_bSkinTone;
	//! Config index of the tone for a swatch, head index within the chosen tone for a head card.
	int m_iIndex;
	Widget m_Widget;
	Widget m_Button;
	Widget m_wSelected;
	//! The checkmark of a swatch or head card, shown with the outline.
	Widget m_wCheck;
	//! The local character shown in a head card; owned by the card and deleted with it. Null for swatches.
	IEntity m_Entity;

	//------------------------------------------------------------------------------------------------
	void OnClicked()
	{
		if (m_Screen)
			m_Screen.OnAppearanceClicked(this);
	}

	//------------------------------------------------------------------------------------------------
	//! The chosen swatch or head card gets the outline and the checkmark.
	void Refresh(bool selected)
	{
		if (m_wSelected)
			m_wSelected.SetVisible(selected);

		if (m_wCheck)
			m_wCheck.SetVisible(selected);
	}
}
