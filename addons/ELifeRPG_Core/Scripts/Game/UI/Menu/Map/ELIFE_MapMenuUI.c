//------------------------------------------------------------------------------------------------
//! What closing the map does with the phone.
enum ELIFE_EPhoneAfterMap
{
	LEAVE,		//!< Nothing: it isn't out, or vanilla takes over.
	RETURN,		//!< Open it on its home screen.
	PUT_AWAY	//!< Holster it.
}

//------------------------------------------------------------------------------------------------
//! Vanilla fullscreen map opened without a map item (M with the phone's map app, or from the phone). Vanilla closes
//! its map through the map item; here M (GadgetMap) or Esc/B (MapEscape) close it, the actions of vanilla's
//! GadgetMapContext, which stays active while this menu is open. The phone key (N) closes it too (ELIFE_PhoneToggle).
class ELIFE_MapMenuUI : SCR_MapMenuUI
{
	//! The press that opened the map doesn't close it again.
	protected static const int CLOSE_GRACE_MS = 250;

	//! Frames the phone waits for the map to close, about a second.
	protected static const int PHONE_OPEN_TRIES = 60;
	protected static int s_iPhoneOpenTries;

	protected int m_iOpenedAt;
	protected ELIFE_EPhoneAfterMap m_ePhoneAfter = ELIFE_EPhoneAfterMap.LEAVE;

	//------------------------------------------------------------------------------------------------
	//! The open one of ours, or null.
	static ELIFE_MapMenuUI GetOpen()
	{
		MenuManager menuManager = GetGame().GetMenuManager();
		if (!menuManager)
			return null;

		ELIFE_MapMenuUI menu = ELIFE_MapMenuUI.Cast(menuManager.FindMenuByPreset(ChimeraMenuPreset.ELIFE_PhoneMapMenu));
		if (!menu)
			menu = ELIFE_MapMenuUI.Cast(menuManager.FindMenuByPreset(ChimeraMenuPreset.ELIFE_MapMenu));

		return menu;
	}

	//------------------------------------------------------------------------------------------------
	//! Opened from the phone rather than with M.
	bool IsFromPhone()
	{
		return false;
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuOpen()
	{
		super.OnMenuOpen();

		m_iOpenedAt = System.GetTickCount();
		InputManager inputManager = GetGame().GetInputManager();
		inputManager.AddActionListener("GadgetMap", EActionTrigger.DOWN, OnMapAction);
		inputManager.AddActionListener("MapEscape", EActionTrigger.DOWN, OnEscapeAction);
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuUpdate(float tDelta)
	{
		super.OnMenuUpdate(tDelta);
		GetGame().GetInputManager().ActivateContext("GadgetMapContext");
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuClose()
	{
		InputManager inputManager = GetGame().GetInputManager();
		inputManager.RemoveActionListener("GadgetMap", EActionTrigger.DOWN, OnMapAction);
		inputManager.RemoveActionListener("MapEscape", EActionTrigger.DOWN, OnEscapeAction);

		super.OnMenuClose();

		if (m_ePhoneAfter == ELIFE_EPhoneAfterMap.RETURN)
			OpenPhoneWhenMapClosed();
		else if (m_ePhoneAfter == ELIFE_EPhoneAfterMap.PUT_AWAY)
			ELIFE_PhoneToggle.HolsterOwnedPhone(SCR_PlayerController.GetLocalControlledEntity());
	}

	//------------------------------------------------------------------------------------------------
	void CloseMap(ELIFE_EPhoneAfterMap phoneAfter)
	{
		m_ePhoneAfter = phoneAfter;
		Close();
	}

	//------------------------------------------------------------------------------------------------
	//! The phone key: from the phone's map it puts the phone away, from the M map it swaps the map for the phone.
	void CloseForPhoneKey()
	{
		if (IsFromPhone())
			CloseMap(ELIFE_EPhoneAfterMap.PUT_AWAY);
		else
			CloseMap(ELIFE_EPhoneAfterMap.RETURN);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnMapAction()
	{
		if (System.GetTickCount() - m_iOpenedAt < CLOSE_GRACE_MS)
			return;

		//! With a map item, vanilla's M takes it into the hand in place of the phone and opens its own map.
		if (IsFromPhone() && !HasMapItem())
			CloseMap(ELIFE_EPhoneAfterMap.RETURN);
		else
			CloseMap(ELIFE_EPhoneAfterMap.LEAVE);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnEscapeAction()
	{
		if (System.GetTickCount() - m_iOpenedAt < CLOSE_GRACE_MS)
			return;

		if (IsFromPhone())
			CloseMap(ELIFE_EPhoneAfterMap.PUT_AWAY);
		else
			CloseMap(ELIFE_EPhoneAfterMap.LEAVE);
	}

	//------------------------------------------------------------------------------------------------
	//! The local player carries a map item, which vanilla opens on M.
	static bool HasMapItem()
	{
		IEntity character = SCR_PlayerController.GetLocalControlledEntity();
		if (!character)
			return false;

		SCR_GadgetManagerComponent gadgets = SCR_GadgetManagerComponent.GetGadgetManager(character);
		if (!gadgets)
			return false;

		return gadgets.GetGadgetByType(EGadgetType.MAP) != null;
	}

	//------------------------------------------------------------------------------------------------
	//! Opens the phone on its home screen once no map of ours is on the stack. Static: the closing menu may be gone
	//! before a call queued on it runs.
	static void OpenPhoneWhenMapClosed()
	{
		s_iPhoneOpenTries = 0;
		GetGame().GetCallqueue().Remove(TryOpenPhone);
		GetGame().GetCallqueue().CallLater(TryOpenPhone, 0, true);
	}

	//------------------------------------------------------------------------------------------------
	protected static void TryOpenPhone()
	{
		s_iPhoneOpenTries++;
		IEntity character = SCR_PlayerController.GetLocalControlledEntity();
		if (!character || s_iPhoneOpenTries > PHONE_OPEN_TRIES)
		{
			GetGame().GetCallqueue().Remove(TryOpenPhone);
			return;
		}

		if (GetOpen())
			return;

		GetGame().GetCallqueue().Remove(TryOpenPhone);
		ELIFE_PhoneGadgetComponent phone = ELIFE_PhoneToggle.FindOwnedPhone(character);
		if (!phone)
			return;

		SCR_GadgetManagerComponent gadgetManager = SCR_GadgetManagerComponent.GetGadgetManager(character);
		if (gadgetManager && phone.GetMode() != EGadgetMode.IN_HAND)
			gadgetManager.SetGadgetMode(phone.GetOwner(), EGadgetMode.IN_HAND);

		phone.OpenPhoneMenu();
		phone.SetScreenState(EPhoneScreenState.HOME);
	}
}
