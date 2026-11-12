//------------------------------------------------------------------------------------------------
//! Link screen: shown when the web account is not linked (webAccountLinked false on an active connect). Linking is optional: Continue without linking skips it.
//! The URL is never shown on screen: Copy link copies it, and Show QR-Code opens an overlay with its QR code. The QR code is a bearer credential,
//! so the overlay hides again on Hide QR-Code, after LINK_REVEAL_MS, when the screen is hidden, or when a new payload arrives. The code is masked
//! until hovered, and never logged. Check again asks the server to re-send PlayerConnected; Leave goes through the menu's Leave path.
//! The URL comes from the server, so the QR code is drawn here.
class ELIFE_JoinLinkScreen
{
	protected const int QR_QUIET_ZONE = 4;
	protected const int QR_DARK = 0xFF000000;
	protected const int QR_LIGHT = 0xFFFFFFFF;
	protected const int COPIED_SHOW_MS = 2000;
	protected const ResourceName LAYOUT_CODE_CELL = "{A484177F6D17CFFE}UI/layouts/Menus/Join/JoinLinkCodeCell.layout";
	protected const string ERROR_KEY_PREFIX = "ELIFE-Join_";
	//! Seconds Check again stays disabled after a press. The server enforces its own m_fRecheckCooldownSeconds (ELIFE_JoinSessionManagerComponent); keep the two equal.
	protected const int RECHECK_COOLDOWN_S = 30;
	protected const int RECHECK_TICK_MS = 1000;
	//! Milliseconds between checks of the QR canvas size while the overlay is open, so a window resize or resolution change redraws it.
	protected const int QR_SIZE_TICK_MS = 250;
	//! Seconds the QR code overlay stays open after Show QR-Code. It is a bearer credential, so it is not left on screen.
	protected const int LINK_REVEAL_MS = 60000;

	protected Widget m_wRoot;
	protected TextWidget m_wLinkText;
	//! Smaller line under the instruction. Its text is set once in Bind.
	protected TextWidget m_wLinkOptional;
	//! Holds one fixed-width cell per code character, built by BuildCodeCells.
	protected Widget m_wCodeCells;
	protected ref array<Widget> m_aCodeCells = {};
	protected Widget m_wCopiedUrl;
	protected Widget m_wCopiedCode;
	protected Widget m_wCodeButton;
	protected Widget m_wCopyCodeButton;
	//! Shown instead of the code cells when the server sent no code.
	protected TextWidget m_wCodeMissing;
	protected Widget m_wHintRow;
	protected TextWidget m_wHint;
	protected CanvasWidget m_wQr;
	//! Full-screen overlay (QrOverlay) with the QR code and Hide QR-Code. Hidden by default; Show QR-Code opens it.
	protected Widget m_wQrOverlay;
	protected SCR_ButtonTextComponent m_LeaveButton;
	protected SCR_ButtonTextComponent m_CheckButton;
	protected SCR_ButtonTextComponent m_SkipButton;
	protected SCR_ButtonTextComponent m_ShowLinkButton;
	protected SCR_ButtonTextComponent m_HideQrButton;

	//! Fired by Leave. The menu runs the Leave path (AckClosed, then client leave).
	protected ref ScriptInvoker m_OnLeave = new ScriptInvoker();
	protected ref array<ref ScriptedWidgetEventHandler> m_aHandlers = {};
	//! The draw commands must outlive SetDrawCommands, so they are kept here.
	protected ref array<ref CanvasWidgetCommand> m_aQrCommands = {};

	protected ELIFE_PlayerController m_Controller;
	protected bool m_bBusy;
	protected string m_sUrl;
	protected string m_sCode;
	//! The URL the QR code was last drawn for; the code is only redrawn when the URL changes.
	protected string m_sQrUrl;
	//! The encoded QR code of m_sQrUrl, kept so a resize only redraws and does not encode again. m_iQrSize is 0 when there is no code.
	protected int m_iQrSize;
	protected ref array<bool> m_aQrModules = {};
	//! Side in pixels of the last draw. -1 when nothing is drawn for the current URL, so the next draw always runs.
	protected float m_fQrDrawnSide = -1;
	protected bool m_bCodeRevealed;
	//! True while the QR code overlay is open (Show QR-Code). Hidden by default.
	protected bool m_bLinkRevealed;
	//! Seconds left before Check again can be pressed again; 0 when no cooldown runs.
	protected int m_iRecheckLeft;

