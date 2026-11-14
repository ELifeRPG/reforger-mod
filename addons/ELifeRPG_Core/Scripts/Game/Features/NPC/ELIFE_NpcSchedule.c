//------------------------------------------------------------------------------------------------
//! Daily opening hours in in-game time, set on an NPC prefab and optionally overridden on its spot: one range, plus
//! an optional second one (e.g. a break between them). In each range, closing before opening means open over
//! midnight; the same time for both in the first range means always open.
[BaseContainerProps()]
class ELIFE_NpcSchedule
{
	[Attribute("9", UIWidgets.Slider, "Opens at this hour", "0 23 1")]
	protected int m_iOpenHour;

	[Attribute("0", UIWidgets.Slider, "and minute", "0 59 1")]
	protected int m_iOpenMinute;

	[Attribute("15", UIWidgets.Slider, "Closes at this hour", "0 23 1")]
	protected int m_iCloseHour;

	[Attribute("0", UIWidgets.Slider, "and minute", "0 59 1")]
	protected int m_iCloseMinute;

	[Attribute("0", UIWidgets.CheckBox, "Also open in a second range, e.g. after a break")]
	protected bool m_bSecondRange;

	[Attribute("16", UIWidgets.Slider, "Second range opens at this hour", "0 23 1")]
	protected int m_iSecondOpenHour;

	[Attribute("0", UIWidgets.Slider, "and minute", "0 59 1")]
	protected int m_iSecondOpenMinute;

	[Attribute("2", UIWidgets.Slider, "Second range closes at this hour", "0 23 1")]
	protected int m_iSecondCloseHour;

	[Attribute("0", UIWidgets.Slider, "and minute", "0 59 1")]
	protected int m_iSecondCloseMinute;

	protected static const int DAY_MINUTES = 24 * 60;

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
	bool HasSecondRange()
	{
		return m_bSecondRange && !IsAlwaysOpen() && GetSecondOpenMinute() != GetSecondCloseMinute();
	}

	//------------------------------------------------------------------------------------------------
	int GetSecondOpenMinute()
	{
		return m_iSecondOpenHour * 60 + m_iSecondOpenMinute;
	}

	//------------------------------------------------------------------------------------------------
	int GetSecondCloseMinute()
	{
		return m_iSecondCloseHour * 60 + m_iSecondCloseMinute;
	}

	//------------------------------------------------------------------------------------------------
	bool IsOpenAt(int minute)
	{
		if (IsAlwaysOpen() || IsInRange(minute, GetOpenMinute(), GetCloseMinute()))
			return true;

		return HasSecondRange() && IsInRange(minute, GetSecondOpenMinute(), GetSecondCloseMinute());
	}

	//------------------------------------------------------------------------------------------------
	bool IsAlwaysOpen()
	{
		return GetOpenMinute() == GetCloseMinute();
	}

	//------------------------------------------------------------------------------------------------
	//! When he opens next after the given minute: the nearer of the ranges' opening times. -1 if always open.
	int GetNextOpenMinute(int minute)
	{
		if (IsAlwaysOpen())
			return -1;

		int next = GetOpenMinute();
		if (HasSecondRange() && MinutesUntil(minute, GetSecondOpenMinute()) < MinutesUntil(minute, next))
			next = GetSecondOpenMinute();

		return next;
	}

	//------------------------------------------------------------------------------------------------
	//! When the range open at the given minute closes, or -1 if none is open (or he is always open).
	int GetCloseMinuteAt(int minute)
	{
		if (IsAlwaysOpen())
			return -1;

		if (IsInRange(minute, GetOpenMinute(), GetCloseMinute()))
			return GetCloseMinute();

		if (HasSecondRange() && IsInRange(minute, GetSecondOpenMinute(), GetSecondCloseMinute()))
			return GetSecondCloseMinute();

		return -1;
	}

	//------------------------------------------------------------------------------------------------
	//! "08:00 – 20:00": the first range.
	string FormatHours()
	{
		return FormatMinute(GetOpenMinute()) + " – " + FormatMinute(GetCloseMinute());
	}

	//------------------------------------------------------------------------------------------------
	//! The second range like FormatHours, or "" without one.
	string FormatSecondHours()
	{
		if (!HasSecondRange())
			return "";

		return FormatMinute(GetSecondOpenMinute()) + " – " + FormatMinute(GetSecondCloseMinute());
	}

	//------------------------------------------------------------------------------------------------
	//! Closing before opening means over midnight.
	protected static bool IsInRange(int minute, int open, int close)
	{
		if (open < close)
			return minute >= open && minute < close;

		if (open > close)
			return minute >= open || minute < close;

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Minutes from one minute of the day forward to another, 0 to 1439.
	protected static int MinutesUntil(int from, int to)
	{
		return (to - from + DAY_MINUTES) % DAY_MINUTES;
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
