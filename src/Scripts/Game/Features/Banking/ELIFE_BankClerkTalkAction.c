//------------------------------------------------------------------------------------------------
//! Asks the server for the clerk's attention. The service dialog opens when the grant comes back.
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
		if (!m_Clerk || !m_Clerk.IsUsableBy(pUserEntity) || IsBusyWithOther())
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
		if (!m_Clerk || !m_Clerk.IsUsableBy(user))
			return false;

		if (IsBusyWithOther())
		{
			SetCannotPerformReason("#ELIFE-Clerk_Busy_Short");
			return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! The dialog only exists on the user's machine.
	override bool HasLocalEffectOnlyScript()
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsBusyWithOther()
	{
		int customer = m_Clerk.GetCustomerPlayerId();
		return customer != 0 && customer != SCR_PlayerController.GetLocalPlayerId();
	}
}
