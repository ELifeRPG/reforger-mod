//------------------------------------------------------------------------------------------------
class ELIFE_JoinCharacterEntry : JsonApiStruct
{
	string id;
	string name;

	//------------------------------------------------------------------------------------------------
	void ELIFE_JoinCharacterEntry()
	{
		RegV("id");
		RegV("name");
	}
}

//------------------------------------------------------------------------------------------------
class ELIFE_JoinSelectPayload : JsonApiStruct
{
	ref array<ref ELIFE_JoinCharacterEntry> characters = {};
	string error;

	//------------------------------------------------------------------------------------------------
	void ELIFE_JoinSelectPayload()
	{
		RegV("characters");
		RegV("error");
	}
}

//------------------------------------------------------------------------------------------------
class ELIFE_JoinCreatePayload : JsonApiStruct
{
	//! int, not bool: bools pack as 1 or 0 (docs/backend-api.md).
	int canGoBack;
	string error;
	string firstName;
	string lastName;
	int appearanceCount;

	//------------------------------------------------------------------------------------------------
	void ELIFE_JoinCreatePayload()
	{
		RegV("canGoBack");
		RegV("error");
		RegV("firstName");
		RegV("lastName");
		RegV("appearanceCount");
	}
}

//------------------------------------------------------------------------------------------------
class ELIFE_JoinLinkPayload : JsonApiStruct
{
	string url;
	string code;
	//! Localisation key, or "" for no hint.
	string hint;

	//------------------------------------------------------------------------------------------------
	void ELIFE_JoinLinkPayload()
	{
		RegV("url");
		RegV("code");
		RegV("hint");
	}
}

//------------------------------------------------------------------------------------------------
class ELIFE_JoinBanPayload : JsonApiStruct
{
	string reason;
	string until;
	string appealUrl;

	//------------------------------------------------------------------------------------------------
	void ELIFE_JoinBanPayload()
	{
		RegV("reason");
		RegV("until");
		RegV("appealUrl");
	}
}

//------------------------------------------------------------------------------------------------
class ELIFE_JoinWhitelistPayload : JsonApiStruct
{
	string url;

	//------------------------------------------------------------------------------------------------
	void ELIFE_JoinWhitelistPayload()
	{
		RegV("url");
	}
}

//------------------------------------------------------------------------------------------------
class ELIFE_JoinErrorPayload : JsonApiStruct
{
	string messageKey;

	//------------------------------------------------------------------------------------------------
	void ELIFE_JoinErrorPayload()
	{
		RegV("messageKey");
	}
}
