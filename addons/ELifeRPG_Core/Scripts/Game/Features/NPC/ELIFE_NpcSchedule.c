//------------------------------------------------------------------------------------------------
//! One daily opening time and closing time in in-game time, set on an NPC prefab and optionally overridden on its
//! spot. Closing before opening means open over midnight; the same time for both means always open.
[BaseContainerProps()]
class ELIFE_NpcSchedule
{
	[Attribute("8", UIWidgets.Slider, "Opens at this hour", "0 23 1")]
	protected int m_iOpenHour;

	[Attribute("0", UIWidgets.Slider, "and minute", "0 59 1")]
	protected int m_iOpenMinute;

	[Attribute("20", UIWidgets.Slider, "Closes at this hour", "0 23 1")]
	protected int m_iCloseHour;

	[Attribute("0", UIWidgets.Slider, "and minute", "0 59 1")]
	protected int m_iCloseMinute;

	//------------------------------------------------------------------------------------------------
	int GetOpenMinute()
	{
		return m_iOpenHour * 60 + m_iOpenMinute;
	}

	//------------------------------------------------------------------------------------------------
	int GetCloseMinute()
	{
		return m_iCloseHour * 60 + m_iCloseMinute;
	}

	//------------------------------------------------------------------------------------------------
	bool IsOpenAt(int minute)
	{
		int open = GetOpenMinute();
		int close = GetCloseMinute();
		if (open == close)
			return true;

		if (open < close)
			return minute >= open && minute < close;

		return minute >= open || minute < close;
	}

	//------------------------------------------------------------------------------------------------
	//! In-game minute of the day, or -1 in a world without a clock.
	static int GetCurrentMinute()
	{
		ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
		if (!world)
			return -1;

		TimeAndWeatherManagerEntity time = world.GetTimeAndWeatherManager();
		if (!time)
			return -1;

		int hour, minute, second;
		time.GetHoursMinutesSeconds(hour, minute, second);
		return hour * 60 + minute;
	}

	//------------------------------------------------------------------------------------------------
	//! "08:00"
	static string FormatMinute(int minute)
	{
		if (minute < 0)
			return "";

		int hour = minute / 60;
		return string.Format("%1:%2", hour.ToString(2), (minute % 60).ToString(2));
	}
}
