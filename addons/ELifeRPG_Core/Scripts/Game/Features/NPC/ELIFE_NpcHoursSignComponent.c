//------------------------------------------------------------------------------------------------
[ComponentEditorProps(category: "ELifeRPG/NPC", description: "Shows the opening hours of the nearest NPC spot on a sign.")]
class ELIFE_NpcHoursSignComponentClass : ELIFE_WorldScreenComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! Client-local. Shows the hours of the ELIFE_NpcSpot it is placed under, else of the nearest one with opening hours.
//! The owner is a text overlay on the sign, hidden while nothing renders.
class ELIFE_NpcHoursSignComponent : ELIFE_WorldScreenComponent
{
	[Attribute("#ELIFE-Sign_OpeningHours", UIWidgets.EditBox, "Heading, may be a string table key.", category: "Hours Sign")]
	protected string m_sTitle;

	[Attribute("", UIWidgets.EditBox, "Entity name of the NPC spot whose hours it shows, e.g. BankClerkSpot1. Empty: the spot it is placed under, else the nearest one with opening hours.", category: "Hours Sign")]
	protected string m_sSpotName;

	[Attribute("25", UIWidgets.EditBox, "Placed under no NPC spot, it shows the hours of the nearest one with opening hours within this many metres.", category: "Hours Sign")]
	protected float m_fSpotRange;

	[Attribute("0", UIWidgets.CheckBox, "Heading shows the spot's name (ELIFE_NpcSpot.m_sMapName) instead of the title, if it has one.", category: "Hours Sign")]
	protected bool m_bShowName;

	protected bool m_bWarnedNoSpot;

	//! The spot whose map marker this sign added.
	protected ELIFE_NpcSpot m_MarkerSpot;
	protected int m_iMarkerTries;
	protected static const int MARKER_RETRY_MS = 1000;
	protected static const int MARKER_TRIES = 60;

	//! Width the heading may take, and the average glyph width per font size, to shrink long names to fit.
	protected static const float TITLE_MAX_WIDTH = 880;
	protected static const float GLYPH_WIDTH = 0.5;
	protected static const int TITLE_SIZE = 110;

	//! With two ranges (one range: the layout's 110, 80 and 120, 8 px above and below each line).
	protected static const int TWO_RANGES_TITLE_SIZE = 90;
	protected static const int TWO_RANGES_DAYS_SIZE = 64;
	protected static const int TWO_RANGES_HOURS_SIZE = 90;
	protected static const int TWO_RANGES_LINE_GAP = 2;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		if (!GetGame().InPlayMode())
			return;

		owner.ClearFlags(EntityFlags.VISIBLE, false);

