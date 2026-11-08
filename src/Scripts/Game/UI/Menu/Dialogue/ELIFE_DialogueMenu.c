//------------------------------------------------------------------------------------------------
//! Forwards clicks and hovers on the reply rows to the menu.
class ELIFE_DialogueReplyHandler : ScriptedWidgetEventHandler
{
	protected ELIFE_DialogueMenu m_Menu;
	protected int m_iIndex;

	//------------------------------------------------------------------------------------------------
	void ELIFE_DialogueReplyHandler(ELIFE_DialogueMenu menu, int index)
	{
		m_Menu = menu;
		m_iIndex = index;
	}

	//------------------------------------------------------------------------------------------------
	override bool OnClick(Widget w, int x, int y, int button)
	{
		if (m_Menu && button == 0)
			m_Menu.ChooseReply(m_iIndex);

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool OnMouseEnter(Widget w, int x, int y)
	{
		if (m_Menu)
			m_Menu.SelectReply(m_iIndex);

		return false;
	}
}

//------------------------------------------------------------------------------------------------
//! A conversation with an NPC: the speaker's name, their spoken line as a subtitle, the specifics under it,
//! and the player's numbered replies from an ELIFE_DialogueConfig. The world stays visible, so the NPC can be
//! seen talking. Everything NPC-specific goes through the ELIFE_DialogueController it is opened with.
//! Keys 1-9 or a click choose; pad up/down and A or Enter choose too; Esc ends it.
class ELIFE_DialogueMenu : ChimeraMenuBase
{
	protected static const ResourceName REPLY_LAYOUT = "{C0C6DE0680BEC911}UI/layouts/Menus/Dialogue/DialogueReply.layout";
	//! One digit key each.
	protected static const int MAX_REPLIES = 9;

	protected static ELIFE_DialogueMenu s_Open;
	protected static ref map<ResourceName, ref ELIFE_DialogueConfig> s_mConfigs = new map<ResourceName, ref ELIFE_DialogueConfig>();

	protected ref ELIFE_DialogueController m_Controller;
	protected ELIFE_DialogueConfig m_Config;

	protected TextWidget m_wSpeaker;
	protected RichTextWidget m_wSubtitle;
	protected RichTextWidget m_wDetails;
	protected Widget m_wReplies;
	protected ref array<Widget> m_aReplyFills = {};
	protected ref array<TextWidget> m_aReplyNumbers = {};
	protected ref array<RichTextWidget> m_aReplyTexts = {};
	protected ref array<ref ELIFE_DialogueReplyHandler> m_aHandlers = {};

	protected ELIFE_DialogueNode m_Node;
	protected array<ref ELIFE_DialogueReply> m_aReplies;
	//! What the node was opened with, for its details: an account number, a packed receipt.
	protected string m_sData;
	protected string m_sSubtitle;
	//! Under the details: "working…", or why something failed.
	protected string m_sNotice;
	protected string m_sShownSpeakerName;
	protected int m_iSelected = -1;

	protected bool m_bWaiting;
	//! Another menu on top (a form) has the input until Resume().
	protected bool m_bSuspended;
	protected bool m_bIsClosing;

	//! Input is polled; keys held when this menu gains focus press nothing.
	protected bool m_bKeysActive;
	protected ref array<bool> m_aDigitHeld = {false, false, false, false, false, false, false, false, false, false};
	protected bool m_bUpHeld;
	protected bool m_bDownHeld;
	protected bool m_bSelectHeld;
	protected bool m_bConfirmHeld;
	protected bool m_bBackHeld;

