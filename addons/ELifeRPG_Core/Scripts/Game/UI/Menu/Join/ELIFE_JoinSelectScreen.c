//------------------------------------------------------------------------------------------------
//! Select screen (spec 5.5): one card per character plus a "+" card that opens create, six per page. The "+" card is
//! the last entry and counts toward paging. A single click highlights a character, Play (or a double-click on it) asks
//! the server to spawn it. Nothing else about the character is shown. Paging shows only when there are more than
//! CARDS_PER_PAGE cards; the selection survives paging.
class ELIFE_JoinSelectScreen
{
	protected const int DOUBLE_CLICK_MS = 400;
	protected const int NAME_MAX = 20;

	protected const ResourceName LAYOUT_CARD = "{0BA887B3D6005AE2}UI/layouts/Menus/Join/JoinCharacterCard.layout";

	protected const string ERROR_KEY_PREFIX = "ELIFE-Join_";
	protected const string NEW_CARD_KEY = "#ELIFE-Join_NewCharacter";

	protected Widget m_wRoot;
	protected Widget m_wCardRow;
	protected TextWidget m_wTitle;
	protected Widget m_wPager;
	protected TextWidget m_wPageLabel;
	protected Widget m_wErrorRow;
	protected TextWidget m_wError;
	protected SCR_ButtonTextComponent m_PrevButton;
	protected SCR_ButtonTextComponent m_NextButton;
	protected SCR_ButtonTextComponent m_PlayButton;
	protected SCR_ButtonTextComponent m_NewButton;

	protected ref array<ref ELIFE_JoinCardClick> m_aCards = {};
	//! Every card of the list in order, the "+" card last. Paging shows a slice of it.
	protected ref array<ref ELIFE_JoinSelectEntry> m_aEntries = {};

	protected ELIFE_PlayerController m_Controller;
	protected bool m_bBusy;
	protected int m_iPage;
	//! Entry index (position in m_aEntries) of the highlighted character, -1 for none.
	protected int m_iSelected = -1;
	protected int m_iLastClicked = -1;
	protected int m_iLastClickTick;

	//------------------------------------------------------------------------------------------------
	//! Binds the widgets of an already created JoinSelect layout.
	void Bind(Widget root)
	{
		m_wRoot = root;
		m_wCardRow = m_wRoot.FindAnyWidget("CardList");
		m_wTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("SelectTitle"));
		m_wPager = m_wRoot.FindAnyWidget("SelectPager");
		m_wPageLabel = TextWidget.Cast(m_wRoot.FindAnyWidget("SelectPageLabel"));
		m_wErrorRow = m_wRoot.FindAnyWidget("SelectErrorRow");
		m_wError = TextWidget.Cast(m_wRoot.FindAnyWidget("SelectError"));

		if (m_wTitle)
			m_wTitle.SetText("#ELIFE-Join_SelectTitle");

		m_PrevButton = SCR_ButtonTextComponent.GetButtonText("SelectPrev", m_wRoot);
		if (m_PrevButton)
		{
			m_PrevButton.SetText("<");
			m_PrevButton.m_OnClicked.Insert(OnPrevClicked);
		}

		m_NextButton = SCR_ButtonTextComponent.GetButtonText("SelectNext", m_wRoot);
		if (m_NextButton)
		{
			m_NextButton.SetText(">");
			m_NextButton.m_OnClicked.Insert(OnNextClicked);
		}

		m_PlayButton = SCR_ButtonTextComponent.GetButtonText("ButtonPlay", m_wRoot);
		if (m_PlayButton)
		{
			m_PlayButton.SetText("#ELIFE-Join_Play");
			m_PlayButton.m_OnClicked.Insert(OnPlayClicked);
		}

