//------------------------------------------------------------------------------------------------
//! One line an NPC can say, with what goes along with it.
[BaseContainerProps(), SCR_BaseContainerCustomTitleField("m_sName")]
class ELIFE_NpcVoiceLine
{
	[Attribute("", UIWidgets.EditBox, "Name of the line, e.g. GREETING. Part of its sound event and subtitle id")]
	string m_sName;

	[Attribute("1", UIWidgets.EditBox, "How many wordings were recorded: <name>, <name>_2, <name>_3 ...", "1 20 1")]
	int m_iVariants;

	[Attribute("0", UIWidgets.CheckBox, "The customer's dialogue may ask for it (answers, small talk). Outcomes and greetings are the server's to say")]
	bool m_bAskable;

	[Attribute("0", UIWidgets.EditBox, "Vanilla gesture id played shortly after the line starts (Configs/Commanding/Gestures); 0 is none")]
	int m_iGesture;

	[Attribute("", UIWidgets.EditBox, "Sound event of what he does while saying it, on his CharacterSoundComponent; empty is none")]
	string m_sSfx;

	[Attribute("0", UIWidgets.EditBox, "Milliseconds before the line may be heard again. Such a line is skipped while he is speaking (e.g. asking others to wait)", "0 600000 1")]
	int m_iRepeatCooldownMs;
}

//------------------------------------------------------------------------------------------------
//! What one kind of NPC can say. Sound event: <prefix><LINE>[_<variant>], or <prefix><VOICE>_<LINE>[_<variant>]
//! when the table lists voices; subtitle id the same with the subtitle prefix. Lines travel as their index here, so server and clients must load the same table.
[BaseContainerProps(configRoot: true)]
class ELIFE_NpcVoiceTable
{
	[Attribute("", UIWidgets.EditBox, "Start of every sound event, e.g. SOUND_ELIFE_CLERK_")]
	string m_sSoundEventPrefix;

	[Attribute("", UIWidgets.EditBox, "Start of every subtitle id in the string table, without #")]
	string m_sSubtitlePrefix;

	[Attribute(desc: "Optional voice names, each with its own sound events, for an NPC kind that may speak with different voices. Empty: one voice, events without a voice part")]
	ref array<string> m_aVoices;

	[Attribute("GREETING", UIWidgets.EditBox, "Said when a visit starts, unless the NPC picks its own greeting")]
	string m_sGreetingLine;

	[Attribute("GOODBYE", UIWidgets.EditBox, "Said when the customer ends the visit")]
	string m_sGoodbyeLine;

	[Attribute("BUSY", UIWidgets.EditBox, "Said to someone who asks while he serves another customer")]
	string m_sBusyLine;

	[Attribute("UNAVAILABLE", UIWidgets.EditBox, "Said when he can't serve at all")]
	string m_sUnavailableLine;

	[Attribute()]
	ref array<ref ELIFE_NpcVoiceLine> m_aLines;

	protected static ref map<ResourceName, ref ELIFE_NpcVoiceTable> s_mLoaded = new map<ResourceName, ref ELIFE_NpcVoiceTable>();

	//------------------------------------------------------------------------------------------------
	//! Loaded once per file and kept.
	static ELIFE_NpcVoiceTable Load(ResourceName path)
	{
		if (path.IsEmpty())
			return null;

		ELIFE_NpcVoiceTable table = s_mLoaded.Get(path);
		if (table)
			return table;

		Resource resource = BaseContainerTools.LoadContainer(path);
		if (resource && resource.IsValid())
			table = ELIFE_NpcVoiceTable.Cast(BaseContainerTools.CreateInstanceFromContainer(resource.GetResource().ToBaseContainer()));

		if (!table)
		{
			Print(string.Format("ELIFE_NpcVoiceTable | %1 failed to load", path), LogLevel.ERROR);
			return null;
		}

		s_mLoaded.Set(path, table);
		return table;
	}

	//------------------------------------------------------------------------------------------------
	//! -1 if there is no such line.
	int FindLine(string name)
	{
		if (name.IsEmpty() || !m_aLines)
			return -1;

		foreach (int i, ELIFE_NpcVoiceLine line : m_aLines)
		{
			if (line && line.m_sName == name)
				return i;
		}

		return -1;
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_NpcVoiceLine GetLine(int index)
	{
		if (!m_aLines || !m_aLines.IsIndexValid(index))
			return null;

		return m_aLines[index];
	}

	//------------------------------------------------------------------------------------------------
	int GetVoiceCount()
	{
		if (!m_aVoices)
			return 0;

		return m_aVoices.Count();
	}

	//------------------------------------------------------------------------------------------------
	string GetVoiceName(int voice)
	{
		if (!m_aVoices || !m_aVoices.IsIndexValid(voice))
			return "";

		return m_aVoices[voice];
	}

	//------------------------------------------------------------------------------------------------
	//! <prefix>[<VOICE>_]<LINE>[_<variant>]; empty if the voice or line is unknown.
	protected string BuildId(string prefix, int voice, int line, int variant)
	{
		ELIFE_NpcVoiceLine entry = GetLine(line);
		if (!entry)
			return "";

		string id = prefix;
		if (GetVoiceCount() > 0)
		{
			string voiceName = GetVoiceName(voice);
			if (voiceName.IsEmpty())
				return "";

			id += voiceName + "_";
		}

		id += entry.m_sName;
		if (variant > 1)
			id += "_" + variant.ToString();

		return id;
	}

	//------------------------------------------------------------------------------------------------
	string GetSoundEvent(int voice, int line, int variant)
	{
		return BuildId(m_sSoundEventPrefix, voice, line, variant);
	}

	//------------------------------------------------------------------------------------------------
	//! The string table key with #, ready for a text widget.
	string GetSubtitle(int voice, int line, int variant)
	{
		string id = BuildId(m_sSubtitlePrefix, voice, line, variant);
		if (id.IsEmpty())
			return "";

		return "#" + id;
	}
}
