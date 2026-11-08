//------------------------------------------------------------------------------------------------
class ELIFE_BankClerkSpotClass : ELIFE_PrefabSpawnClass
{
}

//------------------------------------------------------------------------------------------------
//! Where a clerk stands. A character placed in the world isn't registered for replication, so the server spawns him here instead.
class ELIFE_BankClerkSpot : ELIFE_PrefabSpawn
{
	[Attribute("FEBK", UIWidgets.EditBox, "Code of the bank this desk belongs to (the four letters in its account numbers)")]
	protected string m_sBankCode;

	[Attribute(desc: "Clerks that differ only in their face. Each spot picks one from its position, so a branch keeps its clerk across restarts while different branches get different people.", params: "et")]
	protected ref array<ResourceName> m_aClerkPrefabs;

	[Attribute("-1", UIWidgets.EditBox, "Which of the clerk prefabs to use. -1 picks one from the spot's position.")]
	protected int m_iClerkVariant;

	[Attribute("-1", UIWidgets.EditBox, "Which voice the clerk speaks with (see ELIFE_EClerkVoice: 0 Puck, 1 Lewis, 2 Michael). -1 picks one from the spot's position, independently of the face.")]
	protected int m_iClerkVoice;

	protected bool m_bStandInHidden;

	//------------------------------------------------------------------------------------------------
	protected override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);

		if (m_aClerkPrefabs && !m_aClerkPrefabs.IsEmpty())
			m_rnPrefab = m_aClerkPrefabs[GetVariantIndex()];
	}

	//------------------------------------------------------------------------------------------------
	//! Stable for a placed spot: the same position always gives the same face.
	protected int GetVariantIndex()
	{
		int count = m_aClerkPrefabs.Count();
		if (m_iClerkVariant >= 0)
			return Math.Min(m_iClerkVariant, count - 1);

		vector position = GetOrigin();
		int x = Math.Round(position[0] * 10);
		int z = Math.Round(position[2] * 10);
		int hash = (x * 73856093) ^ (z * 19349663);
		return Math.AbsInt(hash) % count;
	}

	//------------------------------------------------------------------------------------------------
	//! Different hash from the face, so any face can get any voice.
	protected ELIFE_EClerkVoice GetVoice()
	{
		int count = ELIFE_EClerkVoice.COUNT;
		if (m_iClerkVoice >= 0)
			return Math.Min(m_iClerkVoice, count - 1);

		vector position = GetOrigin();
		int x = Math.Round(position[0] * 10);
		int z = Math.Round(position[2] * 10);
		int hash = (x * 49979687) ^ (z * 86028121);
		return Math.AbsInt(hash) % count;
	}

	//------------------------------------------------------------------------------------------------
	//! The stand-in (black suit, boots and head) only marks the spot in the editor. Its parts aren't attached yet at init, so it's hidden on the first frame.
	protected override void EOnFrame(IEntity owner, float timeSlice)
	{
		super.EOnFrame(owner, timeSlice);

		if (m_bStandInHidden || !GetGame().InPlayMode())
			return;

		m_bStandInHidden = true;
		int hidden;
		IEntity part = GetChildren();
		while (part)
		{
			part.ClearFlags(EntityFlags.VISIBLE, false);
			part = part.GetSibling();
			hidden++;
		}

		if (hidden == 0)
			Print("ELIFE_BankClerkSpot | no stand-in parts found to hide", LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	protected override void OnSpawn(IEntity newEnt)
	{
		super.OnSpawn(newEnt);

		ELIFE_BankClerkComponent clerk = ELIFE_BankClerkComponent.Cast(newEnt.FindComponent(ELIFE_BankClerkComponent));
		if (clerk)
		{
			clerk.SetBankCode(m_sBankCode);
			clerk.SetVoice(GetVoice());
		}
		else
			Print(string.Format("ELIFE_BankClerkSpot | %1 has no ELIFE_BankClerkComponent", m_rnPrefab), LogLevel.ERROR);
	}
}