	//------------------------------------------------------------------------------------------------
	void ~ELIFE_JoinLinkScreen()
	{
		if (!GetGame())
			return;

		GetGame().GetCallqueue().Remove(HideCopiedUrl);
		GetGame().GetCallqueue().Remove(HideCopiedCode);
		GetGame().GetCallqueue().Remove(RecheckTick);
		GetGame().GetCallqueue().Remove(OnLinkRevealTimeout);
		GetGame().GetCallqueue().Remove(RedrawQr);
		GetGame().GetCallqueue().Remove(QrSizeTick);
	}

	//------------------------------------------------------------------------------------------------
	//! Binds the widgets of an already created JoinLink layout.
	void Bind(Widget root)
	{
		m_wRoot = root;
		m_wLinkText = TextWidget.Cast(m_wRoot.FindAnyWidget("LinkText"));
		m_wLinkOptional = TextWidget.Cast(m_wRoot.FindAnyWidget("LinkOptional"));
		m_wCodeCells = m_wRoot.FindAnyWidget("LinkCodeCells");
		m_wCopiedUrl = m_wRoot.FindAnyWidget("CopiedUrl");
		m_wCopiedCode = m_wRoot.FindAnyWidget("CopiedCode");
		m_wCodeButton = m_wRoot.FindAnyWidget("LinkCodeButton");
		m_wCopyCodeButton = m_wRoot.FindAnyWidget("CopyCodeButton");
		m_wCodeMissing = TextWidget.Cast(m_wRoot.FindAnyWidget("LinkCodeMissing"));
		m_wHintRow = m_wRoot.FindAnyWidget("LinkHintRow");
		m_wHint = TextWidget.Cast(m_wRoot.FindAnyWidget("LinkHint"));
		m_wQr = CanvasWidget.Cast(m_wRoot.FindAnyWidget("QrCanvas"));
		m_wQrOverlay = m_wRoot.FindAnyWidget("QrOverlay");

		TextWidget title = TextWidget.Cast(m_wRoot.FindAnyWidget("LinkTitle"));
		if (title)
			title.SetText("#ELIFE-Join_LinkTitle");

		TextWidget codeLabel = TextWidget.Cast(m_wRoot.FindAnyWidget("LinkCodeLabel"));
		if (codeLabel)
			codeLabel.SetText("#ELIFE-Join_LinkCode");

		if (m_wLinkText)
		{
			m_wLinkText.SetTextWrapping(true);
			m_wLinkText.SetText("#ELIFE-Join_LinkText");
		}

		if (m_wLinkOptional)
		{
			m_wLinkOptional.SetTextWrapping(true);
			m_wLinkOptional.SetText("#ELIFE-Join_LinkOptional");
		}

		TextWidget copyLinkText = TextWidget.Cast(m_wRoot.FindAnyWidget("CopyLinkText"));
		if (copyLinkText)
			copyLinkText.SetText("#ELIFE-Join_LinkCopy");

		if (m_wHint)
			m_wHint.SetTextWrapping(true);

		if (m_wCodeMissing)
		{
			m_wCodeMissing.SetText("#ELIFE-Join_LinkCodeMissing");
			m_wCodeMissing.SetTextWrapping(true);
		}

		if (m_wCopiedUrl)
			SetTextOf(m_wCopiedUrl, "#ELIFE-Join_Copied");

		if (m_wCopiedCode)
			SetTextOf(m_wCopiedCode, "#ELIFE-Join_Copied");

		//! Copy link copies the URL. The code copies the raw code.
		AddClickHandler("CopyLinkButton", false);
		AddClickHandler("CopyCodeButton", true);

		//! Hovering the code text shows it spaced; leaving masks it again.
		Widget codeButton = m_wRoot.FindAnyWidget("LinkCodeButton");
		if (codeButton)
		{
			ELIFE_JoinLinkCodeHover hover = new ELIFE_JoinLinkCodeHover();
			hover.m_Screen = this;
			codeButton.AddHandler(hover);
			m_aHandlers.Insert(hover);
		}

		m_LeaveButton = SCR_ButtonTextComponent.GetButtonText("ButtonLeave", m_wRoot);
		if (m_LeaveButton)
		{
			m_LeaveButton.SetText("#ELIFE-Join_Leave");
			m_LeaveButton.m_OnClicked.Insert(OnLeaveClicked);
		}

		m_CheckButton = SCR_ButtonTextComponent.GetButtonText("ButtonCheck", m_wRoot);
		if (m_CheckButton)
		{
			m_CheckButton.SetText("#ELIFE-Join_CheckAgain");
			m_CheckButton.m_OnClicked.Insert(OnCheckClicked);
		}

		m_SkipButton = SCR_ButtonTextComponent.GetButtonText("ButtonSkip", m_wRoot);
		if (m_SkipButton)
		{
			m_SkipButton.SetText("#ELIFE-Join_LinkSkip");
			m_SkipButton.m_OnClicked.Insert(OnSkipClicked);
		}

		m_ShowLinkButton = SCR_ButtonTextComponent.GetButtonText("ButtonShowLink", m_wRoot);
		if (m_ShowLinkButton)
		{
			m_ShowLinkButton.SetText("#ELIFE-Join_LinkShowQr");
			m_ShowLinkButton.m_OnClicked.Insert(OnShowLinkClicked);
		}

		m_HideQrButton = SCR_ButtonTextComponent.GetButtonText("ButtonHideQr", m_wRoot);
		if (m_HideQrButton)
		{
			m_HideQrButton.SetText("#ELIFE-Join_LinkHideQr");
			m_HideQrButton.m_OnClicked.Insert(OnHideQrClicked);
		}

		UpdateSkipButton();

		// Hidden by default: the QR code is a bearer credential.
		RefreshLink();
	}