		m_NewButton = SCR_ButtonTextComponent.GetButtonText("ButtonNew", m_wRoot);
		if (m_NewButton)
		{
			m_NewButton.SetText(NEW_CARD_KEY);
			m_NewButton.m_OnClicked.Insert(OnNewClicked);
		}
	}

	//------------------------------------------------------------------------------------------------
	void SetVisible(bool visible)
	{
		if (m_wRoot)
			m_wRoot.SetVisible(visible);
	}

	//------------------------------------------------------------------------------------------------
	//! Rebuilds the first page from the payload. The menu clears busy state before calling this.
	void Show(ELIFE_JoinSelectPayload payload, ELIFE_PlayerController controller)
	{
		m_Controller = controller;
		m_iSelected = -1;
		m_iLastClicked = -1;

		m_aEntries.Clear();
		foreach (ELIFE_JoinCharacterEntry entry : payload.characters)
		{
			if (entry.id == "")
			{
				Print("ELIFE_Join | select payload has a character without an id, skipped", LogLevel.WARNING);
				continue;
			}

			ELIFE_JoinSelectEntry item = new ELIFE_JoinSelectEntry();
			item.m_sCharacterId = entry.id;
			item.m_sName = entry.name;
			item.m_bIsNew = false;
			m_aEntries.Insert(item);
		}

		ELIFE_JoinSelectEntry newEntry = new ELIFE_JoinSelectEntry();
		newEntry.m_bIsNew = true;
		m_aEntries.Insert(newEntry);

		SetError(GetErrorText(payload.error));

		ShowPage(0);
	}

	//------------------------------------------------------------------------------------------------
	//! Disables Prev, Next, Play, New and the cards while a request is in flight. Set by ELIFE_JoinMenu.SetBusy.
	void SetBusy(bool busy)
	{
		m_bBusy = busy;

		foreach (ELIFE_JoinCardClick card : m_aCards)
		{
			if (card.m_Button)
				card.m_Button.SetEnabled(!busy);
		}

		if (m_NewButton)
			m_NewButton.SetEnabled(!busy);

		RefreshPager();
		RefreshPlay();
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
	//! Rebuilds the cards of one page. The selection is kept by entry index, so a highlighted character stays highlighted.
	protected void ShowPage(int page)
	{
		int pages = ELIFE_JoinPaging.PageCount(m_aEntries.Count());
		if (page >= pages)
			page = pages - 1;
		if (page < 0)
			page = 0;

		m_iPage = page;
		ClearCards();

		int start = ELIFE_JoinPaging.PageStart(page);
		int end = start + ELIFE_JoinPaging.CARDS_PER_PAGE;
		if (end > m_aEntries.Count())
			end = m_aEntries.Count();

		for (int i = start; i < end; i++)
		{
			AddCard(i);
		}

		RefreshHighlight();
		RefreshPager();
		RefreshPlay();
	}

	//------------------------------------------------------------------------------------------------
	//! Adds the card of the entry at entryIndex to the current page.
	protected void AddCard(int entryIndex)
	{
		if (!m_wCardRow)
			return;

		ELIFE_JoinSelectEntry entry = m_aEntries[entryIndex];

		Widget cardRoot = GetGame().GetWorkspace().CreateWidgets(LAYOUT_CARD, m_wCardRow);
		if (!cardRoot)
			return;

		ELIFE_JoinCardClick card = new ELIFE_JoinCardClick();
		card.m_Screen = this;
		card.m_iEntryIndex = entryIndex;
		card.m_bIsNew = entry.m_bIsNew;
		card.m_Widget = cardRoot;
		card.m_Button = cardRoot.FindAnyWidget("CardButton");
		card.m_wName = TextWidget.Cast(cardRoot.FindAnyWidget("CardName"));
		card.m_wPlus = cardRoot.FindAnyWidget("CardPlus");
		card.m_wPreview = cardRoot.FindAnyWidget("Preview");
		card.m_wSelected = cardRoot.FindAnyWidget("CardSelected");

		//! The shade texture is dark at the top; the bottom shade is the same texture turned round.
		ImageWidget shadeBottom = ImageWidget.Cast(cardRoot.FindAnyWidget("ShadeBottom"));
		if (shadeBottom)
			shadeBottom.SetRotation(180);

		if (card.m_bIsNew)
		{
			//! The "+" card has no character to preview; it shows the plus sign and the create label.
			if (card.m_wPreview)
				card.m_wPreview.SetVisible(false);

			if (card.m_wPlus)
				card.m_wPlus.SetVisible(true);

			if (card.m_wName)
				card.m_wName.SetText(NEW_CARD_KEY);
		}
		else
		{
			if (card.m_wPlus)
				card.m_wPlus.SetVisible(false);

			if (card.m_wName)
			{
				string upper = entry.m_sName;
				upper.ToUpper();
				card.m_wName.SetText(upper);
			}

			//! Previews show appearance 0 for every character until the backend sends per-character appearance.
			// TODO: per-character appearance/portrait from backend
			ELIFE_JoinPreview.ShowPreview(card.m_wPreview, ELIFE_JoinPreview.GetCharacterPrefab());
		}

		//! The component sits on the CardButton, which is inside the size layout that the card root is.
		SCR_ButtonTextComponent button;
		if (card.m_Button)
			button = SCR_ButtonTextComponent.FindButtonTextComponent(card.m_Button);

		if (button)
			button.m_OnClicked.Insert(card.OnClicked);

		if (card.m_Button)
			card.m_Button.SetEnabled(!m_bBusy);

		m_aCards.Insert(card);
		card.Refresh(false);
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearCards()
	{
		foreach (ELIFE_JoinCardClick card : m_aCards)
		{
			if (card.m_Widget)
				card.m_Widget.RemoveFromHierarchy();
		}

		m_aCards.Clear();
	}

	//------------------------------------------------------------------------------------------------
	//! Single click highlights a character, a second click on it within DOUBLE_CLICK_MS plays it. "+" opens create at once.
	void OnCardClicked(int entryIndex)
	{
		if (m_bBusy || entryIndex < 0 || entryIndex >= m_aEntries.Count())
			return;

		ELIFE_JoinSelectEntry entry = m_aEntries[entryIndex];
		if (entry.m_bIsNew)
		{
			OpenCreate();
			return;
		}

		int now = System.GetTickCount();
		bool doubleClick = entryIndex == m_iLastClicked && now - m_iLastClickTick <= DOUBLE_CLICK_MS;

		m_iLastClicked = entryIndex;
		m_iLastClickTick = now;
		m_iSelected = entryIndex;
		RefreshHighlight();

		if (doubleClick)
			Play();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnPrevClicked()
	{
		if (m_bBusy || m_iPage <= 0)
			return;

		ShowPage(m_iPage - 1);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnNextClicked()
	{
		if (m_bBusy || m_iPage >= ELIFE_JoinPaging.PageCount(m_aEntries.Count()) - 1)
			return;

		ShowPage(m_iPage + 1);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnPlayClicked()
	{
		Play();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnNewClicked()
	{
		OpenCreate();
	}

	//------------------------------------------------------------------------------------------------
	//! Asks the server to open the create screen. Same path as the "+" card.
	protected void OpenCreate()
	{
		if (m_bBusy)
			return;

		SetBusy(true);
		if (m_Controller)
			m_Controller.ELIFE_RequestOpenCreate();
	}

	//------------------------------------------------------------------------------------------------
	protected void Play()
	{
		if (m_bBusy || m_iSelected < 0 || m_iSelected >= m_aEntries.Count())
			return;

		ELIFE_JoinSelectEntry entry = m_aEntries[m_iSelected];
		if (entry.m_bIsNew)
			return;

		SetBusy(true);
		if (m_Controller)
			m_Controller.ELIFE_RequestSelect(entry.m_sCharacterId);
	}

	//------------------------------------------------------------------------------------------------
	protected void RefreshHighlight()
	{
		foreach (ELIFE_JoinCardClick card : m_aCards)
		{
			card.Refresh(card.m_iEntryIndex == m_iSelected);
		}

		RefreshPlay();
	}

	//------------------------------------------------------------------------------------------------
	//! The pager shows only with more than CARDS_PER_PAGE cards; Prev and Next are off at the ends.
	protected void RefreshPager()
	{
		int pages = ELIFE_JoinPaging.PageCount(m_aEntries.Count());
		if (m_wPager)
			m_wPager.SetVisible(m_aEntries.Count() > ELIFE_JoinPaging.CARDS_PER_PAGE);

		if (m_wPageLabel)
			m_wPageLabel.SetText(string.Format("%1 / %2", m_iPage + 1, pages));

		if (m_PrevButton)
			m_PrevButton.SetEnabled(!m_bBusy && m_iPage > 0);

		if (m_NextButton)
			m_NextButton.SetEnabled(!m_bBusy && m_iPage < pages - 1);
	}

	//------------------------------------------------------------------------------------------------
	protected void RefreshPlay()
	{
		if (m_PlayButton)
			m_PlayButton.SetEnabled(!m_bBusy && m_iSelected >= 0);
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
//! One card's data: a character, or the "+" card (m_bIsNew) that opens create. The "+" card is always the last entry.
class ELIFE_JoinSelectEntry
{
	string m_sCharacterId;
	string m_sName;
	bool m_bIsNew;
}

//------------------------------------------------------------------------------------------------
//! One card's click target. Kept in the screen's array so the ScriptInvoker callback stays alive.
class ELIFE_JoinCardClick
{
	ELIFE_JoinSelectScreen m_Screen;
	int m_iEntryIndex;
	bool m_bIsNew;
	Widget m_Widget;
	Widget m_Button;
	TextWidget m_wName;
	Widget m_wPlus;
	Widget m_wPreview;
	Widget m_wSelected;

	//------------------------------------------------------------------------------------------------
	void OnClicked()
	{
		if (m_Screen)
			m_Screen.OnCardClicked(m_iEntryIndex);
	}

	//------------------------------------------------------------------------------------------------
	//! The selected character gets the vanilla focus outline; the "+" card never does.
	void Refresh(bool selected)
	{
		if (m_wSelected)
			m_wSelected.SetVisible(selected && !m_bIsNew);
	}
}
