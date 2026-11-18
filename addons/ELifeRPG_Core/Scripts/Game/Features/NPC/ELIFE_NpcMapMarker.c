//------------------------------------------------------------------------------------------------
modded enum SCR_EMapMarkerType
{
	ELIFE_NPC_SPOT
}

//------------------------------------------------------------------------------------------------
//! Client-local static map marker for an NPC spot with an opening-hours sign, one per spot however many signs.
class ELIFE_NpcMapMarker : SCR_MapMarkerBase
{
	protected static ref array<ref ELIFE_NpcMapMarker> s_aMarkers = {};

	protected ELIFE_NpcSpot m_Spot;
	//! Signs that asked for this marker.
	protected int m_iSigns;

	//------------------------------------------------------------------------------------------------
	ELIFE_NpcSpot GetSpot()
	{
		return m_Spot;
	}

	//------------------------------------------------------------------------------------------------
	//! Client. Puts the spot on the map, or counts one more sign for its marker. False if the map markers aren't
	//! ready yet (no marker manager or no player controller).
	static bool AddFor(notnull ELIFE_NpcSpot spot)
	{
		RemoveOrphans();
		ELIFE_NpcMapMarker existing = Find(spot);
		if (existing)
		{
			existing.m_iSigns++;
			return true;
		}

		SCR_MapMarkerManagerComponent manager = SCR_MapMarkerManagerComponent.GetInstance();
		if (!manager || !GetGame().GetPlayerController())
			return false;

		ELIFE_NpcMapMarker marker = new ELIFE_NpcMapMarker();
		marker.m_Spot = spot;
		marker.m_iSigns = 1;
		marker.SetType(SCR_EMapMarkerType.ELIFE_NPC_SPOT);
		vector position = spot.GetOrigin();
		marker.SetWorldPos((int)position[0], (int)position[2]);
		s_aMarkers.Insert(marker);
		manager.InsertStaticMarker(marker, true);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Client. One sign fewer; the marker goes with the last one.
	static void RemoveFor(ELIFE_NpcSpot spot)
	{
		ELIFE_NpcMapMarker marker = Find(spot);
		if (!marker)
			return;

		marker.m_iSigns--;
		if (marker.m_iSigns > 0)
			return;

		SCR_MapMarkerManagerComponent manager = SCR_MapMarkerManagerComponent.GetInstance();
		if (manager)
			manager.RemoveStaticMarker(marker);

		s_aMarkers.RemoveItem(marker);
	}

	//------------------------------------------------------------------------------------------------
	//! A sign whose spot was deleted first can't remove its marker any more.
	protected static void RemoveOrphans()
	{
		SCR_MapMarkerManagerComponent manager = SCR_MapMarkerManagerComponent.GetInstance();
		for (int i = s_aMarkers.Count() - 1; i >= 0; i--)
		{
			if (s_aMarkers[i].m_Spot)
				continue;

			if (manager)
				manager.RemoveStaticMarker(s_aMarkers[i]);

			s_aMarkers.Remove(i);
		}
	}

	//------------------------------------------------------------------------------------------------
	static void GetAll(notnull array<ELIFE_NpcMapMarker> outMarkers)
	{
		outMarkers.Clear();
		foreach (ELIFE_NpcMapMarker marker : s_aMarkers)
		{
			outMarkers.Insert(marker);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static ELIFE_NpcMapMarker Find(ELIFE_NpcSpot spot)
	{
		foreach (ELIFE_NpcMapMarker marker : s_aMarkers)
		{
			if (marker.m_Spot == spot)
				return marker;
		}

		return null;
	}
}

//------------------------------------------------------------------------------------------------
//! Look of the NPC spot markers: the NPC's map icon and "name · open until 15:00" / "name · closed · opens 16:00",
//! worked out on each client from the spot's hours and the in-game clock (far away NPCs aren't streamed in).
[BaseContainerProps(), SCR_MapMarkerTitle()]
class ELIFE_MapMarkerEntryNpcSpot : SCR_MapMarkerEntryConfig
{
	[Attribute("0.31 0.75 0.47 1", UIWidgets.ColorPicker, "Icon and text while open, for NPCs without their own map colour (ELIFE_NpcComponent.m_vMapColor)")]
	protected ref Color m_OpenColor;

	[Attribute("0.6 0.6 0.6 0.75", UIWidgets.ColorPicker, "Icon and text while closed")]
	protected ref Color m_ClosedColor;

	protected static const int REFRESH_MS = 5000;

	//------------------------------------------------------------------------------------------------
	override SCR_EMapMarkerType GetMarkerType()
	{
		return SCR_EMapMarkerType.ELIFE_NPC_SPOT;
	}

	//------------------------------------------------------------------------------------------------
	//! Not super: that shows the author and the "own marker" icon, and local markers belong to the local player. Their
	//! setters can't hide them either (SetModeIcon shows it for own markers whatever it's given), so they are hidden here.
	override void InitClientSettings(SCR_MapMarkerBase marker, SCR_MapMarkerWidgetComponent widgetComp, bool skipProfanityFilter = false)
	{
		ELIFE_NpcMapMarker npcMarker = ELIFE_NpcMapMarker.Cast(marker);
		if (!npcMarker || !widgetComp)
			return;

		Widget root = marker.GetRootWidget();
		if (root)
		{
			array<string> hidden = {"MarkerModeIcon", "MarkerAuthor", "PlatformIcon"};
			foreach (string name : hidden)
			{
				Widget widget = root.FindAnyWidget(name);
				if (widget)
					widget.SetVisible(false);
			}
		}

		Style(npcMarker, widgetComp);
	}

	//------------------------------------------------------------------------------------------------
	override void OnMapOpen(notnull SCR_MapEntity mapEnt, notnull SCR_MapMarkersUI markerUIComp)
	{
		GetGame().GetCallqueue().Remove(Refresh);
		GetGame().GetCallqueue().CallLater(Refresh, REFRESH_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	override void OnMapClose(notnull SCR_MapEntity mapEnt, notnull SCR_MapMarkersUI markerUIComp)
	{
		GetGame().GetCallqueue().Remove(Refresh);
	}

	//------------------------------------------------------------------------------------------------
	//! While the map is open: opening or closing time may pass.
	protected void Refresh()
	{
		array<ELIFE_NpcMapMarker> markers = {};
		ELIFE_NpcMapMarker.GetAll(markers);
		foreach (ELIFE_NpcMapMarker marker : markers)
		{
			SCR_MapMarkerWidgetComponent widgetComp = marker.GetMarkerComponent();
			if (widgetComp)
				Style(marker, widgetComp);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected Color OpenColor(notnull ELIFE_NpcSpot spot)
	{
		vector rgb = spot.GetMapColor();
		if (rgb == vector.Zero)
			return m_OpenColor;

		return ELIFE_PhoneStyle.Srgb(rgb[0], rgb[1], rgb[2]);
	}

	//------------------------------------------------------------------------------------------------
	protected void Style(notnull ELIFE_NpcMapMarker marker, notnull SCR_MapMarkerWidgetComponent widgetComp)
	{
		ELIFE_NpcSpot spot = marker.GetSpot();
		if (!spot)
			return;

		ELIFE_NpcSchedule schedule = spot.GetSchedule();
		int minute = ELIFE_NpcSchedule.GetCurrentMinute();
		bool open = !schedule || minute < 0 || schedule.IsOpenAt(minute);
		//! Translate doesn't translate its arguments.
		string name = WidgetManager.Translate(spot.GetMapName());

		string text;
		if (!schedule || minute < 0 || schedule.IsAlwaysOpen())
			text = WidgetManager.Translate("#ELIFE-Map_Open", name);
		else if (open)
			text = WidgetManager.Translate("#ELIFE-Map_OpenUntil", name, ELIFE_NpcSchedule.FormatMinute(schedule.GetCloseMinuteAt(minute)));
		else
			text = WidgetManager.Translate("#ELIFE-Map_ClosedOpens", name, ELIFE_NpcSchedule.FormatMinute(schedule.GetNextOpenMinute(minute)));

		widgetComp.SetText(text, true);

		Widget root = marker.GetRootWidget();
		ResourceName icon = spot.GetMapIcon();
		if (root && !icon.IsEmpty())
		{
			ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("MarkerIcon"));
			if (iconWidget)
			{
				iconWidget.LoadImageTexture(0, icon);
				iconWidget.SetImage(0);
				iconWidget.SetVisible(true);
			}
		}

		Color color = m_ClosedColor;
		if (open)
			color = OpenColor(spot);

		widgetComp.SetColor(color);
		//! Not SetTextVisible, which shows the label only on hover; SetColor doesn't tint the text.
		if (root)
		{
			TextWidget textWidget = TextWidget.Cast(root.FindAnyWidget("MarkerText"));
			if (textWidget)
			{
				textWidget.SetColor(color);
				textWidget.SetVisible(true);
			}
		}
	}
}
