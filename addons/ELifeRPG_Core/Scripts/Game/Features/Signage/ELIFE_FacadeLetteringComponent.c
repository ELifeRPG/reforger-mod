//------------------------------------------------------------------------------------------------
[ComponentEditorProps(category: "ELifeRPG/Signage", description: "Spells a name in 3D letters, e.g. a bank's name on its facade.")]
class ELIFE_FacadeLetteringComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! Client-local. Spells its text as one letter entity per character, centred on the owner's origin (baseline),
//! facing its +Z; seen from the front, reading runs along -X. Built in EOnInit: in OnPostInit the owner isn't placed yet.
class ELIFE_FacadeLetteringComponent : ScriptComponent
{
	[Attribute("", UIWidgets.EditBox, "Text to spell, may be a string table key. Empty: the place name (m_sMapName) of the NPC spot it is placed under, else of the nearest named spot within the spot range.", category: "Lettering")]
	protected string m_sText;

	[Attribute("", UIWidgets.EditBox, "Entity name of the NPC spot whose place name it spells, e.g. BankClerkSpot1. Empty: the spot it is placed under, else the nearest named one.", category: "Lettering")]
	protected string m_sSpotName;

	[Attribute("40", UIWidgets.EditBox, "Without a text and placed under no named NPC spot, it takes the name of the nearest one within this many metres.", category: "Lettering")]
	protected float m_fSpotRange;

	[Attribute("", UIWidgets.ResourceNamePicker, "Font: an ELIFE_GlyphSet config (Configs/Signs).", "conf", category: "Lettering")]
	protected ResourceName m_sGlyphSet;

	[Attribute("0.5", UIWidgets.EditBox, "Height of a capital letter in metres.", category: "Lettering")]
	protected float m_fLetterHeight;

	[Attribute("0", UIWidgets.EditBox, "Longest the text may get, in metres; a longer name gets smaller letters. 0 is no limit.", category: "Lettering")]
	protected float m_fMaxWidth;

	[Attribute("", UIWidgets.ResourceNamePicker, "Letter material, e.g. a brand colour. Empty: the glyph set's own (dark metal).", "emat", category: "Lettering")]
	protected ResourceName m_sMaterial;

	protected ref array<IEntity> m_aLetters = {};
	protected int m_iTries;

	protected static const int RETRY_MS = 500;
	//! Spots of later layers may be created a little later; give up after this many tries.
	protected static const int TRIES = 20;
	//! Material slot of every glyph mesh (the generator names it so).
	protected static const string LETTER_MATERIAL_SLOT = "Letter";

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		if (RplSession.Mode() == RplMode.Dedicated)
			return;

