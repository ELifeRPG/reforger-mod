//------------------------------------------------------------------------------------------------
//! A conversation tree for any NPC, shown by ELIFE_DialogueMenu. The tree itself is generic; what an NPC says,
//! does and shows comes from subclasses of ELIFE_DialogueLine, ELIFE_DialogueAction and ELIFE_DialogueDetails,
//! which reach the NPC through the menu's ELIFE_DialogueController.

//------------------------------------------------------------------------------------------------
//! What the NPC says on arriving at a node. Subclasses carry the NPC's own line ids and ask its controller to say them.
[BaseContainerProps()]
class ELIFE_DialogueLine
{
	//------------------------------------------------------------------------------------------------
	void Speak(notnull ELIFE_DialogueController controller)
	{
	}
}

//------------------------------------------------------------------------------------------------
//! What choosing a reply does.
[BaseContainerProps()]
class ELIFE_DialogueAction
{
	//------------------------------------------------------------------------------------------------
	void Run(notnull ELIFE_DialogueMenu menu)
	{
	}
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), SCR_BaseContainerCustomTitleField("m_sTarget")]
class ELIFE_DialogueAction_GoTo : ELIFE_DialogueAction
{
	[Attribute("", UIWidgets.EditBox, "The node to move to")]
	string m_sTarget;

	//------------------------------------------------------------------------------------------------
	override void Run(notnull ELIFE_DialogueMenu menu)
	{
		menu.ShowNode(m_sTarget);
	}
}

//------------------------------------------------------------------------------------------------
//! Ends the conversation, as leaving with Esc does.
[BaseContainerProps()]
class ELIFE_DialogueAction_End : ELIFE_DialogueAction
{
	//------------------------------------------------------------------------------------------------
	override void Run(notnull ELIFE_DialogueMenu menu)
	{
		menu.End();
	}
}

//------------------------------------------------------------------------------------------------
//! The specifics shown under the NPC's line: the spoken line stays generic, these carry names and figures.
[BaseContainerProps()]
class ELIFE_DialogueDetails
{
	//------------------------------------------------------------------------------------------------
	//! Text with one fact per line; "" shows nothing. menu.GetNodeData() holds what the node was opened with.
	string GetText(notnull ELIFE_DialogueMenu menu)
	{
		return "";
	}

	//------------------------------------------------------------------------------------------------
	//! Once, when its node is shown: a sound, for instance.
	void OnShown(notnull ELIFE_DialogueMenu menu)
	{
	}

	//------------------------------------------------------------------------------------------------
	//! "Label: value".
	static string FormatLine(string labelKey, string value)
	{
		return WidgetManager.Translate(labelKey) + ": " + value;
	}
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), SCR_BaseContainerCustomTitleField("m_sText")]
class ELIFE_DialogueReply
{
	[Attribute("", UIWidgets.LocaleEditBox, "What the player says")]
	LocalizedString m_sText;

	[Attribute()]
	ref ELIFE_DialogueAction m_Action;
}

//------------------------------------------------------------------------------------------------
//! One step of the conversation: what the NPC says on arrival, the specifics under it and the player's replies.
[BaseContainerProps(), SCR_BaseContainerCustomTitleField("m_sId")]
class ELIFE_DialogueNode
{
	[Attribute("", UIWidgets.EditBox, "Unique name; actions and the start node refer to it")]
	string m_sId;

	[Attribute(desc: "Said on arrival. Leave empty where the NPC already speaks (greeting, outcomes)")]
	ref ELIFE_DialogueLine m_Line;

	[Attribute()]
	ref ELIFE_DialogueDetails m_Details;

	[Attribute("", UIWidgets.EditBox, "Use another node's replies; only when this node has none of its own")]
	string m_sRepliesOf;

	[Attribute()]
	ref array<ref ELIFE_DialogueReply> m_aReplies;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(configRoot: true)]
class ELIFE_DialogueConfig
{
	[Attribute("", UIWidgets.EditBox, "The node the conversation opens at")]
	string m_sStartNode;

	[Attribute()]
	ref array<ref ELIFE_DialogueNode> m_aNodes;

	//------------------------------------------------------------------------------------------------
	ELIFE_DialogueNode FindNode(string id)
	{
		if (!m_aNodes)
			return null;

		foreach (ELIFE_DialogueNode node : m_aNodes)
		{
			if (node && node.m_sId == id)
				return node;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! The node's own replies, else the ones it borrows. One level only, so a loop in the config can't hang.
	array<ref ELIFE_DialogueReply> GetReplies(notnull ELIFE_DialogueNode node)
	{
		if (node.m_aReplies && !node.m_aReplies.IsEmpty())
			return node.m_aReplies;

		ELIFE_DialogueNode source = FindNode(node.m_sRepliesOf);
		if (source && source.m_aReplies)
			return source.m_aReplies;

		return null;
	}
}
