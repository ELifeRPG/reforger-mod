//------------------------------------------------------------------------------------------------
//! Join menu (spec 5.5). Opened and driven by ShowFromServer, which the owner RPC RpcDo_ShowSession calls.
//! Covers the whole join with the vanilla menu backdrop: Connecting and Loading use the vanilla loading overlay, Error, Banned and
//! Whitelist open a vanilla configurable dialog whose confirm, cancel or close leaves. It closes on IN_GAME. Escape and the pause menu are swallowed while it is open.
class ELIFE_JoinMenu : ChimeraMenuBase
{
	protected const ResourceName JOIN_DIALOGS_CONFIG = "{5BDB965394AEB196}Configs/Dialogs/ELIFE_JoinDialogs.conf";
	protected const ResourceName LAYOUT_SELECT = "{5FC75D4AF9E47587}UI/layouts/Menus/Join/JoinSelect.layout";
	protected const ResourceName LAYOUT_CREATE = "{6992DAA289EE19DA}UI/layouts/Menus/Join/JoinCreate.layout";
	protected const ResourceName LAYOUT_LINK = "{A2BDFD05AABB7733}UI/layouts/Menus/Join/JoinLink.layout";

	//! Presets in JOIN_DIALOGS_CONFIG.
	protected const string DIALOG_TAG_ERROR = "ELIFE_JoinError";
	protected const string DIALOG_TAG_BANNED = "ELIFE_JoinBanned";
	protected const string DIALOG_TAG_WHITELIST = "ELIFE_JoinWhitelist";

	//! Prefix every server error key must carry; anything else falls back to the generic backend error.
	protected const string ERROR_KEY_PREFIX = "ELIFE-Join_";
	protected const int ERROR_KEY_PREFIX_LENGTH = 11;

	//! The server kicks a rejected screen at 120 s. This client-side fallback only matters when the server can't kick,
	//! e.g. the host in Workbench or a listen server.
	protected const int CLIENT_LEAVE_FALLBACK_MS = 125000;
	//! Lets the AckClosed RPC go out before the client leaves the gameplay state.
	protected const int CLIENT_LEAVE_DELAY_MS = 500;

	protected Widget m_wJoinRoot;
	protected Widget m_wContent;
	// TODO: remove debug dump after the join UI visibility bug is fixed
	protected Widget m_wSelectRoot;
	// TODO: remove debug dump after the join UI visibility bug is fixed
	protected Widget m_wCreateRoot;
	protected ref ELIFE_JoinSelectScreen m_SelectScreen;
	protected ref ELIFE_JoinCreateScreen m_CreateScreen;
	protected ref ELIFE_JoinLinkScreen m_LinkScreen;

	//! The open rejection dialog, if any. Vanilla owns the dialog; this is only a reference.
	protected SCR_ConfigurableDialogUi m_Dialog;
	protected ELIFE_EJoinScreen m_eDialogScreen;
	//! The menu object of m_Dialog, to see whether the dialog is still open. Vanilla's CurrentDialog is never cleared, so it can't tell.
	protected MenuBase m_DialogMenu;
	//! Set once the Leave path has run. Leave runs at most once per menu.
	protected bool m_bLeaving;

	protected bool m_bBuilt;
	protected bool m_bBusy;
	protected bool m_bFallbackArmed;

	//! ShowFromServer can arrive before the widgets exist, so the screen waits here until OnMenuOpen.
	protected bool m_bHasPending;
	protected ELIFE_EJoinScreen m_ePendingScreen;
	protected string m_sPendingPayload;

	//------------------------------------------------------------------------------------------------
	//! Client only. Opens the join menu if it isn't up, then shows the screen. IN_GAME without an open menu does nothing.
	static void ShowFromServer(int screen, string payloadJson)
	{
		//! A dedicated server has no UI.
		if (System.IsConsoleApp())
			return;

		MenuManager menuManager = GetGame().GetMenuManager();
		if (!menuManager)
			return;

		ELIFE_JoinMenu menu = ELIFE_JoinMenu.Cast(menuManager.FindMenuByPreset(ChimeraMenuPreset.ELIFE_JoinMenu));
		if (!menu)
		{
			if (screen == ELIFE_EJoinScreen.IN_GAME)
				return;

			menu = ELIFE_JoinMenu.Cast(menuManager.OpenMenu(ChimeraMenuPreset.ELIFE_JoinMenu));
		}

		if (!menu)
		{
			Print("ELIFE_Join | could not open the join menu", LogLevel.WARNING);
			return;
		}

		menu.ShowScreen(screen, payloadJson);
	}

