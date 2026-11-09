//------------------------------------------------------------------------------------------------
//! A line from the NPC's voice table, by name. The server only says lines marked askable.
[BaseContainerProps(), SCR_BaseContainerCustomTitleField("m_sLine")]
class ELIFE_NpcDialogueLine : ELIFE_DialogueLine
{
	[Attribute("", UIWidgets.EditBox, "Name of the line in his voice table")]
	string m_sLine;

	//------------------------------------------------------------------------------------------------
	override void Speak(notnull ELIFE_DialogueController controller)
	{
		ELIFE_NpcDialogueController npc = ELIFE_NpcDialogueController.Cast(controller);
		if (npc)
			npc.RequestSay(m_sLine);
	}
}

//------------------------------------------------------------------------------------------------
//! A conversation with an ELIFE_NpcComponent. Ending it, however it happens, says goodbye and frees the NPC.
//! Enough for an NPC that only talks; one with its own actions subclasses it.
class ELIFE_NpcDialogueController : ELIFE_DialogueController
{
	//! The greeting can be heard just before the conversation opens; a line this recent is still shown.
	protected static const int RECENT_LINE_MS = 5000;

	protected ELIFE_NpcComponent m_Npc;
	protected ELIFE_NpcCharacterComponent m_Link;
	//! The conversation opens before the replicated customer id may have arrived, so a mismatch only counts once it matched.
	protected bool m_bSeenAsCustomer;

	//------------------------------------------------------------------------------------------------
	void ELIFE_NpcDialogueController(notnull ELIFE_NpcComponent npc, notnull ELIFE_NpcCharacterComponent link)
	{
		m_Npc = npc;
		m_Link = link;
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_NpcComponent GetNpc()
	{
		return m_Npc;
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_NpcCharacterComponent GetLink()
	{
		return m_Link;
	}

	//------------------------------------------------------------------------------------------------
	override ResourceName GetConfig()
	{
		if (!m_Npc)
			return ResourceName.Empty;

		return m_Npc.GetDialogueConfig();
	}

	//------------------------------------------------------------------------------------------------
	override IEntity GetSpeaker()
	{
		if (!m_Npc)
			return null;

		return m_Npc.GetOwner();
	}

	//------------------------------------------------------------------------------------------------
	override string GetSpeakerName()
	{
		if (!m_Npc)
			return "";

		return m_Npc.GetDisplayName();
	}

	//------------------------------------------------------------------------------------------------
	//! The server releases the NPC on its own when the customer walks off, dies or idles; the conversation follows.
	override bool IsAvailable()
	{
		if (!m_Npc || !m_Link || !m_Npc.IsUsableBy(SCR_PlayerController.GetLocalControlledEntity()))
			return false;

		if (m_Npc.GetCustomerPlayerId() == SCR_PlayerController.GetLocalPlayerId())
			m_bSeenAsCustomer = true;
		else if (m_bSeenAsCustomer)
			return false;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(notnull ELIFE_DialogueMenu menu)
	{
		super.OnStart(menu);

		string subtitle;
		if (m_Npc.GetRecentSubtitle(RECENT_LINE_MS, subtitle))
			menu.SetSubtitle(subtitle);
	}

	//------------------------------------------------------------------------------------------------
	override void OnEnd()
	{
		if (m_Link && m_Npc)
			m_Link.RequestClose(m_Npc.GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	void RequestSay(string lineName)
	{
		if (m_Link && m_Npc)
			m_Link.RequestSay(m_Npc.GetOwner(), lineName);
	}

	//------------------------------------------------------------------------------------------------
	//! Anything the NPC offers; see ELIFE_NpcComponent.OnRequest.
	void Request(int kind, int a = 0, int b = 0, string text = "")
	{
		if (m_Link && m_Npc)
			m_Link.Request(m_Npc.GetOwner(), kind, a, b, text);
	}
}
