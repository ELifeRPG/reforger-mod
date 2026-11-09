//------------------------------------------------------------------------------------------------
class ELIFE_BankClerkSpotClass : ELIFE_NpcSpotClass
{
}

//------------------------------------------------------------------------------------------------
//! Where a clerk stands, and the bank his desk belongs to.
class ELIFE_BankClerkSpot : ELIFE_NpcSpot
{
	[Attribute("FEBK", UIWidgets.EditBox, "Code of the bank this desk belongs to (the four letters in its account numbers)")]
	protected string m_sBankCode;

	//------------------------------------------------------------------------------------------------
	override protected void OnNpcSpawned(notnull ELIFE_NpcComponent npc)
	{
		ELIFE_BankClerkComponent clerk = ELIFE_BankClerkComponent.Cast(npc);
		if (clerk)
			clerk.SetBankCode(m_sBankCode);
		else
			Print(string.Format("ELIFE_BankClerkSpot | %1 has no ELIFE_BankClerkComponent", m_rnPrefab), LogLevel.ERROR);
	}
}