		//! Its spot on the map, on every player's machine (a dedicated server has no map).
		if (RplSession.Mode() != RplMode.Dedicated)
			GetGame().GetCallqueue().CallLater(AddMapMarker, MARKER_RETRY_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	//! Retried: the spot (hierarchy, registry), the player controller and the marker manager come a little later.
	protected void AddMapMarker()
	{
		m_iMarkerTries++;
		ELIFE_NpcSpot spot = FindSpot();
		if (spot && ELIFE_NpcMapMarker.AddFor(spot))
		{
			m_MarkerSpot = spot;
			GetGame().GetCallqueue().Remove(AddMapMarker);
			return;
		}

		if (m_iMarkerTries >= MARKER_TRIES)
			GetGame().GetCallqueue().Remove(AddMapMarker);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(AddMapMarker);
		if (m_MarkerSpot)
			ELIFE_NpcMapMarker.RemoveFor(m_MarkerSpot);

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	override protected bool BuildContent(Widget host)
	{
		if (!super.BuildContent(host))
			return false;

		Widget root = GetContentRoot();
		if (!root)
			return false;

		SetText(root, "Title", m_sTitle);

		ELIFE_NpcSpot spot = FindSpot();
		if (!spot)
		{
			if (m_bWarnedNoSpot)
				return true;

			m_bWarnedNoSpot = true;
			Print(string.Format("%1 | no NPC spot with opening hours (spot '%2', else within %3 m)", ClassName(), m_sSpotName, m_fSpotRange), LogLevel.WARNING);
			return true;
		}

		string name;
		if (m_bShowName)
			name = WidgetManager.Translate(spot.GetPlaceName());

		if (!name.IsEmpty())
			SetText(root, "Title", name);

		ELIFE_NpcSchedule schedule = spot.GetSchedule();
		if (schedule.IsAlwaysOpen())
		{
			SetText(root, "Days", "#ELIFE-Sign_AlwaysOpen");
			FitTitle(root, name, TITLE_SIZE);
			return true;
		}

		SetText(root, "Days", "#ELIFE-Sign_Daily");
		SetText(root, "Hours", schedule.FormatHours());

		//! A second range is a second hours line. Four lines in the one-range sizes overflow the plate, so all of them
		//! get smaller and closer: the block ends up about as tall as with one range.
		string second = schedule.FormatSecondHours();
		SetText(root, "Hours2", second);
		Widget hours2 = root.FindAnyWidget("Hours2");
		if (hours2)
			hours2.SetVisible(!second.IsEmpty());

		int titleSize = TITLE_SIZE;
		if (!second.IsEmpty())
		{
			titleSize = TWO_RANGES_TITLE_SIZE;
			Compact(root, "Title", TWO_RANGES_TITLE_SIZE);
			Compact(root, "Days", TWO_RANGES_DAYS_SIZE);
			Compact(root, "Hours", TWO_RANGES_HOURS_SIZE);
			Compact(root, "Hours2", TWO_RANGES_HOURS_SIZE);
		}

		FitTitle(root, name, titleSize);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! A name in the heading shrinks below fontSize when it's too long for the plate (estimated from its length).
	protected void FitTitle(notnull Widget root, string name, int fontSize)
	{
		if (name.IsEmpty())
			return;

		TextWidget widget = TextWidget.Cast(root.FindAnyWidget("Title"));
		if (!widget)
			return;

		int fitting = TITLE_MAX_WIDTH / (name.Length() * GLYPH_WIDTH);
		widget.SetExactFontSize(Math.Min(fontSize, fitting));
	}

	//------------------------------------------------------------------------------------------------
	protected void Compact(notnull Widget root, string name, int fontSize)
	{
		TextWidget widget = TextWidget.Cast(root.FindAnyWidget(name));
		if (!widget)
			return;

		widget.SetExactFontSize(fontSize);
		LayoutSlot.SetPadding(widget, 0, TWO_RANGES_LINE_GAP, 0, TWO_RANGES_LINE_GAP);
	}

	//------------------------------------------------------------------------------------------------
	//! The spot this sign belongs to: the one named in m_sSpotName, else one it is placed under (a parent, at any depth) if that has opening hours,
	//! else the nearest spot with opening hours within m_fSpotRange.
	protected ELIFE_NpcSpot FindSpot()
	{
		if (!m_sSpotName.IsEmpty())
		{
			ELIFE_NpcSpot named = ELIFE_NpcSpot.Cast(GetOwner().GetWorld().FindEntityByName(m_sSpotName));
			if (named && named.GetSchedule())
				return named;

			return null;
		}

		IEntity parent = GetOwner().GetParent();
		while (parent)
		{
			ELIFE_NpcSpot spot = ELIFE_NpcSpot.Cast(parent);
			if (spot && spot.GetSchedule())
				return spot;

			parent = parent.GetParent();
		}

		return ELIFE_NpcSpot.FindNearestWithSchedule(GetOwner().GetOrigin(), m_fSpotRange);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetText(notnull Widget root, string name, string text)
	{
		TextWidget widget = TextWidget.Cast(root.FindAnyWidget(name));
		if (widget)
			widget.SetText(text);
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnBeforeBind()
	{
		super.OnBeforeBind();
		GetOwner().SetFlags(EntityFlags.VISIBLE, false);
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnUnbound()
	{
		super.OnUnbound();
		GetOwner().ClearFlags(EntityFlags.VISIBLE, false);
	}
}
