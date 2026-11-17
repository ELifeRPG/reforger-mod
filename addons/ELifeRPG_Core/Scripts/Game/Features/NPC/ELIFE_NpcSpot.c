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

	[Attribute("", UIWidgets.EditBox, "Name of this place on the map (shop, branch), may be a string table key. Empty: the NPC's display name.")]
	protected string m_sMapName;

	protected bool m_bStandInHidden;

	//! Every spot in the world, on server and clients, so props near one find it without a link.
	protected static ref array<ELIFE_NpcSpot> s_aSpots = {};

	//! From the ELIFE_NpcComponent of the prefab this spot spawns, read once.
	protected bool m_bPrefabRead;
	protected ref ELIFE_NpcSchedule m_PrefabSchedule;
	protected string m_sPrefabDisplayName;
	protected ResourceName m_sPrefabMapIcon;
	protected vector m_vPrefabMapColor;

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

		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(RemoveBody);
	}

	//------------------------------------------------------------------------------------------------
	//! This place's opening hours: the spot's override, else the NPC prefab's. Null if he keeps no hours.
	//! Read from the prefab, so it also works on clients, where the spawned NPC doesn't carry them.
	ELIFE_NpcSchedule GetSchedule()
	{
		if (m_ScheduleOverride)
			return m_ScheduleOverride;

		ReadPrefab();
		return m_PrefabSchedule;
	}

	//------------------------------------------------------------------------------------------------
	//! The NPC's display name from his prefab (works on clients, where he may not be spawned).
	string GetDisplayName()
	{
		ReadPrefab();
		return m_sPrefabDisplayName;
	}

	//------------------------------------------------------------------------------------------------
	//! The place's own name (m_sMapName), may be empty or a string table key.
	string GetPlaceName()
	{
		return m_sMapName;
	}

	//------------------------------------------------------------------------------------------------
	//! This place's name on the map: m_sMapName, else the NPC's display name. May be a string table key.
	string GetMapName()
	{
		if (!m_sMapName.IsEmpty())
			return m_sMapName;

		return GetDisplayName();
	}

	//------------------------------------------------------------------------------------------------
	//! The NPC's map icon from his prefab, or empty.
	ResourceName GetMapIcon()
	{
		ReadPrefab();
		return m_sPrefabMapIcon;
	}

	//------------------------------------------------------------------------------------------------
	//! The NPC's map colour while open, sRGB; zero if he has none.
	vector GetMapColor()
	{
		ReadPrefab();
		return m_vPrefabMapColor;
	}

	//------------------------------------------------------------------------------------------------
	protected void ReadPrefab()
	{
		if (m_bPrefabRead)
			return;

		m_bPrefabRead = true;
		if (m_rnPrefab.IsEmpty())
			return;

		Resource resource = Resource.Load(m_rnPrefab);
		if (!resource || !resource.IsValid())
			return;

		IEntitySource source = resource.GetResource().ToEntitySource();
		if (!source)
			return;

		for (int i = 0, count = source.GetComponentCount(); i < count; i++)
		{
			IEntityComponentSource component = source.GetComponent(i);
			typename type = component.GetClassName().ToType();
			if (!type || !type.IsInherited(ELIFE_NpcComponent))
				continue;

			BaseContainer schedule = component.GetObject("m_Schedule");
			if (schedule)
				m_PrefabSchedule = ELIFE_NpcSchedule.Cast(BaseContainerTools.CreateInstanceFromContainer(schedule));

			component.Get("m_sDisplayName", m_sPrefabDisplayName);
			component.Get("m_sMapIcon", m_sPrefabMapIcon);
			component.Get("m_vMapColor", m_vPrefabMapColor);
			return;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The nearest spot within range whose NPC keeps opening hours, or null.
	static ELIFE_NpcSpot FindNearestWithSchedule(vector position, float range)
	{
		return FindNearest(position, range, true);
	}

	//------------------------------------------------------------------------------------------------
	//! The nearest spot within range that has its own place name (m_sMapName), or null.
	static ELIFE_NpcSpot FindNearestNamed(vector position, float range)
	{
		return FindNearest(position, range, false);
	}

	//------------------------------------------------------------------------------------------------
	protected static ELIFE_NpcSpot FindNearest(vector position, float range, bool withSchedule)
	{
		ELIFE_NpcSpot nearest;
		float nearestDistance = range * range;
		foreach (ELIFE_NpcSpot spot : s_aSpots)
		{
			if (!spot)
				continue;

			float distance = vector.DistanceSq(position, spot.GetOrigin());
			if (distance > nearestDistance)
				continue;

			if (withSchedule && !spot.GetSchedule())
				continue;

			if (!withSchedule && spot.GetPlaceName().IsEmpty())
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
	//! Children with a script component in their tree (e.g. an hours sign) are real props and stay visible.
	protected override void EOnFrame(IEntity owner, float timeSlice)
	{
		super.EOnFrame(owner, timeSlice);

		if (m_bStandInHidden || !GetGame().InPlayMode())
			return;

		m_bStandInHidden = true;
		IEntity part = GetChildren();
		while (part)
		{
			if (!HasScriptInTree(part))
				part.ClearFlags(EntityFlags.VISIBLE, false);

			part = part.GetSibling();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static bool HasScriptInTree(notnull IEntity entity)
	{
		if (entity.FindComponent(ScriptComponent))
			return true;

		IEntity child = entity.GetChildren();
		while (child)
		{
			if (HasScriptInTree(child))
				return true;

			child = child.GetSibling();
		}

		return false;
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

		SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(newEnt.FindComponent(SCR_CharacterControllerComponent));
		if (controller)
			controller.GetOnPlayerDeathWithParam().Insert(OnNpcDied);

		ApplyLook(newEnt);
		npc.SetVoice(GetVoiceIndex(npc.GetVoiceCount()));
		npc.SetSchedule(m_ScheduleOverride);
		OnNpcSpawned(npc);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. The NPC died: his body lies there for respawnDelay,
	//! then goes; once it's gone, the base spawner brings a new one after another respawnDelay.
	protected void OnNpcDied(SCR_CharacterControllerComponent controller, IEntity killerEntity, notnull Instigator killer)
	{
		GetGame().GetCallqueue().CallLater(RemoveBody, respawnDelay * 1000, false, controller.GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	protected void RemoveBody(IEntity body)
	{
		if (body)
			SCR_EntityHelper.DeleteEntityAndChildren(body);
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
