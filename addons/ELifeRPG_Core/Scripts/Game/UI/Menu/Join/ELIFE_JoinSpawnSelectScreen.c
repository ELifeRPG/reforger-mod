//------------------------------------------------------------------------------------------------
//! Spawn point screen (spec 4, 5): one card per spawn point of the server's spawn mode, six per page. A single click
//! highlights a point, Spawn (or a double-click on it) asks the server to spawn the character there. Paging shows only
//! when there are more than CARDS_PER_PAGE points; the selection survives paging.
class ELIFE_JoinSpawnSelectScreen
{
	protected const int DOUBLE_CLICK_MS = 400;

	protected const ResourceName LAYOUT_CARD = "{1C5A5ABBEA153555}UI/layouts/Menus/Join/JoinSpawnCard.layout";

	protected const string ERROR_KEY_PREFIX = "ELIFE-Join_";
	protected const string ERROR_KEY_HASH = "#";

	protected const float IMAGE_DIM = 0.85;
	protected const float IMAGE_LIT = 1.0;

	protected Widget m_wRoot;
	protected Widget m_wCardRow;
	protected TextWidget m_wTitle;
	protected Widget m_wPager;
	protected TextWidget m_wPageLabel;
	protected Widget m_wErrorRow;
	protected TextWidget m_wError;
	protected SCR_ButtonTextComponent m_PrevButton;
	protected SCR_ButtonTextComponent m_NextButton;
	protected SCR_ButtonTextComponent m_SpawnButton;

	protected ref array<ref ELIFE_JoinSpawnCardClick> m_aCards = {};
	protected ref array<ref ELIFE_JoinSpawnPointEntry> m_aPoints = {};

	protected ELIFE_PlayerController m_Controller;
	protected bool m_bBusy;
	protected int m_iPage;
	//! Entry index (ELIFE_JoinSpawnPointEntry.index) of the highlighted point, -1 for none.
	protected int m_iSelected = -1;
	protected int m_iLastClicked = -1;
	protected int m_iLastClickTick;

	//------------------------------------------------------------------------------------------------
	//! Binds the widgets of an already created JoinSpawnSelect layout.
	void Bind(Widget root)
	{
		m_wRoot = root;
		m_wCardRow = m_wRoot.FindAnyWidget("SpawnCardRow");
		m_wTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("SpawnSelectTitle"));
		m_wPager = m_wRoot.FindAnyWidget("SpawnPager");
		m_wPageLabel = TextWidget.Cast(m_wRoot.FindAnyWidget("SpawnPageLabel"));
		m_wErrorRow = m_wRoot.FindAnyWidget("SpawnErrorRow");
		m_wError = TextWidget.Cast(m_wRoot.FindAnyWidget("SpawnError"));

		if (m_wTitle)
			m_wTitle.SetText("#ELIFE-Join_SpawnSelectTitle");

		m_PrevButton = SCR_ButtonTextComponent.GetButtonText("SpawnPrev", m_wRoot);
		if (m_PrevButton)
		{
			m_PrevButton.SetText("<");
			m_PrevButton.m_OnClicked.Insert(OnPrevClicked);
		}

		m_NextButton = SCR_ButtonTextComponent.GetButtonText("SpawnNext", m_wRoot);
		if (m_NextButton)
		{
			m_NextButton.SetText(">");
			m_NextButton.m_OnClicked.Insert(OnNextClicked);
		}

