//------------------------------------------------------------------------------------------------
//! Asks the server for the NPC's attention. The conversation opens when the grant comes back.
//! Also while he serves someone else: then he asks the player to wait, and the refusal shows as a hint.
class ELIFE_NpcTalkAction : ScriptedUserAction
{
	protected ELIFE_NpcComponent m_Npc;

	//------------------------------------------------------------------------------------------------
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		m_Npc = ELIFE_NpcComponent.Cast(pOwnerEntity.FindComponent(ELIFE_NpcComponent));
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!m_Npc || !m_Npc.IsUsableBy(pUserEntity))
			return;

		ELIFE_NpcCharacterComponent link = ELIFE_NpcCharacterComponent.Cast(pUserEntity.FindComponent(ELIFE_NpcCharacterComponent));
		if (link)
			link.RequestOpen(pOwnerEntity);
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		return m_Npc && m_Npc.IsUsableBy(user);
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		return m_Npc && m_Npc.IsUsableBy(user);
	}

	//------------------------------------------------------------------------------------------------
	//! The conversation only exists on the user's machine.
	override bool HasLocalEffectOnlyScript()
	{
		return true;
	}
}
