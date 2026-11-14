//------------------------------------------------------------------------------------------------
//! M (vanilla's GadgetMap) without a map item: opens the map when the active phone has the map app installed.
class ELIFE_PhoneMapKey
{
	protected static InputManager s_RegisteredInput;

	//------------------------------------------------------------------------------------------------
	static void Register()
	{
		InputManager inputManager = GetGame().GetInputManager();
		if (!inputManager || s_RegisteredInput == inputManager)
			return;

		inputManager.RemoveActionListener("GadgetMap", EActionTrigger.DOWN, OnMapKey);
		inputManager.AddActionListener("GadgetMap", EActionTrigger.DOWN, OnMapKey);
		s_RegisteredInput = inputManager;
	}

	//------------------------------------------------------------------------------------------------
	static void OnMapKey()
	{
		IEntity character = SCR_PlayerController.GetLocalControlledEntity();
		if (!character)
			return;

		//! The open map closes itself on M (ELIFE_MapMenuUI).
		MenuManager menuManager = GetGame().GetMenuManager();
		if (!menuManager || menuManager.IsAnyMenuOpen() || ELIFE_MapMenuUI.HasMapItem())
			return;

		ELIFE_PhoneGadgetComponent phone = ELIFE_PhoneToggle.FindOwnedPhone(character);
		if (!phone || !phone.IsAppAvailable(EPhoneScreenState.MAP) || !SCR_MapEntity.GetMapInstance())
			return;

		menuManager.OpenMenu(ChimeraMenuPreset.ELIFE_MapMenu);
	}
}