	//------------------------------------------------------------------------------------------------
	//! Client only. Leaves gameplay after a short delay so the AckClosed RPC goes out first. Does nothing if a transition is already running.
	static void LeaveGameplay(string reason)
	{
		if (GameStateTransitions.IsTransitionRequestedOrInProgress())
			return;

		Print(string.Format("ELIFE_Join | client leaving: %1", reason), LogLevel.NORMAL);
		GetGame().GetCallqueue().CallLater(RequestGameplayEnd, CLIENT_LEAVE_DELAY_MS, false);
	}

	//------------------------------------------------------------------------------------------------
	protected static void RequestGameplayEnd()
	{
		if (!GameStateTransitions.IsTransitionRequestedOrInProgress())
			GameStateTransitions.RequestGameplayEndTransition();
	}

	//------------------------------------------------------------------------------------------------
	//! Starts the client fallback once per rejected screen; duplicate pushes do not restart it.
	protected void ArmClientLeaveFallback()
	{
		if (m_bFallbackArmed)
			return;

		m_bFallbackArmed = true;
		GetGame().GetCallqueue().CallLater(OnClientLeaveFallback, CLIENT_LEAVE_FALLBACK_MS, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void DisarmClientLeaveFallback()
	{
		if (!m_bFallbackArmed)
			return;

		m_bFallbackArmed = false;
		GetGame().GetCallqueue().Remove(OnClientLeaveFallback);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnClientLeaveFallback()
	{
		m_bFallbackArmed = false;
		LeaveGameplay("kick fallback");
	}

	//------------------------------------------------------------------------------------------------
	//! Applies a screen now, or once the menu has been built when it is still opening.
	void ShowScreen(ELIFE_EJoinScreen screen, string payloadJson)
	{
		if (!m_bBuilt)
		{
			m_bHasPending = true;
			m_ePendingScreen = screen;
			m_sPendingPayload = payloadJson;
			return;
		}

		ApplyScreen(screen, payloadJson);
	}

	//------------------------------------------------------------------------------------------------
	//! Enables or disables the buttons of the current screen.
	void SetBusy(bool busy)
	{
		m_bBusy = busy;

		if (m_SelectScreen)
			m_SelectScreen.SetBusy(busy);

		if (m_CreateScreen)
			m_CreateScreen.SetBusy(busy);

		if (m_LinkScreen)
			m_LinkScreen.SetBusy(busy);
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_PlayerController GetController()
	{
		return ELIFE_PlayerController.Cast(GetGame().GetPlayerController());
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuOpen()
	{
		super.OnMenuOpen();

		BuildWidgets();
		HideVanillaLoadingPlaceholder();

		InputManager inputManager = GetGame().GetInputManager();
		if (inputManager)
		{
			inputManager.AddActionListener("MenuBack", EActionTrigger.DOWN, OnSwallowAction);
			inputManager.AddActionListener("MenuOpen", EActionTrigger.DOWN, OnSwallowAction);
#ifdef WORKBENCH
			inputManager.AddActionListener("MenuBackWB", EActionTrigger.DOWN, OnSwallowAction);
			inputManager.AddActionListener("MenuOpenWB", EActionTrigger.DOWN, OnSwallowAction);
#endif
		}

		if (m_bHasPending)
		{
			m_bHasPending = false;
			ApplyScreen(m_ePendingScreen, m_sPendingPayload);
		}
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuClose()
	{
		DisarmClientLeaveFallback();
		CloseDialog();
		HideLoading();

		//! Cancels the link screen's "Copied" timers, so they can't fire after the menu closed.
		if (m_LinkScreen)
			m_LinkScreen.SetVisible(false);

		InputManager inputManager = GetGame().GetInputManager();
		if (inputManager)
		{
			inputManager.RemoveActionListener("MenuBack", EActionTrigger.DOWN, OnSwallowAction);
			inputManager.RemoveActionListener("MenuOpen", EActionTrigger.DOWN, OnSwallowAction);
#ifdef WORKBENCH
			inputManager.RemoveActionListener("MenuBackWB", EActionTrigger.DOWN, OnSwallowAction);
			inputManager.RemoveActionListener("MenuOpenWB", EActionTrigger.DOWN, OnSwallowAction);
#endif
		}

		super.OnMenuClose();
	}

	//------------------------------------------------------------------------------------------------
	//! Catches a dialog that closed without our close handler, so its Leave still runs.
	override void OnMenuUpdate(float tDelta)
	{
		super.OnMenuUpdate(tDelta);

		if (m_Dialog && IsDialogVanished())
			OnDialogVanished();
	}

	//------------------------------------------------------------------------------------------------
	//! Swallowed while open, so Escape neither closes the menu nor opens the pause menu.
	protected void OnSwallowAction()
	{
	}

	//------------------------------------------------------------------------------------------------
	// TODO: local menu camera at m_vMenuCameraPosition/m_vMenuCameraAngles (needs Workbench verification)
	protected void BuildWidgets()
	{
		if (m_bBuilt)
			return;

		m_wJoinRoot = GetRootWidget();
		if (!m_wJoinRoot)
		{
			Print("ELIFE_Join | join menu has no root widget", LogLevel.WARNING);
			return;
		}

		m_wContent = m_wJoinRoot.FindAnyWidget("Content");
		if (!m_wContent)
		{
			Print("ELIFE_Join | join menu has no Content slot", LogLevel.WARNING);
			return;
		}

		//! The inherited MenuBackground children share ZOrder 0 and come after Content, so without this they cover the join screens.
		m_wContent.SetZOrder(1);

		Widget selectRoot = CreateStretched(LAYOUT_SELECT, m_wContent);
		m_wSelectRoot = selectRoot; // TODO: remove debug dump after the join UI visibility bug is fixed
		if (selectRoot)
		{
			m_SelectScreen = new ELIFE_JoinSelectScreen();
			m_SelectScreen.Bind(selectRoot);
		}

		Widget createRoot = CreateStretched(LAYOUT_CREATE, m_wContent);
		m_wCreateRoot = createRoot; // TODO: remove debug dump after the join UI visibility bug is fixed
		if (createRoot)
		{
			m_CreateScreen = new ELIFE_JoinCreateScreen();
			m_CreateScreen.Bind(createRoot);
		}

		Widget linkRoot = CreateStretched(LAYOUT_LINK, m_wContent);
		if (linkRoot)
		{
			m_LinkScreen = new ELIFE_JoinLinkScreen();
			m_LinkScreen.Bind(linkRoot);
			m_LinkScreen.GetOnLeave().Insert(OnLinkLeaveClicked);
		}

		m_bBuilt = true;
		ShowSelectCreateVisible(false);
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyScreen(ELIFE_EJoinScreen screen, string payloadJson)
	{
		//! Every incoming screen answers the request in flight, so the buttons come back.
		SetBusy(false);

		//! Only a rejected screen starts the client fallback; any other screen cancels it.
		bool rejected = IsRejectedScreen(screen);
		if (rejected)
			ArmClientLeaveFallback();
		else
			DisarmClientLeaveFallback();

		//! A dialog closed by a path that skipped our close handler (Escape through the menu manager) counts as closed by the player.
		if (m_Dialog && IsDialogVanished())
			OnDialogVanished();

		//! A repeat of the rejected screen whose dialog is still open changes nothing. Any other screen closes that dialog first.
		if (m_Dialog)
		{
			if (rejected && screen == m_eDialogScreen)
				return;

			CloseDialog();
		}

		//! IN_GAME closes the menu below, and vanilla handles the spawn from there.
		if (screen != ELIFE_EJoinScreen.IN_GAME)
			HideVanillaLoadingPlaceholder();

		switch (screen)
		{
			case ELIFE_EJoinScreen.CONNECTING:
			{
				ShowLoading("#ELIFE-Join_Connecting");
				return;
			}

			case ELIFE_EJoinScreen.LOADING:
			{
				ShowLoading("#ELIFE-Join_Loading");
				return;
			}

			case ELIFE_EJoinScreen.SELECTING:
			{
				ShowSelect(payloadJson);
				return;
			}

			case ELIFE_EJoinScreen.CREATING:
			{
				ShowCreate(payloadJson);
				return;
			}

			case ELIFE_EJoinScreen.LINKING:
			{
				ShowLink(payloadJson);
				return;
			}

			case ELIFE_EJoinScreen.ERROR:
			{
				ShowError(payloadJson);
				return;
			}

			case ELIFE_EJoinScreen.BANNED:
			{
				ShowBanned(payloadJson);
				return;
			}

			case ELIFE_EJoinScreen.NOT_WHITELISTED:
			{
				ShowWhitelist(payloadJson);
				return;
			}

			case ELIFE_EJoinScreen.IN_GAME:
			{
				Close();
				return;
			}
		}

		Print(string.Format("ELIFE_Join | unknown join screen %1, showing the backend error", screen), LogLevel.WARNING);
		array<string> noFields = {};
		ShowRejected(ELIFE_EJoinScreen.ERROR, DIALOG_TAG_ERROR, "#ELIFE-Join_ErrorBackend", noFields, noFields);
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsRejectedScreen(ELIFE_EJoinScreen screen)
	{
		return screen == ELIFE_EJoinScreen.ERROR || screen == ELIFE_EJoinScreen.BANNED || screen == ELIFE_EJoinScreen.NOT_WHITELISTED;
	}

	//------------------------------------------------------------------------------------------------
	//! Vanilla's respawn system shows its own loading placeholder over the join menu until the player spawns; hide it so this menu is the only thing shown.
	protected void HideVanillaLoadingPlaceholder()
	{
		SCR_RespawnSystemComponent respawnSystem = SCR_RespawnSystemComponent.GetInstance();
		if (respawnSystem)
			respawnSystem.DestroyLoadingPlaceholder();
	}

	//------------------------------------------------------------------------------------------------
	protected void ShowSelect(string payloadJson)
	{
		HideLoading();
		ShowSelectCreateVisible(false);

		ELIFE_JoinSelectPayload payload = new ELIFE_JoinSelectPayload();
		payload.ExpandFromRAW(payloadJson);

		if (m_SelectScreen)
		{
			m_SelectScreen.SetVisible(true);
			m_SelectScreen.Show(payload, GetController());
		}

		DebugDumpCreate("select"); // TODO: remove debug dump after the join UI visibility bug is fixed
	}

	//------------------------------------------------------------------------------------------------
	protected void ShowCreate(string payloadJson)
	{
		HideLoading();
		ShowSelectCreateVisible(false);

		ELIFE_JoinCreatePayload payload = new ELIFE_JoinCreatePayload();
		payload.ExpandFromRAW(payloadJson);

		if (m_CreateScreen)
		{
			m_CreateScreen.SetVisible(true);
			m_CreateScreen.Show(payload, GetController());
		}

		DebugDumpCreate("create"); // TODO: remove debug dump after the join UI visibility bug is fixed
	}

	//------------------------------------------------------------------------------------------------
	//! The link screen is not a rejection, so it never arms the client fallback (ApplyScreen) and the idle kick is server-side only.
	protected void ShowLink(string payloadJson)
	{
		//! A late push after Leave must not reopen the screen.
		if (m_bLeaving)
			return;

		HideLoading();
		ShowSelectCreateVisible(false);

		ELIFE_JoinLinkPayload payload = new ELIFE_JoinLinkPayload();
		payload.ExpandFromRAW(payloadJson);

		if (m_LinkScreen)
		{
			m_LinkScreen.SetVisible(true);
			m_LinkScreen.Show(payload, GetController());
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Leave on the link screen: AckClosed, then client leave, the same as the rejection dialogs.
	protected void OnLinkLeaveClicked()
	{
		LeaveJoin("link screen leave");
	}

	//------------------------------------------------------------------------------------------------
	// TODO: remove debug dump after the join UI visibility bug is fixed
	//! Prints the widget tree state of the join menu: direct children of the root, Content, and the select/create screen with its first two children.
	protected void DebugDumpCreate(string where)
	{
		if (!m_wJoinRoot)
			return;

		Print(string.Format("ELIFE_Join | DEBUG %1 root children of m_wJoinRoot:", where), LogLevel.NORMAL);
		Widget child = m_wJoinRoot.GetChildren();
		int index = 0;
		while (child)
		{
			DebugWidgetLine(where, string.Format("root[%1]", index), child);
			child = child.GetSibling();
			index++;
		}

		DebugWidgetLine(where, "content", m_wContent);

		Widget screenRoot = m_wCreateRoot;
		if (where == "select")
			screenRoot = m_wSelectRoot;

		DebugWidgetLine(where, "screen", screenRoot);
		if (screenRoot)
		{
			Widget screenChild = screenRoot.GetChildren();
			for (int i = 0; i < 2 && screenChild; i++)
			{
				DebugWidgetLine(where, string.Format("screen.child%1", i), screenChild);
				screenChild = screenChild.GetSibling();
			}
		}

		bool loadingAttached = m_wContent && m_wContent.FindHandler(SCR_LoadingOverlayHelperComponent) != null;
		Print(string.Format("ELIFE_Join | DEBUG %1 loadingOverlayAttachedToContent=%2", where, loadingAttached), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// TODO: remove debug dump after the join UI visibility bug is fixed
	protected void DebugWidgetLine(string where, string label, Widget w)
	{
		if (!w)
		{
			Print(string.Format("ELIFE_Join | DEBUG %1 %2 missing", where, label), LogLevel.NORMAL);
			return;
		}

		float posX;
		float posY;
		float sizeW;
		float sizeH;
		w.GetScreenPos(posX, posY);
		w.GetScreenSize(sizeW, sizeH);

		Print(string.Format("ELIFE_Join | DEBUG %1 %2 name=%3 type=%4 visible=%5 zOrder=%6 opacity=%7 color=%8",
			where, label, w.GetName(), w.GetTypeName(), w.IsVisible(), w.GetZOrder(), w.GetOpacity(), DebugColor(w)), LogLevel.NORMAL);
		Print(string.Format("ELIFE_Join | DEBUG pos=%1,%2 size=%3x%4 parentsVisible=%5",
			posX, posY, sizeW, sizeH, DebugParentsVisible(w)), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// TODO: remove debug dump after the join UI visibility bug is fixed
	protected string DebugColor(Widget w)
	{
		Color color = w.GetColor();
		if (!color)
			return "null";

		return string.Format("%1/%2/%3/%4", color.R(), color.G(), color.B(), color.A());
	}

	//------------------------------------------------------------------------------------------------
	// TODO: remove debug dump after the join UI visibility bug is fixed
	//! True when every ancestor of w, up to the join root, reports IsVisible.
	protected bool DebugParentsVisible(Widget w)
	{
		Widget parent = w.GetParent();
		while (parent)
		{
			if (!parent.IsVisible())
				return false;

			if (parent == m_wJoinRoot)
				break;

			parent = parent.GetParent();
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void ShowSelectCreateVisible(bool visible)
	{
		if (m_SelectScreen)
			m_SelectScreen.SetVisible(visible);

		if (m_CreateScreen)
			m_CreateScreen.SetVisible(visible);

		if (m_LinkScreen)
			m_LinkScreen.SetVisible(visible);
	}

	//------------------------------------------------------------------------------------------------
	//! Vanilla loading overlay over the content, with a localised text.
	protected void ShowLoading(string textKey)
	{
		ShowSelectCreateVisible(false);

		if (m_wContent)
			SCR_LoadingOverlay.ShowForWidget(m_wContent, WidgetManager.Translate(textKey));
	}

	//------------------------------------------------------------------------------------------------
	protected void HideLoading()
	{
		if (m_wContent)
			SCR_LoadingOverlay.HideForWidget(m_wContent);
	}

	//------------------------------------------------------------------------------------------------
	protected void ShowError(string payloadJson)
	{
		ELIFE_JoinErrorPayload payload = new ELIFE_JoinErrorPayload();
		payload.ExpandFromRAW(payloadJson);

		string textKey = "#ELIFE-Join_ErrorBackend";
		if (payload.messageKey.Length() > ERROR_KEY_PREFIX_LENGTH && payload.messageKey.Substring(0, ERROR_KEY_PREFIX_LENGTH) == ERROR_KEY_PREFIX)
			textKey = "#" + payload.messageKey;

		array<string> noFields = {};
		ShowRejected(ELIFE_EJoinScreen.ERROR, DIALOG_TAG_ERROR, textKey, noFields, noFields);
	}

	//------------------------------------------------------------------------------------------------
	protected void ShowBanned(string payloadJson)
	{
		ELIFE_JoinBanPayload payload = new ELIFE_JoinBanPayload();
		payload.ExpandFromRAW(payloadJson);

		array<string> keys = {"#ELIFE-Join_BannedReason", "#ELIFE-Join_BannedUntil", "#ELIFE-Join_BannedAppeal"};
		array<string> values = {payload.reason, payload.until, payload.appealUrl};
		ShowRejected(ELIFE_EJoinScreen.BANNED, DIALOG_TAG_BANNED, "#ELIFE-Join_BannedText", keys, values);
	}

	//------------------------------------------------------------------------------------------------
	protected void ShowWhitelist(string payloadJson)
	{
		ELIFE_JoinWhitelistPayload payload = new ELIFE_JoinWhitelistPayload();
		payload.ExpandFromRAW(payloadJson);

		array<string> keys = {"#ELIFE-Join_WhitelistUrl"};
		array<string> values = {payload.url};
		ShowRejected(ELIFE_EJoinScreen.NOT_WHITELISTED, DIALOG_TAG_WHITELIST, "#ELIFE-Join_WhitelistText", keys, values);
	}

	//------------------------------------------------------------------------------------------------
	//! Opens the vanilla configurable dialog of a rejected screen. The title comes from its preset; the message is the text key
	//! followed by each field that has a value. Confirm, cancel and close all leave (see OnDialogClosed).
	protected void ShowRejected(ELIFE_EJoinScreen screen, string presetTag, string textKey, array<string> fieldKeys, array<string> fieldValues)
	{
		//! Once the player has left, a late rejection opens nothing.
		if (m_bLeaving)
			return;

		HideLoading();
		ShowSelectCreateVisible(false);

		SCR_ConfigurableDialogUi dialog = SCR_ConfigurableDialogUi.CreateFromPreset(JOIN_DIALOGS_CONFIG, presetTag);
		if (!dialog)
		{
			Print(string.Format("ELIFE_Join | could not open the %1 dialog, leaving", presetTag), LogLevel.WARNING);
			LeaveJoin("dialog missing");
			return;
		}

		//! The dialog is the top menu right after it opens. Only a proxy is tracked; otherwise the vanished check stays off.
		MenuBase dialogMenu = GetGame().GetMenuManager().GetTopMenu();
		if (SCR_ConfigurableDialogUiProxy.Cast(dialogMenu))
			m_DialogMenu = dialogMenu;

		string message = WidgetManager.Translate(textKey);
		for (int i = 0; i < fieldKeys.Count() && i < fieldValues.Count(); i++)
		{
			if (fieldValues[i] != "")
				message += "\n" + WidgetManager.Translate(fieldKeys[i]) + ": " + fieldValues[i];
		}

		dialog.SetMessage(message);
		dialog.m_OnClose.Insert(OnDialogClosed);

		m_Dialog = dialog;
		m_eDialogScreen = screen;
	}

	//------------------------------------------------------------------------------------------------
	//! Closes the rejection dialog without leaving, for a new screen or when the menu closes.
	protected void CloseDialog()
	{
		if (!m_Dialog)
			return;

		SCR_ConfigurableDialogUi dialog = m_Dialog;
		m_Dialog = null;
		m_DialogMenu = null;
		dialog.m_OnClose.Remove(OnDialogClosed);
		dialog.Close();
	}

	//------------------------------------------------------------------------------------------------
	//! True when the dialog's menu has closed without Close() having run, e.g. Escape through the menu manager.
	protected bool IsDialogVanished()
	{
		return m_DialogMenu && !m_DialogMenu.IsOpen();
	}

	//------------------------------------------------------------------------------------------------
	//! A vanished dialog counts as closed by the player: its handler is removed, then the Leave runs.
	protected void OnDialogVanished()
	{
		m_Dialog.m_OnClose.Remove(OnDialogClosed);
		OnDialogClosed();
	}

	//------------------------------------------------------------------------------------------------
	//! Confirm, cancel or close of the rejection dialog. CloseDialog and OnDialogVanished remove this handler first, so only a user close reaches here.
	protected void OnDialogClosed()
	{
		m_Dialog = null;
		m_DialogMenu = null;
		LeaveJoin("rejection dialog closed");
	}

	//------------------------------------------------------------------------------------------------
	//! Sends AckClosed so the server can kick at once, then leaves gameplay. Runs at most once.
	protected void LeaveJoin(string reason)
	{
		if (m_bLeaving)
			return;

		m_bLeaving = true;

		ELIFE_PlayerController controller = GetController();
		if (controller)
			controller.ELIFE_AckClosed();

		ELIFE_JoinMenu.LeaveGameplay(reason);
	}

	//------------------------------------------------------------------------------------------------
	protected Widget CreateStretched(ResourceName layout, notnull Widget parent)
	{
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return null;

		Widget widget = workspace.CreateWidgets(layout, parent);
		if (!widget)
			return null;

		AlignableSlot.SetHorizontalAlign(widget, LayoutHorizontalAlign.Stretch);
		AlignableSlot.SetVerticalAlign(widget, LayoutVerticalAlign.Stretch);

		return widget;
	}
}
