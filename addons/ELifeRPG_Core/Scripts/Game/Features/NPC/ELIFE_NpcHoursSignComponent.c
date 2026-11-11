//------------------------------------------------------------------------------------------------
[ComponentEditorProps(category: "ELifeRPG/NPC", description: "Shows the opening hours of the nearest NPC spot on a sign.")]
class ELIFE_NpcHoursSignComponentClass : ELIFE_WorldScreenComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! Client-local. Shows the hours of the nearest ELIFE_NpcSpot with opening hours. The owner is a text overlay on the
//! sign, hidden while nothing renders.
class ELIFE_NpcHoursSignComponent : ELIFE_WorldScreenComponent
{
	[Attribute("#ELIFE-Sign_OpeningHours", UIWidgets.EditBox, "Heading, may be a string table key.", category: "Hours Sign")]
	protected string m_sTitle;

	[Attribute("25", UIWidgets.EditBox, "Shows the hours of the nearest NPC spot with opening hours within this many metres.", category: "Hours Sign")]
	protected float m_fSpotRange;

	protected bool m_bWarnedNoSpot;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		if (GetGame().InPlayMode())
			owner.ClearFlags(EntityFlags.VISIBLE, false);
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

		ELIFE_NpcSpot spot = ELIFE_NpcSpot.FindNearestWithSchedule(GetOwner().GetOrigin(), m_fSpotRange);
		if (!spot)
		{
			if (m_bWarnedNoSpot)
				return true;

			m_bWarnedNoSpot = true;
			Print(string.Format("%1 | no NPC spot with opening hours within %2 m", ClassName(), m_fSpotRange), LogLevel.WARNING);
			return true;
		}

		ELIFE_NpcSchedule schedule = spot.GetSchedule();
		if (schedule.IsAlwaysOpen())
		{
			SetText(root, "Days", "#ELIFE-Sign_AlwaysOpen");
			return true;
		}

		SetText(root, "Days", "#ELIFE-Sign_Daily");
		SetText(root, "Hours", schedule.FormatHours());
		return true;
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