		SetEventMask(owner, EntityEvent.INIT);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		if (!TryBuild())
			GetGame().GetCallqueue().CallLater(Retry, RETRY_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(Retry);
		foreach (IEntity letter : m_aLetters)
		{
			if (letter)
				SCR_EntityHelper.DeleteEntityAndChildren(letter);
		}

		m_aLetters.Clear();
		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	protected void Retry()
	{
		m_iTries++;
		if (TryBuild())
		{
			GetGame().GetCallqueue().Remove(Retry);
			return;
		}

		if (m_iTries < TRIES)
			return;

		GetGame().GetCallqueue().Remove(Retry);
		Print(string.Format("%1 | %2 has no text: no m_sText, and spot '%3' or the nearest within %4 m has no place name", ClassName(), GetOwner().GetName(), m_sSpotName, m_fSpotRange), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	//! Spawns the letters. False while there is no text yet (worth retrying).
	protected bool TryBuild()
	{
		string text = GetText();
		if (text.IsEmpty())
			return false;

		ELIFE_GlyphSet glyphSet = ELIFE_GlyphSet.Load(m_sGlyphSet);
		if (!glyphSet)
		{
			Print(string.Format("%1 | %2: glyph set %3 didn't load", ClassName(), GetOwner().GetName(), m_sGlyphSet), LogLevel.WARNING);
			return true;
		}

		float width;
		float height = GetHeight(glyphSet, text, width);

		IEntity owner = GetOwner();
		vector transform[4];
		owner.GetWorldTransform(transform);
		vector start = transform[3];
		float x = width * 0.5;
		bool warned;
		int i;
		int count = text.Length();
		while (i < count)
		{
			int length = ELIFE_GlyphSet.CharLength(text, i);
			string character = text.Substring(i, length);
			i += length;
			ELIFE_Glyph glyph = glyphSet.Find(character);
			if (glyph)
			{
				transform[3] = start + transform[0] * x;
				SpawnLetter(glyph.m_sModel, transform, height);
			}
			else if (character != " " && !warned)
			{
				warned = true;
				Print(string.Format("%1 | the glyph set has no '%2' (in '%3'), left a gap", ClassName(), character, text), LogLevel.WARNING);
			}

			x -= glyphSet.GetAdvance(character) * height;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void SpawnLetter(ResourceName model, vector transform[4], float height)
	{
		Resource resource = Resource.Load(model);
		if (!resource || !resource.IsValid())
		{
			Print(string.Format("%1 | letter model %2 didn't load", ClassName(), model), LogLevel.WARNING);
			return;
		}

		VObject mesh = resource.GetResource().ToVObject();
		if (!mesh)
			return;

		IEntity owner = GetOwner();
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		params.Transform = transform;
		params.Scale = height;
		params.Parent = owner;

		IEntity letter = GetGame().SpawnEntity(GenericEntity, owner.GetWorld(), params);
		if (!letter)
			return;

		string remap;
		if (!m_sMaterial.IsEmpty())
			remap = string.Format("$remap '%1' '%2';", LETTER_MATERIAL_SLOT, m_sMaterial);

		letter.SetObject(mesh, remap);
		letter.SetFlags(EntityFlags.VISIBLE);
		letter.Update();
		m_aLetters.Insert(letter);
	}

	//------------------------------------------------------------------------------------------------
	//! Capital letter height, shrunk so the text stays within m_fMaxWidth. width: the text's width in metres.
	protected float GetHeight(notnull ELIFE_GlyphSet glyphSet, string text, out float width)
	{
		width = glyphSet.Measure(text) * m_fLetterHeight;
		if (m_fMaxWidth <= 0 || width <= m_fMaxWidth)
			return m_fLetterHeight;

		float height = m_fLetterHeight * m_fMaxWidth / width;
		width = m_fMaxWidth;
		return height;
	}

	//------------------------------------------------------------------------------------------------
	//! The text in capitals, translated; empty while there is none (yet).
	protected string GetText()
	{
		string text = m_sText;
		if (text.IsEmpty())
		{
			ELIFE_NpcSpot spot = FindSpot();
			if (spot)
				text = spot.GetPlaceName();
		}

		if (text.IsEmpty())
			return text;

		text = WidgetManager.Translate(text);
		// ToUpper may only handle ASCII; capital ß is spelled SS
		text.Replace("ä", "Ä");
		text.Replace("ö", "Ö");
		text.Replace("ü", "Ü");
		text.Replace("ß", "ss");
		text.ToUpper();
		return text;
	}

	//------------------------------------------------------------------------------------------------
	//! The spot named in m_sSpotName, else a named spot it is placed under (at any depth), else the nearest named one.
	protected ELIFE_NpcSpot FindSpot()
	{
		if (!m_sSpotName.IsEmpty())
			return ELIFE_NpcSpot.Cast(GetOwner().GetWorld().FindEntityByName(m_sSpotName));

		IEntity parent = GetOwner().GetParent();
		while (parent)
		{
			ELIFE_NpcSpot spot = ELIFE_NpcSpot.Cast(parent);
			if (spot && !spot.GetPlaceName().IsEmpty())
				return spot;

			parent = parent.GetParent();
		}

		return ELIFE_NpcSpot.FindNearestNamed(GetOwner().GetOrigin(), m_fSpotRange);
	}
}
