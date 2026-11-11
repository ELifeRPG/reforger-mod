//------------------------------------------------------------------------------------------------
[EntityEditorProps(category: "ELifeRPG/Dev", description: "Dev world only: sets the in-game time and how fast it runs when play starts, to test opening hours.")]
class ELIFE_DevTimeComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! Server-only, once at the start of play.
class ELIFE_DevTimeComponent : ScriptComponent
{
	[Attribute("19", UIWidgets.Slider, "Start hour", "0 23 1")]
	protected int m_iStartHour;

	[Attribute("55", UIWidgets.Slider, "Start minute", "0 59 1")]
	protected int m_iStartMinute;

	[Attribute("1440", UIWidgets.EditBox, "Real seconds per in-game day; 1440 is one in-game minute per real second")]
	protected float m_fDaySeconds;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		if (!GetGame().InPlayMode() || !Replication.IsServer())
			return;

		//! The time manager may not be ready in the first frame.
		GetGame().GetCallqueue().CallLater(Apply, 1000);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(Apply);
	}

	//------------------------------------------------------------------------------------------------
	protected void Apply()
	{
		ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
		TimeAndWeatherManagerEntity time;
		if (world)
			time = world.GetTimeAndWeatherManager();

		if (!time)
		{
			Print("ELIFE_DevTimeComponent | no time manager in this world", LogLevel.WARNING);
			return;
		}

		time.SetIsDayAutoAdvanced(true);
		time.SetDayDuration(m_fDaySeconds);
		time.SetHoursMinutesSeconds(m_iStartHour, m_iStartMinute, 0, true);
		Print(string.Format("ELIFE_DevTimeComponent | time set to %1:%2, a day lasts %3 s", m_iStartHour, m_iStartMinute.ToString(2), m_fDaySeconds));
	}
}
