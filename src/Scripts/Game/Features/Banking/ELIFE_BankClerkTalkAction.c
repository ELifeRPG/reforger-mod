//------------------------------------------------------------------------------------------------
//! Asks the server for the clerk's attention. The service dialog opens when the grant comes back.
//! Also while he serves someone else: then he asks the player to wait, and the refusal shows as a hint.
class ELIFE_BankClerkTalkAction : ScriptedUserAction
{
	protected ELIFE_BankClerkComponent m_Clerk;

	//------------------------------------------------------------------------------------------------
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		m_Clerk = ELIFE_BankClerkComponent.Cast(pOwnerEntity.FindComponent(ELIFE_BankClerkComponent));
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!m_Clerk || !m_Clerk.IsUsableBy(pUserEntity))
			return;

		ELIFE_BankClerkCharacterComponent link = ELIFE_BankClerkCharacterComponent.Cast(pUserEntity.FindComponent(ELIFE_BankClerkCharacterComponent));
		if (link)
			link.RequestOpen(pOwnerEntity);
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		return m_Clerk && m_Clerk.IsUsableBy(user);
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		return m_Clerk && m_Clerk.IsUsableBy(user);
	}

	//------------------------------------------------------------------------------------------------
	//! The dialog only exists on the user's machine.
	override bool HasLocalEffectOnlyScript()
	{
		return true;
	}
}
