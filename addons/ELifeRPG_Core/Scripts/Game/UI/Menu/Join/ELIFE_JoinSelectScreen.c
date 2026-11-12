//------------------------------------------------------------------------------------------------
//! Select screen (spec 5.5): one card per character plus a "+" card that opens create. A single click highlights a
//! character, Play (or a double-click on it) asks the server to spawn it. Nothing else about the character is shown.
class ELIFE_JoinSelectScreen
{
	protected const int CARDS_PER_ROW = 3;
	protected const int DOUBLE_CLICK_MS = 400;
	protected const int NAME_MAX = 20;

	protected const ResourceName LAYOUT_ROW = "{8C33390AEF438F42}UI/layouts/Menus/Join/JoinCharacterRow.layout";
	protected const ResourceName LAYOUT_CARD = "{0BA887B3D6005AE2}UI/layouts/Menus/Join/JoinCharacterCard.layout";

	protected const string ERROR_KEY_PREFIX = "ELIFE-Join_";

	protected Widget m_wRoot;
	protected Widget m_wCardList;
	protected TextWidget m_wTitle;
	protected Widget m_wErrorRow;
	protected TextWidget m_wError;
	protected SCR_ButtonTextComponent m_PlayButton;
	protected SCR_ButtonTextComponent m_NewButton;

	protected ref array<ref ELIFE_JoinCardClick> m_aCards = {};
	protected ref array<Widget> m_aRows = {};

	protected ELIFE_PlayerController m_Controller;
	protected bool m_bBusy;
	protected int m_iSelected = -1;
	protected int m_iLastClicked = -1;
	protected int m_iLastClickTick;

	//------------------------------------------------------------------------------------------------
	//! Binds the widgets of an already created JoinSelect layout.
	void Bind(Widget root)
	{
		m_wRoot = root;
		m_wCardList = m_wRoot.FindAnyWidget("CardList");
		m_wTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("SelectTitle"));
		m_wErrorRow = m_wRoot.FindAnyWidget("SelectErrorRow");
		m_wError = TextWidget.Cast(m_wRoot.FindAnyWidget("SelectError"));

		if (m_wTitle)
			m_wTitle.SetText("#ELIFE-Join_SelectTitle");

		m_PlayButton = SCR_ButtonTextComponent.GetButtonText("ButtonPlay", m_wRoot);
		if (m_PlayButton)
		{
			m_PlayButton.SetText("#ELIFE-Join_Play");
			m_PlayButton.m_OnClicked.Insert(OnPlayClicked);
		}

