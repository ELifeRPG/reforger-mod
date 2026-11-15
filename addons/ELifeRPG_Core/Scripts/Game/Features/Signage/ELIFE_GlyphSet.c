//------------------------------------------------------------------------------------------------
//! One letter of a glyph set: its mesh (cap height 1 m, origin at the baseline left) and how far the next one starts.
[BaseContainerProps(), SCR_BaseContainerCustomTitleField("m_sCharacter")]
class ELIFE_Glyph
{
	[Attribute("", UIWidgets.EditBox, "The character, e.g. A")]
	string m_sCharacter;

	[Attribute("", UIWidgets.ResourceNamePicker, "Mesh of the letter", "xob")]
	ResourceName m_sModel;

	[Attribute("0.7", UIWidgets.EditBox, "Advance to the next letter, in cap heights")]
	float m_fAdvance;
}

//------------------------------------------------------------------------------------------------
//! A font as 3D letters, for ELIFE_FacadeLetteringComponent. Generated with the glyph kit (Props: Configs/Signs).
[BaseContainerProps(configRoot: true)]
class ELIFE_GlyphSet
{
	[Attribute("0.3", UIWidgets.EditBox, "Advance of a space (and of a character the set lacks), in cap heights")]
	float m_fSpaceAdvance;

	[Attribute(desc: "The letters")]
	ref array<ref ELIFE_Glyph> m_aGlyphs;

	protected ref map<string, ELIFE_Glyph> m_mByCharacter;

	//------------------------------------------------------------------------------------------------
	static ELIFE_GlyphSet Load(ResourceName path)
	{
		if (path.IsEmpty())
			return null;

		Resource resource = BaseContainerTools.LoadContainer(path);
		if (!resource || !resource.IsValid())
			return null;

		return ELIFE_GlyphSet.Cast(BaseContainerTools.CreateInstanceFromContainer(resource.GetResource().ToBaseContainer()));
	}

	//------------------------------------------------------------------------------------------------
	//! The glyph of one character, or null.
	ELIFE_Glyph Find(string character)
	{
		if (!m_mByCharacter)
		{
			m_mByCharacter = new map<string, ELIFE_Glyph>();
			foreach (ELIFE_Glyph glyph : m_aGlyphs)
			{
				m_mByCharacter.Set(glyph.m_sCharacter, glyph);
			}
		}

		return m_mByCharacter.Get(character);
	}

	//------------------------------------------------------------------------------------------------
	//! Advance of one character, in cap heights.
	float GetAdvance(string character)
	{
		ELIFE_Glyph glyph = Find(character);
		if (glyph)
			return glyph.m_fAdvance;

		return m_fSpaceAdvance;
	}

	//------------------------------------------------------------------------------------------------
	//! Width of a whole text, in cap heights.
	float Measure(string text)
	{
		float width;
		int i;
		int count = text.Length();
		while (i < count)
		{
			int length = CharLength(text, i);
			width += GetAdvance(text.Substring(i, length));
			i += length;
		}

		return width;
	}

	//------------------------------------------------------------------------------------------------
	//! Bytes of the UTF-8 character starting at index (strings index bytes, so an umlaut is two).
	static int CharLength(string text, int index)
	{
		int length = 1;
		int count = text.Length();
		while (index + length < count && (text.ToAscii(index + length) & 0xC0) == 0x80)
		{
			length++;
		}

		return length;
	}
}
