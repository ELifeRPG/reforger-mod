//------------------------------------------------------------------------------------------------
//! Money formatting. The Bridge sends money as display text ("1 234.56 Ŧ"), shown as it is; FormatMoney is for
//! amounts the mod holds itself, such as cash.
class ELIFE_PhoneBankingService
{
	protected static const int IBAN_GROUP_LENGTH = 4;
	protected static const string DIGITS = "0123456789";
	protected static const string NONZERO_DIGITS = "123456789";
	protected static const string MINUS_SIGN = "−";
	//! A thin space is three bytes in UTF-8.
	protected static const int SEPARATOR_MAX_BYTES = 3;
	//! The largest whole amount whose cents, with any two cent digits, still fit an int.
	protected static const int MAX_MAJOR = 21474835;
	protected static const int SATURATED_CENTS = 2147483647;

	//------------------------------------------------------------------------------------------------
	//! Reads the Bridge's display text back into cents, for comparing with amounts the mod caps. Saturates at the
	//! int range instead of wrapping, so a huge balance still compares as huge. Never send the result anywhere.
	//! Group separators may be spaces (also non-breaking or thin), apostrophes, commas or dots; the decimal
	//! separator is the last comma or dot when one or two digits follow it.
	static int MoneyTextToCents(string text)
	{
		bool negative;
		int first = -1;
		for (int i = 0; i < text.Length(); i++)
		{
			string ch = text.Substring(i, 1);
			if (DIGITS.Contains(ch))
			{
				first = i;
				break;
			}

			if (ch == "-" || text.IndexOfFrom(i, MINUS_SIGN) == i)
				negative = true;
		}

		if (first < 0)
			return 0;

		//! The figure's digits, with "," or "." kept and every other separator as a space.
		string run;
		for (int i = first; i < text.Length(); i++)
		{
			string ch = text.Substring(i, 1);
			if (DIGITS.Contains(ch))
			{
				run += ch;
				continue;
			}

			int nextDigit = FindDigit(text, i + 1, SEPARATOR_MAX_BYTES);
			if (nextDigit < 0 || !IsSeparator(text, i, nextDigit))
				break;

			if (ch == "," || ch == ".")
				run += ch;
			else
				run += " ";

			i = nextDigit - 1;
		}

		int decimalAt = Math.Max(run.LastIndexOf(","), run.LastIndexOf("."));
		int minorDigits = run.Length() - decimalAt - 1;
		if (decimalAt < 0 || minorDigits > 2)
			decimalAt = run.Length();

		int major;
		bool saturated;
		for (int i = 0; i < decimalAt; i++)
		{
			int digit = DIGITS.IndexOf(run.Substring(i, 1));
			if (digit < 0)
				continue;

			if (major > (MAX_MAJOR - digit) / 10)
				saturated = true;
			else
				major = major * 10 + digit;
		}

		int minor;
		for (int i = decimalAt + 1; i < run.Length(); i++)
			minor = minor * 10 + DIGITS.IndexOf(run.Substring(i, 1));

		if (run.Length() - decimalAt - 1 == 1)
			minor *= 10;

		int cents = major * 100 + minor;
		if (saturated)
			cents = SATURATED_CENTS;

		if (negative)
			return -cents;

		return cents;
	}

	//------------------------------------------------------------------------------------------------
	//! The first digit at or after start, no further than maxDistance. -1 if none.
	protected static int FindDigit(string text, int start, int maxDistance)
	{
		for (int i = start; i < text.Length() && i <= start + maxDistance; i++)
		{
			if (DIGITS.Contains(text.Substring(i, 1)))
				return i;
		}

		return -1;
	}

	//------------------------------------------------------------------------------------------------
	//! Whether text[from, to) can sit between two digits of one figure: one ASCII separator, or the bytes of a
	//! single non-ASCII space such as a non-breaking or thin one.
	protected static bool IsSeparator(string text, int from, int to)
	{
		string ch = text.Substring(from, 1);
		if (to - from == 1 && (ch == " " || ch == "," || ch == "." || ch == "'"))
			return true;

		for (int i = from; i < to; i++)
		{
			int code = text.Substring(i, 1).ToAscii();
			if (code >= 0 && code < 128)
				return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! "0.00 Ŧ", or no figure at all.
	static bool IsZeroMoney(string text)
	{
		return MoneyTextToCents(text) == 0;
	}

	//------------------------------------------------------------------------------------------------
	//! For fee terms such as "1.00 Ŧ + 2 %": free only if no part of it is above zero.
	static bool IsFreeOfCharge(string terms)
	{
		for (int i = 0; i < terms.Length(); i++)
		{
			if (NONZERO_DIGITS.Contains(terms.Substring(i, 1)))
				return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	static string FormatMoney(int cents)
	{
		bool negative = cents < 0;
		if (negative)
			cents = -cents;

		int major = cents / 100;
		int minor = cents % 100;
		string minorText = minor.ToString();
		if (minor < 10)
			minorText = "0" + minorText;

		string sign = "";
		if (negative)
			sign = "-";

		//! Unit suffixed, not prefixed, so the sign stays leading and figures keep a common left edge.
		return sign + InsertThousands(major) + "." + minorText + " " + ELIFE_PhoneStyle.CURRENCY_SYMBOL;
	}

	//------------------------------------------------------------------------------------------------
	//! Groups of four, the way an IBAN is printed.
	static string GroupIban(string iban)
	{
		string grouped = "";
		for (int i = 0; i < iban.Length(); i += IBAN_GROUP_LENGTH)
		{
			if (i > 0)
				grouped += " ";

			int length = iban.Length() - i;
			if (length > IBAN_GROUP_LENGTH)
				length = IBAN_GROUP_LENGTH;

			grouped += iban.Substring(i, length);
		}

		return grouped;
	}

	//------------------------------------------------------------------------------------------------
	//! For amounts that can only be whole tolars, such as cash.
	static string FormatWholeMoney(int tolars)
	{
		return InsertThousands(tolars) + " " + ELIFE_PhoneStyle.CURRENCY_SYMBOL;
	}

	//------------------------------------------------------------------------------------------------
	protected static string InsertThousands(int major)
	{
		if (major < 1000)
			return major.ToString();

		string result = "";
		while (major >= 1000)
		{
			int group = major % 1000;
			major = major / 1000;

			string groupText = group.ToString();
			if (group < 10)
				groupText = "00" + groupText;
			else if (group < 100)
				groupText = "0" + groupText;

			if (result == "")
				result = groupText;
			else
				result = groupText + " " + result;
		}

		return major.ToString() + " " + result;
	}
}