	//------------------------------------------------------------------------------------------------
	//! False if no menu opened. A menu that opens but can't set up ends itself, so the controller's OnEnd cleans up.
	static bool Open(notnull ELIFE_DialogueController controller)
	{
		MenuManager menuManager = GetGame().GetMenuManager();
		if (!menuManager)
			return false;

		ELIFE_DialogueMenu menu = ELIFE_DialogueMenu.Cast(menuManager.OpenMenu(ChimeraMenuPreset.ELIFE_DialogueMenu));
		if (!menu)
			return false;

		menu.Setup(controller);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	static ELIFE_DialogueMenu GetOpen()
	{
		return s_Open;
	}

	//------------------------------------------------------------------------------------------------
	//! For any NPC whose line is heard on this machine; only an open conversation with that speaker shows it.
	static void OnSpeakerSaid(IEntity speaker, string text)
	{
		if (s_Open && speaker && s_Open.m_Controller && s_Open.m_Controller.GetSpeaker() == speaker)
			s_Open.SetSubtitle(text);
	}

	//------------------------------------------------------------------------------------------------
	//! Loaded once per tree and kept.
	protected static ELIFE_DialogueConfig LoadConfig(ResourceName path)
	{
		ELIFE_DialogueConfig config = s_mConfigs.Get(path);
		if (config)
			return config;

		Resource resource = BaseContainerTools.LoadContainer(path);
		if (resource && resource.IsValid())
			config = ELIFE_DialogueConfig.Cast(BaseContainerTools.CreateInstanceFromContainer(resource.GetResource().ToBaseContainer()));

		if (!config)
		{
			Print(string.Format("ELIFE_DialogueMenu | dialogue config %1 failed to load", path), LogLevel.ERROR);
			return null;
		}

		s_mConfigs.Set(path, config);
		return config;
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuOpen()
	{
		super.OnMenuOpen();

		s_Open = this;
		Widget root = GetRootWidget();
		m_wSpeaker = TextWidget.Cast(root.FindAnyWidget("Speaker"));
		m_wSubtitle = RichTextWidget.Cast(root.FindAnyWidget("Subtitle"));
		m_wDetails = RichTextWidget.Cast(root.FindAnyWidget("Details"));
		m_wReplies = root.FindAnyWidget("Replies");
	}

	//------------------------------------------------------------------------------------------------
	//! Here, not in OnMenuOpen: the controller isn't known yet there.
	protected void Setup(ELIFE_DialogueController controller)
	{
		m_Controller = controller;
		m_Config = LoadConfig(controller.GetConfig());
		if (!m_Config || !m_Config.FindNode(m_Config.m_sStartNode))
		{
			Print("ELIFE_DialogueMenu | dialogue has no start node", LogLevel.ERROR);
			End();
			return;
		}

		controller.OnStart(this);
		ShowNode(m_Config.m_sStartNode);
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_DialogueController GetController()
	{
		return m_Controller;
	}

	//------------------------------------------------------------------------------------------------
	string GetNodeData()
	{
		return m_sData;
	}

	//------------------------------------------------------------------------------------------------
	//! data is what the node's details are about.
	void ShowNode(string id, string data = "")
	{
		if (m_bIsClosing)
			return;

		ELIFE_DialogueNode node = m_Config.FindNode(id);
		if (!node)
		{
			Print(string.Format("ELIFE_DialogueMenu | no dialogue node '%1'", id), LogLevel.ERROR);
			return;
		}

		m_Node = node;
		m_sData = data;
		m_sNotice = "";
		m_bWaiting = false;

		//! Cleared until the NPC's line is heard, so the old one doesn't sit over the new replies.
		if (node.m_Line)
		{
			m_sSubtitle = "";
			node.m_Line.Speak(m_Controller);
		}

		if (node.m_Details)
			node.m_Details.OnShown(this);

		BuildReplies();
		Refresh();
	}

	//------------------------------------------------------------------------------------------------
	void SetSubtitle(string text)
	{
		m_sSubtitle = text;
		Refresh();
	}

	//------------------------------------------------------------------------------------------------
	//! While waiting for an answer the replies are greyed out and can't be chosen.
	void SetWaiting(bool waiting, string notice = "")
	{
		m_bWaiting = waiting;
		m_sNotice = notice;
		Refresh();
	}

	//------------------------------------------------------------------------------------------------
	//! Another menu opens on top and takes the input.
	void Suspend()
	{
		m_bSuspended = true;
	}

	//------------------------------------------------------------------------------------------------
	void Resume()
	{
		m_bSuspended = false;
		m_bKeysActive = false;
	}

	//------------------------------------------------------------------------------------------------
	void End()
	{
		if (m_bIsClosing)
			return;

		m_bIsClosing = true;
		Close();
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildReplies()
	{
		m_aReplies = m_Config.GetReplies(m_Node);
		m_iSelected = -1;
		m_aReplyFills.Clear();
		m_aReplyNumbers.Clear();
		m_aReplyTexts.Clear();
		m_aHandlers.Clear();

		if (!m_wReplies)
			return;

		while (m_wReplies.GetChildren())
			m_wReplies.GetChildren().RemoveFromHierarchy();

		if (!m_aReplies)
			return;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		foreach (int i, ELIFE_DialogueReply reply : m_aReplies)
		{
			if (i >= MAX_REPLIES)
				break;

			Widget row = workspace.CreateWidgets(REPLY_LAYOUT, m_wReplies);
			if (!row)
				break;

			ELIFE_DialogueReplyHandler handler = new ELIFE_DialogueReplyHandler(this, i);
			m_aHandlers.Insert(handler);
			row.AddHandler(handler);

			m_aReplyFills.Insert(row.FindAnyWidget("Fill"));
			TextWidget number = TextWidget.Cast(row.FindAnyWidget("Number"));
			RichTextWidget text = RichTextWidget.Cast(row.FindAnyWidget("Text"));
			m_aReplyNumbers.Insert(number);
			m_aReplyTexts.Insert(text);

			if (number)
				number.SetText((i + 1).ToString() + ".");
			if (text)
				text.SetText(reply.m_sText);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void Refresh()
	{
		m_sShownSpeakerName = m_Controller.GetSpeakerName();
		if (m_wSpeaker)
			m_wSpeaker.SetText(m_sShownSpeakerName);

		if (m_wSubtitle)
		{
			m_wSubtitle.SetText(m_sSubtitle);
			m_wSubtitle.SetVisible(m_sSubtitle != "");
		}

		if (m_wDetails)
		{
			string details;
			if (m_Node && m_Node.m_Details)
				details = m_Node.m_Details.GetText(this);

			if (m_sNotice != "")
			{
				if (details != "")
					details += "\n";
				details += WidgetManager.Translate(m_sNotice);
			}

			m_wDetails.SetText(details);
			m_wDetails.SetVisible(details != "");
		}

		float inkAlpha = 1;
		if (m_bWaiting)
			inkAlpha = 0.4;

		foreach (int i, Widget fill : m_aReplyFills)
		{
			float fillAlpha = 0;
			if (i == m_iSelected && !m_bWaiting)
				fillAlpha = 0.12;

			if (fill)
				fill.SetColor(new Color(1, 1, 1, fillAlpha));

			if (m_aReplyNumbers[i])
				m_aReplyNumbers[i].SetOpacity(inkAlpha);
			if (m_aReplyTexts[i])
				m_aReplyTexts[i].SetOpacity(inkAlpha);
		}
	}

	//------------------------------------------------------------------------------------------------
	void SelectReply(int index)
	{
		if (index >= 0 && index < m_aReplyFills.Count() && index != m_iSelected)
		{
			m_iSelected = index;
			Refresh();
		}
	}

	//------------------------------------------------------------------------------------------------
	void ChooseReply(int index)
	{
		if (m_bWaiting || m_bSuspended || m_bIsClosing || !m_aReplies || index < 0 || index >= m_aReplyFills.Count())
			return;

		ELIFE_DialogueReply reply = m_aReplies[index];
		if (reply.m_Action)
			reply.m_Action.Run(this);
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuUpdate(float tDelta)
	{
		super.OnMenuUpdate(tDelta);

		if (m_bIsClosing)
			return;

		if (!m_Controller.IsAvailable())
		{
			End();
			return;
		}

		if (m_Controller.GetSpeakerName() != m_sShownSpeakerName)
			Refresh();

		PollKeys();
	}

	//------------------------------------------------------------------------------------------------
	protected void PollKeys()
	{
		if (!IsFocused() || m_bSuspended)
		{
			m_bKeysActive = false;
			return;
		}

		//! The first frame only records key state, so Esc that closed a menu on top doesn't also end this one.
		bool active = m_bKeysActive;
		m_bKeysActive = true;

		for (int digit = 1; digit <= MAX_REPLIES; digit++)
		{
			bool down = IsActionDown("ELIFE_KeyDigit" + digit.ToString());
			if (active && down && !m_aDigitHeld[digit])
				ChooseReply(digit - 1);

			m_aDigitHeld[digit] = down;
		}

		bool upDown = IsActionDown("ELIFE_PadUp");
		if (active && upDown && !m_bUpHeld)
			MoveSelection(-1);
		m_bUpHeld = upDown;

		bool downDown = IsActionDown("ELIFE_PadDown");
		if (active && downDown && !m_bDownHeld)
			MoveSelection(1);
		m_bDownHeld = downDown;

		bool selectDown = IsActionDown("ELIFE_PadSelect");
		if (active && selectDown && !m_bSelectHeld)
			ChooseReply(m_iSelected);
		m_bSelectHeld = selectDown;

		bool confirmDown = IsActionDown("ELIFE_KeyConfirm");
		if (active && confirmDown && !m_bConfirmHeld)
			ChooseReply(m_iSelected);
		m_bConfirmHeld = confirmDown;

		bool backDown = IsActionDown("MenuBack");
		if (active && backDown && !m_bBackHeld)
			End();
		m_bBackHeld = backDown;
	}

	//------------------------------------------------------------------------------------------------
	protected void MoveSelection(int step)
	{
		int count = m_aReplyFills.Count();
		if (count == 0)
			return;

		if (m_iSelected < 0)
			m_iSelected = 0;
		else
			m_iSelected = (m_iSelected + step + count) % count;

		Refresh();
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsActionDown(string action)
	{
		InputManager inputManager = GetGame().GetInputManager();
		return inputManager && inputManager.GetActionValue(action) > 0;
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuClose()
	{
		m_bIsClosing = true;
		if (s_Open == this)
			s_Open = null;

		if (m_Controller)
			m_Controller.OnEnd();

		super.OnMenuClose();
	}
}
