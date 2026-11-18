//------------------------------------------------------------------------------------------------
//! The NPC side of one conversation in ELIFE_DialogueMenu. Each kind of NPC subclasses it: which tree to show,
//! who is speaking, when the conversation must end, and what its own lines and actions do.
class ELIFE_DialogueController
{
	protected ELIFE_DialogueMenu m_Menu;

	//------------------------------------------------------------------------------------------------
	//! The tree to show, an ELIFE_DialogueConfig.
	ResourceName GetConfig()
	{
		return ResourceName.Empty;
	}

	//------------------------------------------------------------------------------------------------
	//! Its lines become this conversation's subtitles (see ELIFE_DialogueMenu.OnSpeakerSaid).
	IEntity GetSpeaker()
	{
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Shown above the subtitle. Checked every frame, so a name that replicates late appears when it arrives.
	string GetSpeakerName()
	{
		return "";
	}

	//------------------------------------------------------------------------------------------------
	//! Checked every frame; false closes the conversation (out of range, dead, served someone else).
	bool IsAvailable()
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Before the start node is shown.
	void OnStart(notnull ELIFE_DialogueMenu menu)
	{
		m_Menu = menu;
	}

	//------------------------------------------------------------------------------------------------
	//! However the conversation ends: a reply, Esc, or IsAvailable() turning false.
	void OnEnd()
	{
	}
}
