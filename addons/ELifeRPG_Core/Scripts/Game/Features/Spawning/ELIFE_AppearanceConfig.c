//------------------------------------------------------------------------------------------------
/*!
	Skin tone and head choices of the create screen, edited by the server owner in
	Configs/Characters/ELIFE_Appearances.conf. Each tone has a body prefab and the head prefabs it allows; the
	create screen hides a tone without heads.
*/
[BaseContainerProps(configRoot: true)]
class ELIFE_AppearanceConfig
{
	[Attribute()]
	ref array<ref ELIFE_SkinTone> m_aSkinTones;

	//------------------------------------------------------------------------------------------------
	//! True when skin indexes a tone of cfg and head indexes one of its heads. Null config, tone without body or heads
	//! is invalid.
	static bool IsValid(ELIFE_AppearanceConfig cfg, int skin, int head)
	{
		if (!cfg || !cfg.m_aSkinTones)
			return false;

		if (skin < 0 || skin >= cfg.m_aSkinTones.Count())
			return false;

		ELIFE_SkinTone tone = cfg.m_aSkinTones[skin];
		if (!tone || tone.m_sBody.IsEmpty() || !tone.m_aHeads)
			return false;

		return head >= 0 && head < tone.m_aHeads.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Null when the resource is missing or does not hold an ELIFE_AppearanceConfig.
	static ELIFE_AppearanceConfig Load(ResourceName path)
	{
		Resource res = BaseContainerTools.LoadContainer(path);
		if (!res || !res.IsValid())
			return null;

		BaseContainer src = res.GetResource().ToBaseContainer();
		if (!src)
			return null;

		return ELIFE_AppearanceConfig.Cast(BaseContainerTools.CreateInstanceFromContainer(src));
	}
}

//------------------------------------------------------------------------------------------------
//! One skin tone: its name key, swatch colour for the create screen, the body prefab and the allowed head prefabs.
[BaseContainerProps()]
class ELIFE_SkinTone
{
	[Attribute("", UIWidgets.EditBox, "Localised name")]
	string m_sName;

	[Attribute("1 1 1 1", UIWidgets.ColorPicker, "Swatch colour")]
	ref Color m_Swatch;

	[Attribute("", UIWidgets.ResourceNamePicker, "Body prefab", "et")]
	ResourceName m_sBody;

	[Attribute("", UIWidgets.ResourceNamePicker, "Head prefabs", "et")]
	ref array<ResourceName> m_aHeads;
}