	//------------------------------------------------------------------------------------------------
	//! Fired by the Leave button. The menu subscribes to it.
	ScriptInvoker GetOnLeave()
	{
		return m_OnLeave;
	}

	//------------------------------------------------------------------------------------------------
	void SetVisible(bool visible)
	{
		if (!visible)
		{
			CancelCopiedLabels();
			// The QR code overlay does not stay up while the screen is away.
			SetLinkRevealed(false);
		}

		if (m_wRoot)
			m_wRoot.SetVisible(visible);
	}

	//------------------------------------------------------------------------------------------------
	//! Shows the payload: the masked code and the hint, and keeps the URL for Copy link and the QR overlay. The menu clears busy state before calling this.
	void Show(ELIFE_JoinLinkPayload payload, ELIFE_PlayerController controller)
	{
		if (!m_wRoot)
			return;

		m_Controller = controller;
		m_sUrl = payload.url;
		m_sCode = payload.code;
		m_bCodeRevealed = false;

		// A new payload closes the QR code overlay again; the player presses Show QR-Code for the new one.
		SetLinkRevealed(false);

		CancelCopiedLabels();

		if (payload.url != m_sQrUrl)
			RebuildQr(payload.url);

		BuildCodeCells();

		// TODO: remove link code debug logging
		string cellsContainer = "false";
		if (m_wCodeCells)
			cellsContainer = "true";

		Print(string.Format("ELIFE_Join | DEBUG link screen code length=%1 cells=%2 cellsContainer=%3", m_sCode.Length(), m_aCodeCells.Count(), cellsContainer), LogLevel.NORMAL);

		ShowCodeRow(m_sCode != "");
		RefreshCode();
		SetHint(GetHintText(payload.hint));
	}

