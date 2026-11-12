//------------------------------------------------------------------------------------------------
/*!
	Server-side character name rules (spec 5.6). The UI may mirror them but is never trusted.
	Each part is 2-20 characters: ASCII letters, '-' and ''', starting with a letter, no "--" or "''".
*/
class ELIFE_CharacterNameValidator
{
	protected const int MIN_PART_LENGTH = 2;
	protected const int MAX_PART_LENGTH = 20;

	//------------------------------------------------------------------------------------------------
	//! Trims and removes '<' and '>' (rich-text remnants).
	static string CleanPart(string part)
	{
		string cleaned = part;
		cleaned.Replace("<", "");
		cleaned.Replace(">", "");
		return cleaned.Trim();
	}

	//------------------------------------------------------------------------------------------------
	//! Checks an already cleaned part. Returns false with a localisation key in errorKey.
	static bool ValidatePart(string part, out string errorKey)
	{
		errorKey = "";

		int length = part.Length();
		if (length < MIN_PART_LENGTH)
		{
			errorKey = "#ELIFE-Join_NameTooShort";
			return false;
		}

		if (length > MAX_PART_LENGTH)
		{
			errorKey = "#ELIFE-Join_NameTooLong";
			return false;
		}

		if (!IsLetter(part.ToAscii(0)))
		{
			errorKey = "#ELIFE-Join_NameInvalid";
			return false;
		}

		for (int i = 0; i < length; i++)
		{
			if (!IsAllowedChar(part.ToAscii(i)))
			{
				errorKey = "#ELIFE-Join_NameInvalid";
				return false;
			}
		}

		if (part.Contains("--") || part.Contains("''"))
		{
			errorKey = "#ELIFE-Join_NameInvalid";
			return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Upper-cases the first letter and leaves the rest unchanged.
	static string Capitalize(string part)
	{
		if (part.IsEmpty())
			return part;

		string head = part.Substring(0, 1);
		head.ToUpper();
		return head + part.Substring(1, part.Length() - 1);
	}

	//------------------------------------------------------------------------------------------------
	//! Cleans and validates both parts. On success fullName is "First Last".
	static bool Validate(string firstName, string lastName, out string fullName, out string errorKey)
	{
		fullName = "";
		errorKey = "";

		string first = CleanPart(firstName);
		string last = CleanPart(lastName);

		string partKey;
		if (!ValidatePart(first, partKey))
		{
			errorKey = partKey;
			return false;
		}

		if (!ValidatePart(last, partKey))
		{
			errorKey = partKey;
			return false;
		}

		fullName = Capitalize(first) + " " + Capitalize(last);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsLetter(int code)
	{
		return (code >= 65 && code <= 90) || (code >= 97 && code <= 122);
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsAllowedChar(int code)
	{
		return IsLetter(code) || code == 45 || code == 39;
	}
}
