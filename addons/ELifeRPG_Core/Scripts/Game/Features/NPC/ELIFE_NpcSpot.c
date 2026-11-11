//------------------------------------------------------------------------------------------------
//! Optional on a spot: for an NPC kind placed in many locations (like the bank clerk), each spot picks a look and
//! a voice from its position, so a place keeps its NPC across restarts while different places get different people.
//! The look only swaps head and body; the prefab keeps its clothing.
[BaseContainerProps()]
class ELIFE_NpcSpotVariation
{
	[Attribute("{8A428AA8D35D5CAF}Configs/NPC/ELIFE_NpcLooks_Civilian.conf", UIWidgets.ResourceNamePicker, "Looks to pick from, an ELIFE_NpcLookList. Empty keeps the prefab's look.", "conf")]
	ResourceName m_sLooks;

	[Attribute(desc: "Only these looks (by name, e.g. White_04). Empty uses all. Give two NPC kinds separate sets so they never look alike.")]
	ref array<string> m_aLookNames;

	[Attribute("1", UIWidgets.CheckBox, "Pick one of the voices in his voice table, independently of the look. Off uses the spot's voice.")]
	bool m_bVaryVoice;
}

//------------------------------------------------------------------------------------------------
class ELIFE_NpcSpotClass : ELIFE_PrefabSpawnClass
{
}

//------------------------------------------------------------------------------------------------
//! Where an NPC stands. A character placed in the world isn't registered for replication, so the server spawns him here instead.
//! By default it spawns its prefab as it is, with one voice; m_Variation makes look and voice depend on the position.
//! A kind of NPC may subclass it to hand its own settings over in OnNpcSpawned.
class ELIFE_NpcSpot : ELIFE_PrefabSpawn
{
	[Attribute("0", UIWidgets.EditBox, "Which voice of his voice table he speaks with, from 0, unless the variation picks one.")]
	protected int m_iVoice;

	[Attribute(desc: "Optional: vary look and voice by position. Leave empty for an NPC with a fixed look and voice.")]
	protected ref ELIFE_NpcSpotVariation m_Variation;

	[Attribute(desc: "Optional: opening hours for this place, instead of the NPC prefab's.")]
	protected ref ELIFE_NpcSchedule m_ScheduleOverride;

	protected bool m_bStandInHidden;

	//! Every spot in the world, on server and clients, so props near one find it without a link.
	protected static ref array<ELIFE_NpcSpot> s_aSpots = {};

	protected ref ELIFE_NpcSchedule m_PrefabSchedule;
	protected bool m_bPrefabScheduleRead;

	//------------------------------------------------------------------------------------------------
	void ELIFE_NpcSpot(IEntitySource src, IEntity parent)
	{
		s_aSpots.Insert(this);
	}

	//------------------------------------------------------------------------------------------------
	void ~ELIFE_NpcSpot()
	{
		if (s_aSpots)
			s_aSpots.RemoveItem(this);
	}

	//------------------------------------------------------------------------------------------------
	//! This place's opening hours: the spot's override, else the NPC prefab's. Null if he keeps no hours.
	//! Read from the prefab, so it also works on clients, where the spawned NPC doesn't carry them.
	ELIFE_NpcSchedule GetSchedule()
	{
		if (m_ScheduleOverride)
			return m_ScheduleOverride;

		if (!m_bPrefabScheduleRead)
		{
			m_bPrefabScheduleRead = true;
			m_PrefabSchedule = ELIFE_NpcSchedule.FromPrefab(m_rnPrefab);
		}

		return m_PrefabSchedule;
	}

	//------------------------------------------------------------------------------------------------
	//! The nearest spot within range whose NPC keeps opening hours, or null.
	static ELIFE_NpcSpot FindNearestWithSchedule(vector position, float range)
	{
		ELIFE_NpcSpot nearest;
		float nearestDistance = range * range;
		foreach (ELIFE_NpcSpot spot : s_aSpots)
		{
			if (!spot)
				continue;

			float distance = vector.DistanceSq(position, spot.GetOrigin());
			if (distance > nearestDistance || !spot.GetSchedule())
				continue;

			nearest = spot;
			nearestDistance = distance;
		}

		return nearest;
	}

	//------------------------------------------------------------------------------------------------
	//! Stable for a placed spot: the same position always gives the same pick. Look and voice use different
	//! factors, so any look can get any voice.
	protected int PickByPosition(int count, int factorX, int factorZ)
	{
		if (count <= 0)
			return 0;

		vector position = GetOrigin();
		int x = Math.Round(position[0] * 10);
		int z = Math.Round(position[2] * 10);
		int hash = (x * factorX) ^ (z * factorZ);
		return Math.AbsInt(hash) % count;
	}

	//------------------------------------------------------------------------------------------------
	protected int GetVoiceIndex(int count)
	{
		if (m_Variation && m_Variation.m_bVaryVoice)
			return PickByPosition(count, 49979687, 86028121);

		if (count <= 0)
			return 0;

		return Math.Clamp(m_iVoice, 0, count - 1);
	}

	//------------------------------------------------------------------------------------------------
	//! A stand-in (child entities) may mark the spot in the editor. Its parts aren't attached yet at init, so it's hidden on the first frame.
	protected override void EOnFrame(IEntity owner, float timeSlice)
	{
		super.EOnFrame(owner, timeSlice);

		if (m_bStandInHidden || !GetGame().InPlayMode())
			return;

		m_bStandInHidden = true;
		IEntity part = GetChildren();
		while (part)
		{
			part.ClearFlags(EntityFlags.VISIBLE, false);
			part = part.GetSibling();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected override void OnSpawn(IEntity newEnt)
	{
		super.OnSpawn(newEnt);

		ELIFE_NpcComponent npc = ELIFE_NpcComponent.Cast(newEnt.FindComponent(ELIFE_NpcComponent));
		if (!npc)
		{
			Print(string.Format("%1 | %2 has no ELIFE_NpcComponent", ClassName(), m_rnPrefab), LogLevel.ERROR);
			return;
		}

		ApplyLook(newEnt);
		npc.SetVoice(GetVoiceIndex(npc.GetVoiceCount()));
		npc.SetSchedule(m_ScheduleOverride);
		OnNpcSpawned(npc);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. The variation's look for this position, if it has looks.
	protected void ApplyLook(notnull IEntity npc)
	{
		if (!m_Variation || m_Variation.m_sLooks.IsEmpty())
			return;

		ELIFE_NpcLookList list = ELIFE_NpcLookList.Load(m_Variation.m_sLooks);
		if (!list)
			return;

		array<ELIFE_NpcLook> looks = {};
		list.Collect(m_Variation.m_aLookNames, looks);
		if (looks.IsEmpty())
			return;

		ELIFE_NpcLook look = looks[PickByPosition(looks.Count(), 73856093, 19349663)];
		if (!ELIFE_NpcLookList.Apply(npc, look))
			Print(string.Format("%1 | couldn't give %2 the look %3", ClassName(), m_rnPrefab, look.m_sName), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only, right after spawning, before the first customer.
	protected void OnNpcSpawned(notnull ELIFE_NpcComponent npc)
	{
	}
}
