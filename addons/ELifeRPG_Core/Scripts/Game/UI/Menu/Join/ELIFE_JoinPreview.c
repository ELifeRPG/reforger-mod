//------------------------------------------------------------------------------------------------
//! Appearance previews for the select and create screens. Previews need the world's ItemPreviewManager and an
//! ItemPreviewWidget; without either the screens still work and the preview stays blank (one warning per cause).
class ELIFE_JoinPreview
{
	protected static bool s_bWarnedNoManager;
	protected static bool s_bWarnedNoWidget;

	//------------------------------------------------------------------------------------------------
	//! Appearance prefab of the client's session manager, or an empty ResourceName when the instance is missing.
	static ResourceName GetAppearancePrefab(int index)
	{
		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (!manager)
			return "";

		return manager.GetAppearancePrefab(index);
	}

	//------------------------------------------------------------------------------------------------
	//! Appearance count from the client's session manager; without it the payload's count is used.
	static int GetAppearanceCount(int payloadCount)
	{
		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (!manager)
			return payloadCount;

		return manager.GetAppearanceCount();
	}

	//------------------------------------------------------------------------------------------------
	//! Renders the prefab into the widget, an ItemPreviewWidget from the layout. A missing manager hides the widget.
	static void ShowPreview(Widget widget, ResourceName prefab)
	{
		ItemPreviewWidget preview = ItemPreviewWidget.Cast(widget);
		if (!preview)
		{
			if (!s_bWarnedNoWidget)
			{
				s_bWarnedNoWidget = true;
				Print("ELIFE_Join | preview widget is not an ItemPreviewWidget, previews stay blank", LogLevel.WARNING);
			}

			return;
		}

		ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
		ItemPreviewManagerEntity previewManager = null;
		if (world)
			previewManager = world.GetItemPreviewManager();

		if (!previewManager)
		{
			if (!s_bWarnedNoManager)
			{
				s_bWarnedNoManager = true;
				Print("ELIFE_Join | no ItemPreviewManager in the world, previews are hidden", LogLevel.WARNING);
			}

			preview.SetVisible(false);
			return;
		}

		if (prefab == "")
			return;

		previewManager.SetPreviewItemFromPrefab(preview, prefab);
	}
}