	//------------------------------------------------------------------------------------------------
	//! Disables Check Again while a request is in flight or its cooldown runs. Leave stays enabled, so the player can always get out.
	//! Set by ELIFE_JoinMenu.SetBusy.
	void SetBusy(bool busy)
	{
		m_bBusy = busy;
		UpdateCheckButton();
		UpdateSkipButton();
	}

	//------------------------------------------------------------------------------------------------
	//! Called by the hover handler of the code text.
	void SetCodeRevealed(bool revealed)
	{
		if (m_bCodeRevealed == revealed)
			return;

		m_bCodeRevealed = revealed;
		RefreshCode();
	}

	//------------------------------------------------------------------------------------------------
	//! Shows or hides the QR code overlay. Showing starts the LINK_REVEAL_MS timer that hides it again; hiding cancels it.
	//! While shown, the QR code is redrawn at the canvas's pixel size; hiding stops that.
	protected void SetLinkRevealed(bool revealed)
	{
		GetGame().GetCallqueue().Remove(OnLinkRevealTimeout);
		GetGame().GetCallqueue().Remove(RedrawQr);
		GetGame().GetCallqueue().Remove(QrSizeTick);
		m_bLinkRevealed = revealed;

		if (revealed)
		{
			GetGame().GetCallqueue().CallLater(OnLinkRevealTimeout, LINK_REVEAL_MS, false);
			// The overlay was hidden, so the canvas has no size yet. Draw on the next frame, once the layout has run.
			GetGame().GetCallqueue().CallLater(RedrawQr, 0, false);
			// Then re-check the size, so a window resize or resolution change redraws the code.
			GetGame().GetCallqueue().CallLater(QrSizeTick, QR_SIZE_TICK_MS, true);
		}

		RefreshLink();
	}

	//------------------------------------------------------------------------------------------------
	//! Repeats while the overlay is open: redraws the QR code if the canvas size changed.
	protected void QrSizeTick()
	{
		RedrawQr();
	}

	//------------------------------------------------------------------------------------------------
	//! Closes the QR code overlay once LINK_REVEAL_MS has passed since Show QR-Code.
	protected void OnLinkRevealTimeout()
	{
		SetLinkRevealed(false);
	}

	//------------------------------------------------------------------------------------------------
	//! Called by the Show QR-Code button: opens the QR code overlay.
	protected void OnShowLinkClicked()
	{
		SetLinkRevealed(true);
	}

	//------------------------------------------------------------------------------------------------
	//! Called by the Hide QR-Code button: closes the QR code overlay.
	protected void OnHideQrClicked()
	{
		SetLinkRevealed(false);
	}

	//------------------------------------------------------------------------------------------------
	//! Shows the QR code overlay while revealed, and enables Show QR-Code only when there is a URL. The URL itself is never shown.
	//! The copy handlers read m_sUrl directly, so Copy link works whether or not the overlay is open.
	protected void RefreshLink()
	{
		if (m_wQrOverlay)
			m_wQrOverlay.SetVisible(m_bLinkRevealed);

		if (m_ShowLinkButton)
			m_ShowLinkButton.SetEnabled(m_sUrl != "");
	}

	//------------------------------------------------------------------------------------------------
	//! Called by the copy handlers. Copies the code or the URL and shows "Copied" for a moment.
	void OnCopyClicked(bool isCode)
	{
		string text = m_sUrl;
		if (isCode)
			text = m_sCode;

		if (text == "")
			return;

		System.ExportToClipboard(text);
		ShowCopied(isCode);
	}

