//------------------------------------------------------------------------------------------------
//! Appearance previews for the select and create screens. Previews need the world's ItemPreviewManager and an
//! ItemPreviewWidget; without either the screens still work and the preview stays blank (one warning per cause).
class ELIFE_JoinPreview
{
	protected static bool s_bWarnedNoManager;
	protected static bool s_bWarnedNoWidget;
	protected static bool s_bWarnedLocalSpawn;

	//------------------------------------------------------------------------------------------------
	//! Character prefab of the client's session manager, or an empty ResourceName when the instance is missing.
	static ResourceName GetCharacterPrefab()
	{
		ELIFE_JoinSessionManagerComponent manager = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (!manager)
			return "";

		return manager.GetCharacterPrefab();
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

	//------------------------------------------------------------------------------------------------
	//! Spawns a local, non-replicated character from GetCharacterPrefab() with the body and head applied to its visual
	//! identity (the same steps as the server's ApplyAppearance_S). The caller owns the entity and deletes it with
	//! DeleteLocalCharacter. Null when the prefab, the identity component or the visual identity is missing (WARNING, once).
	static IEntity SpawnLocalCharacter(ResourceName body, ResourceName head)
	{
		ResourceName prefab = GetCharacterPrefab();
		Resource res = null;
		if (prefab != "")
			res = Resource.Load(prefab);

		IEntity entity = null;
		if (res && res.IsValid())
			entity = GetGame().SpawnEntityPrefabLocal(res);

		SCR_CharacterIdentityComponent identityComponent = null;
		if (entity)
			identityComponent = SCR_CharacterIdentityComponent.Cast(entity.FindComponent(SCR_CharacterIdentityComponent));

		Identity identity = null;
		if (identityComponent)
			identity = identityComponent.GetIdentity();

		VisualIdentity visual = null;
		if (identity)
			visual = identity.GetVisualIdentity();

		if (!visual)
		{
			if (entity)
				SCR_EntityHelper.DeleteEntityAndChildren(entity);

			if (!s_bWarnedLocalSpawn)
			{
				s_bWarnedLocalSpawn = true;
				Print(string.Format("ELIFE_Join | local preview character not built (prefab=%1), falling back to the plain prefab preview", prefab), LogLevel.WARNING);
			}

			return null;
		}

		visual.SetHead(head);
		visual.SetBody(body);
		identityComponent.SetIdentity(identity);
		identityComponent.CommitChanges();
		return entity;
	}

	//------------------------------------------------------------------------------------------------
	//! Shows a local character from SpawnLocalCharacter in the widget. False when the widget is not an ItemPreviewWidget or the world has no ItemPreviewManager.
	static bool ShowLocalCharacter(Widget widget, IEntity entity)
	{
		ItemPreviewWidget preview = ItemPreviewWidget.Cast(widget);
		ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
		ItemPreviewManagerEntity previewManager = null;
		if (world)
			previewManager = world.GetItemPreviewManager();

		if (!preview || !previewManager || !entity)
			return false;

		previewManager.SetPreviewItem(preview, entity);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Deletes a character made by SpawnLocalCharacter. Null is ignored.
	static void DeleteLocalCharacter(IEntity entity)
	{
		if (entity)
			SCR_EntityHelper.DeleteEntityAndChildren(entity);
	}
}
