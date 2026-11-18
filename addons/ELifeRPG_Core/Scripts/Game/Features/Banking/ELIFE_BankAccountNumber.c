//------------------------------------------------------------------------------------------------
//! Account numbers as the backend issues them: EL, two MOD-97 check digits, a four-letter bank code and ten digits.
class ELIFE_BankAccountNumber
{
	static const string COUNTRY = "EL";
	static const int LENGTH = 18;
	protected static const int BANK_CODE_START = 4;
	protected static const int ACCOUNT_START = 8;
	protected static const int CHECK_MODULUS = 97;
	//! In MOD-97, A counts as 10, B as 11 and so on.
	protected static const int LETTER_OFFSET = 55;

	//------------------------------------------------------------------------------------------------
	//! Without spaces and in capitals, the way players may type it from a grouped display.
	static string Normalize(string text)
	{
		string number = text;
		number.Replace(" ", "");
		number.ToUpper();
		return number;
	}

	//------------------------------------------------------------------------------------------------
	//! The four letters naming the bank, "" if the number is too short.
	static string GetBankCode(string number)
	{
		if (number.Length() < ACCOUNT_START)
			return "";

		return number.Substring(BANK_CODE_START, ACCOUNT_START - BANK_CODE_START);
	}

	//------------------------------------------------------------------------------------------------
	//! The shape and the check digits; whether the account exists is the backend's to say.
	static bool IsValid(string number)
	{
		if (number.Length() != LENGTH || !number.StartsWith(COUNTRY))
			return false;

		for (int i = 2; i < LENGTH; i++)
		{
			bool letter = i >= BANK_CODE_START && i < ACCOUNT_START;
			if (letter != IsLetter(number.Get(i)) || (!letter && !IsDigit(number.Get(i))))
				return false;
		}

		string rearranged = number.Substring(BANK_CODE_START, LENGTH - BANK_CODE_START) + number.Substring(0, BANK_CODE_START);
		int remainder;
		for (int i = 0; i < rearranged.Length(); i++)
		{
			string character = rearranged.Get(i);
			if (IsDigit(character))
				remainder = (remainder * 10 + character.ToInt()) % CHECK_MODULUS;
			else
				remainder = (remainder * 100 + character.ToAscii() - LETTER_OFFSET) % CHECK_MODULUS;
		}

		return remainder == 1;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsDigit(string character)
	{
		int code = character.ToAscii();
		return code >= 48 && code <= 57;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsLetter(string character)
	{
		int code = character.ToAscii();
		return code >= 65 && code <= 90;
	}
}