	//------------------------------------------------------------------------------------------------
	//! Rebuilds one fixed-width cell per character of the code. Cells keep the same width whatever the character,
	//! which gives a monospace look with the vanilla font.
	protected void BuildCodeCells()
	{
		foreach (Widget oldCell : m_aCodeCells)
		{
			oldCell.RemoveFromHierarchy();
		}

		m_aCodeCells.Clear();

		if (!m_wCodeCells)
			return;

		for (int i = 0; i < m_sCode.Length(); i++)
		{
			Widget cell = GetGame().GetWorkspace().CreateWidgets(LAYOUT_CODE_CELL, m_wCodeCells);
			if (cell)
				m_aCodeCells.Insert(cell);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Shows each character in its cell while revealed, otherwise a star in every cell.
	protected void RefreshCode()
	{
		for (int i = 0; i < m_aCodeCells.Count(); i++)
		{
			TextWidget cellText = TextWidget.Cast(m_aCodeCells[i].FindAnyWidget("CellText"));
			if (!cellText)
				continue;

			if (m_bCodeRevealed)
				cellText.SetText(m_sCode.Substring(i, 1));
			else
				cellText.SetText("*");
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Shows the code cells and copy icon when there is a code, otherwise the missing-code text.
	protected void ShowCodeRow(bool hasCode)
	{
		if (m_wCodeButton)
			m_wCodeButton.SetVisible(hasCode);

		if (m_wCopyCodeButton)
			m_wCopyCodeButton.SetVisible(hasCode);

		if (m_wCodeMissing)
			m_wCodeMissing.SetVisible(!hasCode);
	}

	//------------------------------------------------------------------------------------------------
	//! Shows the hint in vanilla warning style; an empty key hides the row.
	protected void SetHint(string textKey)
	{
		if (m_wHint)
			m_wHint.SetText(textKey);

		if (m_wHintRow)
			m_wHintRow.SetVisible(textKey != "");
	}

	//------------------------------------------------------------------------------------------------
	//! Accepts a localisation key with or without the leading "#" and only with the ELIFE-Join_ prefix, like the other join screens.
	protected string GetHintText(string hint)
	{
		string key = hint;
		if (key.Length() > 0 && key.Substring(0, 1) == "#")
			key = key.Substring(1, key.Length() - 1);

		if (key.Length() > ERROR_KEY_PREFIX.Length() && key.Substring(0, ERROR_KEY_PREFIX.Length()) == ERROR_KEY_PREFIX)
			return "#" + key;

		return "";
	}

	//------------------------------------------------------------------------------------------------
	//! Encodes url once and caches the modules; then draws them at the canvas's current pixel size. An empty or unencodable URL leaves the canvas blank.
	protected void RebuildQr(string url)
	{
		m_sQrUrl = url;
		m_iQrSize = 0;
		m_fQrDrawnSide = -1;

		if (!m_wQr)
			return;

		// Drop the old drawing at once, so a previous URL is never left on the canvas.
		m_aQrCommands.Clear();
		m_wQr.SetDrawCommands(m_aQrCommands);

		if (url != "")
		{
			int size;
			array<bool> modules = {};
			if (ELIFE_QrCode.Encode(url, size, modules))
			{
				m_iQrSize = size;
				m_aQrModules = modules;
			}
			else
			{
				Print("ELIFE_Join | link URL does not fit a QR code, showing none", LogLevel.WARNING);
			}
		}

		RedrawQr();
	}

	//------------------------------------------------------------------------------------------------
	//! Draws the cached QR code at the canvas's pixel size. Dark modules are black, the quiet zone and background are white.
	//! The canvas is in pixels, so the code is drawn from the screen size: the layout box is scaled to the resolution, and fixed
	//! units would overflow or shrink it. Width and height are 0 until the layout has run, so nothing is drawn then; the tick tries again.
	protected void RedrawQr()
	{
		if (!m_wQr)
			return;

		float width;
		float height;
		m_wQr.GetScreenSize(width, height);
		float side = Math.Min(width, height);
		if (side <= 0)
			return;

		// Same size as the last draw: nothing to do. The 0.5 px tolerance absorbs layout rounding.
		float delta = side - m_fQrDrawnSide;
		if (delta > -0.5 && delta < 0.5)
			return;

		m_aQrCommands.Clear();
		if (m_iQrSize > 0)
			AddQrCommands(side, m_iQrSize, m_aQrModules);

		m_wQr.SetDrawCommands(m_aQrCommands);
		m_fQrDrawnSide = side;
	}

	//------------------------------------------------------------------------------------------------
	//! Adds the white background and one rectangle per horizontal run of dark modules, filling a square of side pixels.
	protected void AddQrCommands(float side, int size, array<bool> modules)
	{
		if (side <= 0)
			return;

		//! The module size is a float, so the code and its quiet zone fill the side exactly and no white is left over.
		//! No minimum module size: a very small side gives sub-pixel modules that blur, but the box is still filled exactly.
		float moduleSize = side / (size + 2 * QR_QUIET_ZONE);

		//! The quiet zone is QR_QUIET_ZONE modules on each side, so the code starts that far in.
		float origin = QR_QUIET_ZONE * moduleSize;

		AddRect(0, 0, side, side, QR_LIGHT);

		for (int y = 0; y < size; y++)
		{
			int x = 0;
			while (x < size)
			{
				if (!modules[y * size + x])
				{
					x++;
				}
				else
				{
					int runStart = x;
					while (x < size && modules[y * size + x])
					{
						x++;
					}

					AddRect(origin + runStart * moduleSize, origin + y * moduleSize, origin + x * moduleSize, origin + (y + 1) * moduleSize, QR_DARK);
				}
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void AddRect(float x0, float y0, float x1, float y1, int color)
	{
		PolygonDrawCommand command = new PolygonDrawCommand();
		command.m_iColor = color;

		array<float> vertices = {x0, y0, x1, y0, x1, y1, x0, y1};
		command.m_Vertices = vertices;

		m_aQrCommands.Insert(command);
	}

	//------------------------------------------------------------------------------------------------
	protected void AddClickHandler(string widgetName, bool isCode)
	{
		Widget w = m_wRoot.FindAnyWidget(widgetName);
		if (!w)
			return;

		ELIFE_JoinLinkClick click = new ELIFE_JoinLinkClick();
		click.m_Screen = this;
		click.m_bCode = isCode;
		w.AddHandler(click);
		m_aHandlers.Insert(click);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetTextOf(Widget w, string text)
	{
		TextWidget textWidget = TextWidget.Cast(w);
		if (textWidget)
			textWidget.SetText(text);
	}

	//------------------------------------------------------------------------------------------------
	//! Shows the "Copied" label next to the icon that was clicked, and hides it again after COPIED_SHOW_MS.
	protected void ShowCopied(bool isCode)
	{
		//! Only one "Copied" label is up at a time: the other one goes at once, and each label has its own timer.
		Widget label = m_wCopiedUrl;
		if (isCode)
		{
			GetGame().GetCallqueue().Remove(HideCopiedUrl);
			HideCopiedUrl();
			label = m_wCopiedCode;
		}
		else
		{
			GetGame().GetCallqueue().Remove(HideCopiedCode);
			HideCopiedCode();
		}

		if (!label)
			return;

		label.SetVisible(true);

		if (isCode)
		{
			GetGame().GetCallqueue().Remove(HideCopiedCode);
			GetGame().GetCallqueue().CallLater(HideCopiedCode, COPIED_SHOW_MS, false);
		}
		else
		{
			GetGame().GetCallqueue().Remove(HideCopiedUrl);
			GetGame().GetCallqueue().CallLater(HideCopiedUrl, COPIED_SHOW_MS, false);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void CancelCopiedLabels()
	{
		GetGame().GetCallqueue().Remove(HideCopiedUrl);
		GetGame().GetCallqueue().Remove(HideCopiedCode);

		if (m_wCopiedUrl)
			m_wCopiedUrl.SetVisible(false);

		if (m_wCopiedCode)
			m_wCopiedCode.SetVisible(false);
	}

	//------------------------------------------------------------------------------------------------
	protected void HideCopiedUrl()
	{
		if (m_wCopiedUrl)
			m_wCopiedUrl.SetVisible(false);
	}

	//------------------------------------------------------------------------------------------------
	protected void HideCopiedCode()
	{
		if (m_wCopiedCode)
			m_wCopiedCode.SetVisible(false);
	}

	//------------------------------------------------------------------------------------------------
	//! Asks the server to re-send PlayerConnected. Every push answers it, so the buttons come back then, but the cooldown keeps Check Again off until it ends.
	protected void OnCheckClicked()
	{
		if (m_bBusy || m_iRecheckLeft > 0 || !m_Controller)
			return;

		SetBusy(true);
		StartRecheckCooldown();
		m_Controller.ELIFE_RequestRecheck();
	}

	//------------------------------------------------------------------------------------------------
	//! Continues without linking. Mirrors Check again: busy while the request is in flight, no cooldown. Every push answers it and clears busy.
	protected void OnSkipClicked()
	{
		if (m_bBusy || !m_Controller)
			return;

		SetBusy(true);
		m_Controller.ELIFE_RequestSkipLink();
	}

	//------------------------------------------------------------------------------------------------
	//! Starts the countdown after a press: the label shows the seconds left and ticks down once a second.
	//! Not cancelled by SetVisible: ShowLink hides and shows the screen on every push, and that must not re-enable the button.
	protected void StartRecheckCooldown()
	{
		m_iRecheckLeft = RECHECK_COOLDOWN_S;
		GetGame().GetCallqueue().Remove(RecheckTick);
		GetGame().GetCallqueue().CallLater(RecheckTick, RECHECK_TICK_MS, true);
		UpdateCheckButton();
	}

	//------------------------------------------------------------------------------------------------
	//! Once a second while the cooldown runs. At 0 the timer stops and the label is plain again.
	protected void RecheckTick()
	{
		m_iRecheckLeft--;
		if (m_iRecheckLeft <= 0)
		{
			m_iRecheckLeft = 0;
			GetGame().GetCallqueue().Remove(RecheckTick);
		}

		UpdateCheckButton();
	}

	//------------------------------------------------------------------------------------------------
	//! Shows "Check again (N)" while the cooldown runs. The button is enabled only when neither busy nor cooling down.
	protected void UpdateCheckButton()
	{
		if (!m_CheckButton)
			return;

		string label = WidgetManager.Translate("#ELIFE-Join_CheckAgain");
		if (m_iRecheckLeft > 0)
			label = string.Format("%1 (%2)", label, m_iRecheckLeft);

		m_CheckButton.SetText(label);
		m_CheckButton.SetEnabled(!m_bBusy && m_iRecheckLeft == 0);
	}

	//------------------------------------------------------------------------------------------------
	//! Disabled while a request is in flight, the same as Check again.
	protected void UpdateSkipButton()
	{
		if (m_SkipButton)
			m_SkipButton.SetEnabled(!m_bBusy);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnLeaveClicked()
	{
		m_OnLeave.Invoke();
	}
}

//------------------------------------------------------------------------------------------------
//! Copies the URL or the code when its widget is clicked.
class ELIFE_JoinLinkClick : ScriptedWidgetEventHandler
{
	ELIFE_JoinLinkScreen m_Screen;
	bool m_bCode;

	//------------------------------------------------------------------------------------------------
	override bool OnClick(Widget w, int x, int y, int button)
	{
		if (m_Screen)
			m_Screen.OnCopyClicked(m_bCode);

		return false;
	}
}

//------------------------------------------------------------------------------------------------
//! Reveals the code while the cursor is on its text.
class ELIFE_JoinLinkCodeHover : ScriptedWidgetEventHandler
{
	ELIFE_JoinLinkScreen m_Screen;

	//------------------------------------------------------------------------------------------------
	override bool OnMouseEnter(Widget w, int x, int y)
	{
		if (m_Screen)
			m_Screen.SetCodeRevealed(true);

		return false;
	}

	//------------------------------------------------------------------------------------------------
	override bool OnMouseLeave(Widget w, Widget enterW, int x, int y)
	{
		if (m_Screen)
			m_Screen.SetCodeRevealed(false);

		return false;
	}
}
