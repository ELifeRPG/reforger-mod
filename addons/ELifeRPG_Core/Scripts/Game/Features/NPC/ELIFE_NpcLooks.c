//------------------------------------------------------------------------------------------------
//! One look an NPC can get: a head and the body that matches it (skin and hands). Clothing stays the prefab's.
[BaseContainerProps(), SCR_BaseContainerCustomTitleField("m_sName")]
class ELIFE_NpcLook
{
	[Attribute("", UIWidgets.EditBox, "Name a spot's variation can pick it by, e.g. White_04")]
	string m_sName;

	[Attribute("", UIWidgets.ResourceNamePicker, "Head prefab", "et")]
	ResourceName m_sHead;

	[Attribute("", UIWidgets.ResourceNamePicker, "Body prefab matching the head's skin", "et")]
	ResourceName m_sBody;
}

//------------------------------------------------------------------------------------------------
//! Looks for NPCs whose spot varies them (ELIFE_NpcSpotVariation).
[BaseContainerProps(configRoot: true)]
class ELIFE_NpcLookList
{
	[Attribute()]
	ref array<ref ELIFE_NpcLook> m_aLooks;

	protected static ref map<ResourceName, ref ELIFE_NpcLookList> s_mLoaded = new map<ResourceName, ref ELIFE_NpcLookList>();

	//------------------------------------------------------------------------------------------------
	//! Loaded once per file and kept.
	static ELIFE_NpcLookList Load(ResourceName path)
	{
		if (path.IsEmpty())
			return null;

		ELIFE_NpcLookList list = s_mLoaded.Get(path);
		if (list)
			return list;

		Resource resource = BaseContainerTools.LoadContainer(path);
		if (resource && resource.IsValid())
			list = ELIFE_NpcLookList.Cast(BaseContainerTools.CreateInstanceFromContainer(resource.GetResource().ToBaseContainer()));

		if (!list)
		{
			Print(string.Format("ELIFE_NpcLookList | %1 failed to load", path), LogLevel.ERROR);
			return null;
		}

		s_mLoaded.Set(path, list);
		return list;
	}

	//------------------------------------------------------------------------------------------------
	//! The looks named, in that order; all of them if names is empty. Unknown names are skipped with a warning.
	void Collect(array<string> names, notnull array<ELIFE_NpcLook> looks)
	{
		looks.Clear();
		if (!m_aLooks)
			return;

		if (!names || names.IsEmpty())
		{
			foreach (ELIFE_NpcLook look : m_aLooks)
			{
				if (look)
					looks.Insert(look);
			}

			return;
		}

		foreach (string name : names)
		{
			ELIFE_NpcLook found = Find(name);
			if (found)
				looks.Insert(found);
			else
				Print(string.Format("ELIFE_NpcLookList | no look named %1", name), LogLevel.WARNING);
		}
	}

	//------------------------------------------------------------------------------------------------
	ELIFE_NpcLook Find(string name)
	{
		if (!m_aLooks)
			return null;

		foreach (ELIFE_NpcLook look : m_aLooks)
		{
			if (look && look.m_sName == name)
				return look;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only. Gives the character this look, keeping its name, voice and clothing; the identity replicates.
	static bool Apply(notnull IEntity character, notnull ELIFE_NpcLook look)
	{
		CharacterIdentityComponent identityComp = CharacterIdentityComponent.Cast(character.FindComponent(CharacterIdentityComponent));
		if (!identityComp)
			return false;

		Identity current = identityComp.GetIdentity();
		if (!current)
			return false;

		VisualIdentity visual = new VisualIdentity();
		VisualIdentity currentVisual = current.GetVisualIdentity();
		if (currentVisual)
			visual.SetMeshConfig(currentVisual.GetMeshConfig());

		if (!look.m_sHead.IsEmpty())
			visual.SetHead(look.m_sHead);
		else if (currentVisual)
			visual.SetHead(currentVisual.GetHead());

		if (!look.m_sBody.IsEmpty())
			visual.SetBody(look.m_sBody);
		else if (currentVisual)
			visual.SetBody(currentVisual.GetBody());

		//! A new identity, like vanilla's tutorial respawn, so the prefab's own data is left alone.
		Identity identity = new Identity();
		identity.SetName(current.GetName());
		identity.SetAlias(current.GetAlias());
		identity.SetSurname(current.GetSurname());
		identity.SetSoundIdentity(current.GetSoundIdentity());
		identity.SetVisualIdentity(visual);
		identityComp.SetIdentity(identity);
		return true;
	}
}