		m_SpawnButton = SCR_ButtonTextComponent.GetButtonText("ButtonSpawn", m_wRoot);
		if (m_SpawnButton)
		{
			m_SpawnButton.SetText("#ELIFE-Join_Spawn");
			m_SpawnButton.m_OnClicked.Insert(OnSpawnClicked);
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
	void Show(ELIFE_JoinSpawnSelectPayload payload, ELIFE_PlayerController controller)
	{
		m_Controller = controller;
		m_iSelected = -1;
		m_iLastClicked = -1;
		m_iPage = 0;

		m_aPoints.Clear();
		if (payload)
		{
			foreach (ELIFE_JoinSpawnPointEntry entry : payload.points)
			{
				if (entry)
					m_aPoints.Insert(entry);
			}

			SetError(GetErrorText(payload.error));
		}
		else
		{
			SetError("");
		}

		ShowPage(0);
	}

	//------------------------------------------------------------------------------------------------
	//! Disables Prev, Next, Spawn and the cards while a request is in flight. Set by ELIFE_JoinMenu.SetBusy.
	void SetBusy(bool busy)
	{
		m_bBusy = busy;

		foreach (ELIFE_JoinSpawnCardClick card : m_aCards)
		{
			if (card.m_Button)
				card.m_Button.SetEnabled(!busy);
		}

		RefreshPager();
		RefreshSpawn();
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
	//! Rebuilds the cards of one page. The selection is kept by entry index, so a highlighted point stays highlighted.
	protected void ShowPage(int page)
	{
		int pages = ELIFE_JoinPaging.PageCount(m_aPoints.Count());
		if (page >= pages)
			page = pages - 1;
		if (page < 0)
			page = 0;

		m_iPage = page;
		ClearCards();

		int start = ELIFE_JoinPaging.PageStart(page);
		int end = start + ELIFE_JoinPaging.CARDS_PER_PAGE;
		if (end > m_aPoints.Count())
			end = m_aPoints.Count();

		for (int i = start; i < end; i++)
		{
			AddCard(m_aPoints[i]);
		}

		RefreshHighlight();
		RefreshPager();
		RefreshSpawn();
	}

	//------------------------------------------------------------------------------------------------
	protected void AddCard(ELIFE_JoinSpawnPointEntry entry)
	{
		if (!m_wCardRow)
			return;

		Widget cardRoot = GetGame().GetWorkspace().CreateWidgets(LAYOUT_CARD, m_wCardRow);
		if (!cardRoot)
			return;

		ELIFE_JoinSpawnCardClick card = new ELIFE_JoinSpawnCardClick();
		card.m_Screen = this;
		card.m_iEntryIndex = entry.index;
		card.m_Widget = cardRoot;
		card.m_Button = cardRoot.FindAnyWidget("SpawnCard");
		card.m_wImage = ImageWidget.Cast(cardRoot.FindAnyWidget("SpawnImage"));
		card.m_wImageScale = cardRoot.FindAnyWidget("SpawnImageScale");
		card.m_wSelected = cardRoot.FindAnyWidget("SpawnSelected");

		//! The shade texture is dark at the top; the bottom shade is the same texture turned round.
		ImageWidget shadeBottom = ImageWidget.Cast(cardRoot.FindAnyWidget("ShadeBottom"));
		if (shadeBottom)
			shadeBottom.SetRotation(180);

		TextWidget name = TextWidget.Cast(cardRoot.FindAnyWidget("SpawnName"));
		if (name)
		{
			string upper = entry.name;
			upper.ToUpper();
			name.SetText(upper);
		}

		//! No image, or one that does not load: the image is hidden and the card background shows.
		bool loaded = false;
		if (card.m_wImage && entry.image != "")
		{
			ResourceName texture = entry.image;
			loaded = card.m_wImage.LoadImageTexture(0, texture);
		}

		//! Scale to fill crops by the image's own size, so it must carry the texture's aspect.
		if (loaded)
		{
			int width, height;
			card.m_wImage.GetImageSize(0, width, height);
			if (width > 0 && height > 0)
				card.m_wImage.SetSize(width, height);
		}

		if (card.m_wImageScale)
			card.m_wImageScale.SetVisible(loaded);

		if (card.m_wImage)
		{
			card.m_wImage.SetVisible(loaded);
			card.m_wImage.SetColor(new Color(IMAGE_DIM, IMAGE_DIM, IMAGE_DIM, 1));
		}

		//! The component sits on the SpawnCard button, which is inside the size layout that the card root is.
		SCR_ButtonTextComponent button;
		if (card.m_Button)
			button = SCR_ButtonTextComponent.FindButtonTextComponent(card.m_Button);

		if (button)
		{
			button.m_OnClicked.Insert(card.OnClicked);
			button.GetOnMouseEnter().Insert(card.OnMouseEnter);
			button.GetOnMouseLeave().Insert(card.OnMouseLeave);
		}

		if (card.m_Button)
			card.m_Button.SetEnabled(!m_bBusy);

		m_aCards.Insert(card);
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearCards()
	{
		foreach (ELIFE_JoinSpawnCardClick card : m_aCards)
		{
			if (card.m_Widget)
				card.m_Widget.RemoveFromHierarchy();
		}

		m_aCards.Clear();
	}

	//------------------------------------------------------------------------------------------------
	//! Single click highlights a point, a second click on it within DOUBLE_CLICK_MS spawns there.
	void OnCardClicked(ELIFE_JoinSpawnCardClick card)
	{
		if (m_bBusy || !card)
			return;

		int now = System.GetTickCount();
		bool doubleClick = card.m_iEntryIndex == m_iLastClicked && now - m_iLastClickTick <= DOUBLE_CLICK_MS;

		m_iLastClicked = card.m_iEntryIndex;
		m_iLastClickTick = now;
		m_iSelected = card.m_iEntryIndex;
		RefreshHighlight();
		RefreshSpawn();

		if (doubleClick)
			Spawn();
	}

	//------------------------------------------------------------------------------------------------
	//! Brightens the image of a hovered card.
	void OnCardHover(ELIFE_JoinSpawnCardClick card, bool hovered)
	{
		if (!card || !card.m_wImage)
			return;

		float lit = IMAGE_DIM;
		if (hovered)
			lit = IMAGE_LIT;

		card.m_wImage.SetColor(new Color(lit, lit, lit, 1));
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
		if (m_bBusy || m_iPage >= ELIFE_JoinPaging.PageCount(m_aPoints.Count()) - 1)
			return;

		ShowPage(m_iPage + 1);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnSpawnClicked()
	{
		Spawn();
	}

	//------------------------------------------------------------------------------------------------
	//! Asks the server to spawn at the highlighted point. The server answers with IN_GAME or with this screen again.
	protected void Spawn()
	{
		if (m_bBusy || m_iSelected < 0)
			return;

		SetBusy(true);
		if (m_Controller)
			m_Controller.ELIFE_RequestSpawnPoint(m_iSelected);
	}

	//------------------------------------------------------------------------------------------------
	protected void RefreshHighlight()
	{
		foreach (ELIFE_JoinSpawnCardClick card : m_aCards)
		{
			card.Refresh(card.m_iEntryIndex == m_iSelected);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The pager shows only with more than CARDS_PER_PAGE points; Prev and Next are off at the ends.
	protected void RefreshPager()
	{
		int pages = ELIFE_JoinPaging.PageCount(m_aPoints.Count());
		if (m_wPager)
			m_wPager.SetVisible(m_aPoints.Count() > ELIFE_JoinPaging.CARDS_PER_PAGE);

		if (m_wPageLabel)
			m_wPageLabel.SetText(string.Format("%1 / %2", m_iPage + 1, pages));

		if (m_PrevButton)
			m_PrevButton.SetEnabled(!m_bBusy && m_iPage > 0);

		if (m_NextButton)
			m_NextButton.SetEnabled(!m_bBusy && m_iPage < pages - 1);
	}

	//------------------------------------------------------------------------------------------------
	protected void RefreshSpawn()
	{
		if (m_SpawnButton)
			m_SpawnButton.SetEnabled(!m_bBusy && m_iSelected >= 0);
	}

	//------------------------------------------------------------------------------------------------
	//! Error keys with the ELIFE-Join_ prefix are shown; anything else is ignored. A leading "#" is stripped before the check.
	protected string GetErrorText(string messageKey)
	{
		string key = messageKey;
		if (key.StartsWith(ERROR_KEY_HASH))
			key = key.Substring(ERROR_KEY_HASH.Length(), key.Length() - ERROR_KEY_HASH.Length());

		if (key.Length() > ERROR_KEY_PREFIX.Length() && key.Substring(0, ERROR_KEY_PREFIX.Length()) == ERROR_KEY_PREFIX)
			return "#" + key;

		return "";
	}
}

//------------------------------------------------------------------------------------------------
//! One spawn point card's click target. Kept in the screen's array so the ScriptInvoker callbacks stay alive.
class ELIFE_JoinSpawnCardClick
{
	ELIFE_JoinSpawnSelectScreen m_Screen;
	int m_iEntryIndex;
	Widget m_Widget;
	Widget m_Button;
	ImageWidget m_wImage;
	Widget m_wImageScale;
	Widget m_wSelected;

	//------------------------------------------------------------------------------------------------
	void OnClicked()
	{
		if (m_Screen)
			m_Screen.OnCardClicked(this);
	}

	//------------------------------------------------------------------------------------------------
	void OnMouseEnter()
	{
		if (m_Screen)
			m_Screen.OnCardHover(this, true);
	}

	//------------------------------------------------------------------------------------------------
	void OnMouseLeave()
	{
		if (m_Screen)
			m_Screen.OnCardHover(this, false);
	}

	//------------------------------------------------------------------------------------------------
	//! The highlighted card gets the vanilla focus outline.
	void Refresh(bool selected)
	{
		if (m_wSelected)
			m_wSelected.SetVisible(selected);
	}
}
