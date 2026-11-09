//------------------------------------------------------------------------------------------------
//! What a form shows and does. A feature subclasses it with its own layout; ELIFE_FormMenu hosts it, so a
//! package can add forms without its own entry in chimeraMenus.conf.
class ELIFE_FormContent : Managed
{
	protected ELIFE_FormMenu m_Menu;
	protected Widget m_wRoot;

	//------------------------------------------------------------------------------------------------
	//! The layout to show, filling the screen.
	ResourceName GetLayout()
	{
		return ResourceName.Empty;
	}

	//------------------------------------------------------------------------------------------------
	//! The layout exists now.
	void OnOpen(notnull ELIFE_FormMenu menu, notnull Widget root)
	{
		m_Menu = menu;
		m_wRoot = root;
	}

	//------------------------------------------------------------------------------------------------
	void OnUpdate(float tDelta)
	{
	}

	//------------------------------------------------------------------------------------------------
	//! However it closes.
	void OnClose()
	{
	}

	//------------------------------------------------------------------------------------------------
	Widget GetRootWidget()
	{
		return m_wRoot;
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_FormMenu GetMenu()
	{
		return m_Menu;
	}

	//------------------------------------------------------------------------------------------------
	bool IsFocused()
	{
		return m_Menu && m_Menu.IsFocused();
	}

	//------------------------------------------------------------------------------------------------
	void Close()
	{
		if (m_Menu)
			m_Menu.Close();
	}
}

//------------------------------------------------------------------------------------------------
//! A full-screen menu with nothing of its own: it shows an ELIFE_FormContent's layout and hands it the frames.
class ELIFE_FormMenu : ChimeraMenuBase
{
	protected ref ELIFE_FormContent m_Content;
	protected bool m_bClosed;

	//------------------------------------------------------------------------------------------------
	//! Null if nothing opened.
	static ELIFE_FormMenu Open(notnull ELIFE_FormContent content)
	{
		MenuManager menuManager = GetGame().GetMenuManager();
		if (!menuManager)
			return null;

		ELIFE_FormMenu menu = ELIFE_FormMenu.Cast(menuManager.OpenMenu(ChimeraMenuPreset.ELIFE_FormMenu));
		if (!menu)
			return null;

		if (!menu.Setup(content))
			return null;

		return menu;
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_FormContent GetContent()
	{
		return m_Content;
	}

	//------------------------------------------------------------------------------------------------
	protected bool Setup(notnull ELIFE_FormContent content)
	{
		Widget root = GetGame().GetWorkspace().CreateWidgets(content.GetLayout(), GetRootWidget());
		if (!root)
		{
			Print(string.Format("ELIFE_FormMenu | layout %1 failed to load", content.GetLayout()), LogLevel.ERROR);
			Close();
			return false;
		}

		FrameSlot.SetAnchorMin(root, 0, 0);
		FrameSlot.SetAnchorMax(root, 1, 1);
		FrameSlot.SetOffsets(root, 0, 0, 0, 0);

		m_Content = content;
		content.OnOpen(this, root);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuUpdate(float tDelta)
	{
		super.OnMenuUpdate(tDelta);

		if (m_Content && !m_bClosed)
			m_Content.OnUpdate(tDelta);
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuClose()
	{
		m_bClosed = true;
		if (m_Content)
			m_Content.OnClose();

		super.OnMenuClose();
	}
}