		m_NewButton = SCR_ButtonTextComponent.GetButtonText("ButtonNew", m_wRoot);
		if (m_NewButton)
		{
			m_NewButton.SetText("#ELIFE-Join_NewCharacter");
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
	//! Rebuilds the cards from the payload. The menu clears busy state before calling this.
	void Show(ELIFE_JoinSelectPayload payload, ELIFE_PlayerController controller)
	{
		m_Controller = controller;
		m_iSelected = -1;
		m_iLastClicked = -1;

		ClearCards();

		if (!m_wCardList)
			return;

		foreach (ELIFE_JoinCharacterEntry entry : payload.characters)
		{
			if (entry.id == "")
			{
				Print("ELIFE_Join | select payload has a character without an id, skipped", LogLevel.WARNING);
				continue;
			}

			AddCard(entry.id, entry.name, false);
		}

		AddCard("", "+", true);

		SetError(GetErrorText(payload.error));

		RefreshPlay();
	}

	//------------------------------------------------------------------------------------------------
	//! Disables Play, New and the cards while a request is in flight. Set by ELIFE_JoinMenu.SetBusy.
	void SetBusy(bool busy)
	{
		m_bBusy = busy;

		foreach (ELIFE_JoinCardClick card : m_aCards)
		{
			if (card.m_Widget)
				card.m_Widget.SetEnabled(!busy);
		}

		if (m_NewButton)
			m_NewButton.SetEnabled(!busy);

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
	protected void AddCard(string characterId, string name, bool isNew)
	{
		//! One row per CARDS_PER_ROW cards; a new row is created once the last one is full.
		int index = m_aCards.Count();
		if (index % CARDS_PER_ROW == 0)
		{
			Widget row = GetGame().GetWorkspace().CreateWidgets(LAYOUT_ROW, m_wCardList);
			if (!row)
				return;

			m_aRows.Insert(row);
		}

		Widget parent = m_aRows[m_aRows.Count() - 1];
		Widget cardRoot = GetGame().GetWorkspace().CreateWidgets(LAYOUT_CARD, parent);
		if (!cardRoot)
			return;

		ELIFE_JoinCardClick card = new ELIFE_JoinCardClick();
		card.m_Screen = this;
		card.m_iIndex = index;
		card.m_sCharacterId = characterId;
		card.m_bIsNew = isNew;
		card.m_Widget = cardRoot;
		cardRoot.SetEnabled(!m_bBusy);
		card.m_wName = TextWidget.Cast(cardRoot.FindAnyWidget("CardName"));
		card.m_wSelected = cardRoot.FindAnyWidget("CardSelected");
		card.m_sName = name;

		//! The "+" card has no character to preview.
		Widget previewSize = cardRoot.FindAnyWidget("PreviewSize");
		if (card.m_bIsNew)
		{
			if (previewSize)
				previewSize.SetVisible(false);
		}
		else
		{
			//! Previews show appearance 0 for every character until the backend sends per-character appearance.
			// TODO: per-character appearance/portrait from backend
			ELIFE_JoinPreview.ShowPreview(cardRoot.FindAnyWidget("Preview"), ELIFE_JoinPreview.GetAppearancePrefab(0));
		}

		SCR_ButtonTextComponent button = SCR_ButtonTextComponent.FindButtonTextComponent(cardRoot);
		if (button)
			button.m_OnClicked.Insert(card.OnClicked);

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

		foreach (Widget row : m_aRows)
		{
			if (row)
				row.RemoveFromHierarchy();
		}

		m_aCards.Clear();
		m_aRows.Clear();
	}

	//------------------------------------------------------------------------------------------------
	//! Single click highlights a character, a second click on it within DOUBLE_CLICK_MS plays it. "+" opens create at once.
	void OnCardClicked(int index)
	{
		if (m_bBusy || index < 0 || index >= m_aCards.Count())
			return;

		ELIFE_JoinCardClick card = m_aCards[index];
		if (card.m_bIsNew)
		{
			OpenCreate();
			return;
		}

		int now = System.GetTickCount();
		bool doubleClick = index == m_iLastClicked && now - m_iLastClickTick <= DOUBLE_CLICK_MS;

		m_iLastClicked = index;
		m_iLastClickTick = now;
		m_iSelected = index;
		RefreshHighlight();

		if (doubleClick)
			Play();
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
		if (m_bBusy || m_iSelected < 0 || m_iSelected >= m_aCards.Count())
			return;

		ELIFE_JoinCardClick card = m_aCards[m_iSelected];
		if (card.m_bIsNew)
			return;

		SetBusy(true);
		if (m_Controller)
			m_Controller.ELIFE_RequestSelect(card.m_sCharacterId);
	}

	//------------------------------------------------------------------------------------------------
	protected void RefreshHighlight()
	{
		foreach (ELIFE_JoinCardClick card : m_aCards)
		{
			card.Refresh(card.m_iIndex == m_iSelected);
		}

		RefreshPlay();
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
//! One card's click target. Kept in the screen's array so the ScriptInvoker callback stays alive.
class ELIFE_JoinCardClick
{
	ELIFE_JoinSelectScreen m_Screen;
	int m_iIndex;
	string m_sCharacterId;
	string m_sName;
	bool m_bIsNew;
	Widget m_Widget;
	TextWidget m_wName;
	Widget m_wSelected;

	//------------------------------------------------------------------------------------------------
	void OnClicked()
	{
		if (m_Screen)
			m_Screen.OnCardClicked(m_iIndex);
	}

	//------------------------------------------------------------------------------------------------
	//! The selected character gets the vanilla focus outline; the "+" card never does.
	void Refresh(bool selected)
	{
		if (m_wSelected)
			m_wSelected.SetVisible(selected && !m_bIsNew);

		if (!m_wName)
			return;

		if (m_bIsNew)
			m_wName.SetText("+");
		else
			m_wName.SetText(m_sName);
	}
}
