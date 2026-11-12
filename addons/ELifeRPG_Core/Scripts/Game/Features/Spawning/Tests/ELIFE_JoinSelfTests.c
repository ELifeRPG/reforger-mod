#ifdef WORKBENCH
//------------------------------------------------------------------------------------------------
/*!
	Workbench-only self-tests for the join flow. Run from Plugins -> ELIFE -> Run Join Self-Tests.
	Prints `ELIFE_SelfTest | PASS <name>` / `FAIL <name>: <why>` per check and a final DONE line.
	Add new tests as `protected static void Test_<Name>()` and call them from RunAll().
*/
class ELIFE_JoinSelfTests
{
	protected static int s_iPass;
	protected static int s_iFail;

	//------------------------------------------------------------------------------------------------
	static void RunAll()
	{
		s_iPass = 0;
		s_iFail = 0;

		Test_NameValidator_Accepts();
		Test_NameValidator_Rejects();
		Test_TransitionTable();
		Test_SelectPayloadRoundTrip();
		Test_WorkbenchId_Format();
		Test_WorkbenchId_SameMachineSameId();
		Test_WorkbenchId_PeerNumberSuffix();
		Test_WorkbenchId_EmptyMachineName();
		Test_WorkbenchId_DifferentMachinesDiffer();
		Test_Identity_MapPlatformKindName();
		Test_Registry_StoredValueAndPlatform();
		Test_Registry_FindPlayerIdReverseLookup();
		Test_Registry_UnknownIdEmpty();
		Test_Registry_UnregisterRemoves();
		Test_Registry_OverwriteReplaces();
		Test_Registry_RegisterMatchesResolve();
		Test_Qr_ReedSolomonVector();
		Test_Qr_FormatBits();
		Test_Qr_VersionBits();
		Test_Qr_FinderAndTiming();
		Test_Qr_OwnerUrlMatrix();
		Test_Qr_VersionChoiceAndLimit();
		Test_Qr_LongStringVersion13();
		Test_Qr_CodewordCounts();

		ELIFE_JoinSessionManagerComponent mgr = ELIFE_JoinSessionManagerComponent.GetInstance();
		if (mgr)
		{
			// Create tests need one valid appearance; a world with none configured gets a temporary one, restored at the end.
			array<ResourceName> savedAppearance = mgr.m_aAppearancePrefabs;
			if (mgr.GetAppearanceCount() == 0)
			{
				array<ResourceName> tempAppearance = new array<ResourceName>();
				tempAppearance.Insert("{9FA4749AF6C55D96}Prefabs/Characters/Factions/LIFE/Character_LIFE.et");
				mgr.m_aAppearancePrefabs = tempAppearance;
			}

			Test_AcceptResponse_RejectsStaleToken();
			Test_SetState_RefusesIllegalMove();
			Test_OpenCreate_IgnoredInCreating();
			Test_InFlight_BlocksSecondRequest();
			Test_InFlight_ClearedRequestAccepted();
			Test_Tick_KicksRejectedAtDeadline();
			Test_Tick_KicksIdleSelecting();
			Test_RequestTimeout_LateReplyIgnored();
			Test_Tick_KicksAcrossClockWrap();
			Test_Connect_BlockedRejectsBanned();
			Test_Connect_NotWhitelistedRejects();
			Test_Connect_ActiveNoCharactersCreates();
			Test_Connect_UnlinkedSchedulesRetry();
			Test_Connect_UnlinkedRetryFires();
			Test_Connect_UnlinkedRetriesExhaust();
			Test_Connect_UnlinkedWaitNotTimedOut();
			Test_Recheck_InLinkingGoesPending();
			Test_Recheck_UnlinkedAgainSchedulesRetry();
			Test_Recheck_ActiveNoCharactersCreates();
			Test_Recheck_ErrorBackToLinking();
			Test_Recheck_TimeoutBackToLinking();
			Test_Recheck_IgnoredWhenNotLinking();
			Test_Recheck_CooldownIgnoresEarly();
			Test_Recheck_LinkedWithCharactersSelects();
			Test_Recheck_StillNotLinkedShowsNotYet();
			Test_Recheck_FailureKeepsLastLink();
			Test_SkipLink_NoCharactersCreates();
			Test_SkipLink_WithCharactersSelects();
			Test_SkipLink_IgnoredOutsideLinking();
			Test_Tick_KicksIdleLinking();
			Test_AckClosed_LinkingKicks();
			Test_AckClosed_RecheckKicks();
			Test_AckClosed_CreatingRefused();
			Test_Connect_ErrorStatusRejects();
			Test_Connect_NullDataRejects();
			Test_Connect_UnknownStatusProceedsAsActive();
			Test_Connect_UnknownStatusWithCharactersSelects();
			Test_Connect_ActiveNotLinkedShowsLinking();
			Test_Connect_ActiveLinkedWithCharactersSelects();
			Test_Connect_NotLinkedNoUrlGoesToCharacters();
			Test_Connect_StaleTokenIgnored();
			Test_Connect_ParseStatusString();
			Test_DuplicateClear_ReplyStartsConnect();
			Test_AwaitApi_WaitsInsteadOfReject();
			Test_AwaitApi_DeadlineRejects();
			Test_AwaitApi_ResumesWhenAvailable();
			Test_AwaitApi_EndSessionCleansUp();
			Test_Select_UnknownIdRefused();
			Test_Select_IgnoredInCreating();
			Test_Select_KnownIdLoads();
			Test_Create_ShortNameStaysCreating();
			Test_Create_AppearanceOutOfRangeRefused();
			Test_Create_SecondRequestIgnoredInFlight();
			Test_Create_SuccessSelectsNewCharacter();
			Test_Create_ErrorShowsCreateAgain();
			Test_CharacterSelected_ErrorReturnsToSelecting();
			Test_NameFilter_MismatchRefused();
			Test_NameFilter_StaleAnswerDropped();
			Test_EntityLost_InGameToSpawning();
			Test_EntityLost_IgnoredInSelecting();
			Test_SpawnFailed_ThreeTimesRejects();
			Test_Appearance_OutOfRangeIsDefault();
			Test_ConfirmedCharacter_UnconfirmedStatesEmpty();
			Test_ConfirmedCharacter_ConfirmedStatesGiveId();
			Test_ConfirmedCharacter_NoSessionEmpty();

			mgr.ClearTestSessions();
			mgr.m_aAppearancePrefabs = savedAppearance;
		}
		else
		{
			Print("ELIFE_SelfTest | SKIP manager tests: no game mode (run in play mode)", LogLevel.NORMAL);
		}

		Print(string.Format("ELIFE_SelfTest | DONE pass=%1 fail=%2", s_iPass, s_iFail), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected static void Check(bool cond, string name, string why)
	{
		if (cond)
		{
			s_iPass++;
			Print("ELIFE_SelfTest | PASS " + name, LogLevel.NORMAL);
		}
		else
		{
			s_iFail++;
			Print("ELIFE_SelfTest | FAIL " + name + ": " + why, LogLevel.NORMAL);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void ExpectReject(string firstName, string lastName, string expectedKey, string name)
	{
		string fullName;
		string errorKey;
		bool ok = ELIFE_CharacterNameValidator.Validate(firstName, lastName, fullName, errorKey);
		Check(!ok && errorKey == expectedKey, name, string.Format("ok=%1 key=%2", ok, errorKey));
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_NameValidator_Accepts()
	{
		string fullName;
		string errorKey;
		bool ok;

		ok = ELIFE_CharacterNameValidator.Validate("anna", "smith", fullName, errorKey);
		Check(ok && fullName == "Anna Smith", "name: anna smith", string.Format("ok=%1 full=%2", ok, fullName));

		ok = ELIFE_CharacterNameValidator.Validate("Jean-Luc", "O'Neil", fullName, errorKey);
		Check(ok && fullName == "Jean-Luc O'Neil", "name: Jean-Luc O'Neil", string.Format("ok=%1 full=%2", ok, fullName));

		ok = ELIFE_CharacterNameValidator.Validate("<An>na", "Smith", fullName, errorKey);
		Check(ok && fullName == "Anna Smith", "name: <An>na is cleaned", string.Format("ok=%1 full=%2", ok, fullName));
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_NameValidator_Rejects()
	{
		ExpectReject("a", "Smith", "#ELIFE-Join_NameTooShort", "name: one letter is too short");
		ExpectReject("", "Smith", "#ELIFE-Join_NameTooShort", "name: empty is too short");
		ExpectReject("Abcdefghijklmnopqrstu", "Smith", "#ELIFE-Join_NameTooLong", "name: 21 letters is too long");
		ExpectReject("Jean--Luc", "X1", "#ELIFE-Join_NameInvalid", "name: double hyphen and digit invalid");
		ExpectReject("-Anna", "Smith", "#ELIFE-Join_NameInvalid", "name: leading hyphen invalid");
		ExpectReject("Anna", "Sm1th", "#ELIFE-Join_NameInvalid", "name: digit invalid");
		ExpectReject("Anna", "Müller", "#ELIFE-Join_NameInvalid", "name: non-ASCII letter invalid");
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_TransitionTable()
	{
		Check(ELIFE_JoinSession.IsTransitionAllowed(ELIFE_EJoinState.NONE, ELIFE_EJoinState.SESSION_PENDING),
			"transition NONE->SESSION_PENDING allowed", "expected true");
		Check(!ELIFE_JoinSession.IsTransitionAllowed(ELIFE_EJoinState.CREATING, ELIFE_EJoinState.SPAWNING),
			"transition CREATING->SPAWNING blocked", "expected false");
		Check(!ELIFE_JoinSession.IsTransitionAllowed(ELIFE_EJoinState.ENDED, ELIFE_EJoinState.ENDED),
			"transition ENDED->ENDED blocked", "expected false");
		Check(ELIFE_JoinSession.IsTransitionAllowed(ELIFE_EJoinState.IN_GAME, ELIFE_EJoinState.SPAWNING),
			"transition IN_GAME->SPAWNING allowed", "expected true");
		Check(!ELIFE_JoinSession.IsTransitionAllowed(ELIFE_EJoinState.REJECTED, ELIFE_EJoinState.CREATING),
			"transition REJECTED->CREATING blocked", "expected false");
		Check(ELIFE_JoinSession.IsTransitionAllowed(ELIFE_EJoinState.IN_GAME, ELIFE_EJoinState.ENDED),
			"transition IN_GAME->ENDED allowed", "expected true");
		Check(ELIFE_JoinSession.IsTransitionAllowed(ELIFE_EJoinState.SESSION_PENDING, ELIFE_EJoinState.LINKING),
			"transition SESSION_PENDING->LINKING allowed", "expected true");
		Check(ELIFE_JoinSession.IsTransitionAllowed(ELIFE_EJoinState.LINKING, ELIFE_EJoinState.SESSION_PENDING),
			"transition LINKING->SESSION_PENDING allowed", "expected true");
		Check(ELIFE_JoinSession.IsTransitionAllowed(ELIFE_EJoinState.LINKING, ELIFE_EJoinState.REJECTED),
			"transition LINKING->REJECTED allowed", "expected true");
		Check(ELIFE_JoinSession.IsTransitionAllowed(ELIFE_EJoinState.LINKING, ELIFE_EJoinState.ENDED),
			"transition LINKING->ENDED allowed", "expected true");
		Check(ELIFE_JoinSession.IsTransitionAllowed(ELIFE_EJoinState.LINKING, ELIFE_EJoinState.CREATING),
			"transition LINKING->CREATING allowed (continue without linking)", "expected true");
		Check(ELIFE_JoinSession.IsTransitionAllowed(ELIFE_EJoinState.LINKING, ELIFE_EJoinState.SELECTING),
			"transition LINKING->SELECTING allowed (continue without linking)", "expected true");
		Check(!ELIFE_JoinSession.IsTransitionAllowed(ELIFE_EJoinState.LINKING, ELIFE_EJoinState.SPAWNING),
			"transition LINKING->SPAWNING blocked", "expected false");
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_SelectPayloadRoundTrip()
	{
		ELIFE_JoinSelectPayload src = new ELIFE_JoinSelectPayload();
		src.characters.Insert(MakeEntry("c-1", "Anna Smith"));
		src.characters.Insert(MakeEntry("c-2", "Jean-Luc O'Neil"));
		src.Pack();
		string json = src.AsString();

		ELIFE_JoinSelectPayload dst = new ELIFE_JoinSelectPayload();
		dst.ExpandFromRAW(json);

		bool ok = dst.characters.Count() == 2;
		if (ok)
			ok = dst.characters[0].id == "c-1" && dst.characters[0].name == "Anna Smith"
				&& dst.characters[1].id == "c-2" && dst.characters[1].name == "Jean-Luc O'Neil";
		Check(ok, "select payload round-trip", string.Format("json=%1", json));
	}

	//------------------------------------------------------------------------------------------------
	//! 36 characters, dashes at 8, 13, 18 and 23, fixed prefix `00000000-0000-`, every other character lowercase hex.
	protected static void Test_WorkbenchId_Format()
	{
		string id = ELIFE_JoinIdentity.BuildWorkbenchId("DESKTOP-ABC", 0);
		bool shapeOk = id.Length() == 36 && id.Substring(8, 1) == "-" && id.Substring(13, 1) == "-"
			&& id.Substring(18, 1) == "-" && id.Substring(23, 1) == "-" && id.IndexOf("00000000-0000-") == 0;
		if (shapeOk)
		{
			for (int i = 0; i < 36; i++)
			{
				if (i == 8 || i == 13 || i == 18 || i == 23)
					continue;

				if (!"0123456789abcdef".Contains(id.Substring(i, 1)))
					shapeOk = false;
			}
		}

		Check(shapeOk, "workbench id: 36-char low GUID, lowercase hex", string.Format("id=%1", id));
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_WorkbenchId_SameMachineSameId()
	{
		string first = ELIFE_JoinIdentity.BuildWorkbenchId("DESKTOP-ABC", 0);
		string second = ELIFE_JoinIdentity.BuildWorkbenchId("DESKTOP-ABC", 0);

		Check(first == second, "workbench id: same machine gives the same id", string.Format("first=%1 second=%2", first, second));
	}

	//------------------------------------------------------------------------------------------------
	//! Peer number is decimal, left-padded to 12 digits.
	protected static void Test_WorkbenchId_PeerNumberSuffix()
	{
		string three = ELIFE_JoinIdentity.BuildWorkbenchId("DESKTOP-ABC", 3);
		string ten = ELIFE_JoinIdentity.BuildWorkbenchId("DESKTOP-ABC", 10);

		Check(three.IndexOf("000000000003") == 24 && three.Length() == 36, "workbench id: peer 3 ends with 000000000003", string.Format("id=%1", three));
		Check(ten.IndexOf("000000000010") == 24 && ten.Length() == 36, "workbench id: peer 10 ends with 000000000010", string.Format("id=%1", ten));
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_WorkbenchId_EmptyMachineName()
	{
		string id = ELIFE_JoinIdentity.BuildWorkbenchId("", 0);

		Check(id == "00000000-0000-0000-0000-000000000000", "workbench id: empty machine name gives all zeros", string.Format("id=%1", id));
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_WorkbenchId_DifferentMachinesDiffer()
	{
		string a = ELIFE_JoinIdentity.BuildWorkbenchId("DESKTOP-ABC", 0);
		string b = ELIFE_JoinIdentity.BuildWorkbenchId("DESKTOP-XYZ", 0);

		Check(a != b, "workbench id: different machines give different ids", string.Format("a=%1 b=%2", a, b));
	}

	//------------------------------------------------------------------------------------------------
	//! Engine PlatformKind names to the wire values. Lowercase input is accepted; unknown and empty names give "" (nothing sent).
	protected static void Test_Identity_MapPlatformKindName()
	{
		ExpectPlatform("XBOX", "Xbox", "platform: XBOX maps to Xbox");
		ExpectPlatform("xbox", "Xbox", "platform: lowercase xbox maps to Xbox");
		ExpectPlatform("PSN", "PlayStation", "platform: PSN maps to PlayStation");
		ExpectPlatform("PS5", "PlayStation", "platform: PS5 maps to PlayStation");
		ExpectPlatform("STEAM", "Pc", "platform: STEAM maps to Pc");
		ExpectPlatform("PC", "Pc", "platform: PC maps to Pc");
		ExpectPlatform("NONE", "", "platform: NONE sends nothing");
		ExpectPlatform("", "", "platform: empty name sends nothing");
	}

	//------------------------------------------------------------------------------------------------
	protected static void ExpectPlatform(string kindName, string expected, string name)
	{
		string platform = ELIFE_JoinIdentity.MapPlatformKindName(kindName);
		Check(platform == expected, name, string.Format("kind=%1 got=%2 expected=%3", kindName, platform, expected));
	}

	//------------------------------------------------------------------------------------------------
	//! Identity registry with negative (test) ids: the stored identity and platform are read back, and Has is true.
	protected static void Test_Registry_StoredValueAndPlatform()
	{
		ELIFE_PlayerIdentityRegistry.SetForTests(-901, "id-901", "Pc");

		string identity = ELIFE_PlayerIdentityRegistry.GetIdentityId(-901);
		string platform = ELIFE_PlayerIdentityRegistry.GetPlatform(-901);
		Check(identity == "id-901", "registry: GetIdentityId returns the stored value", string.Format("got=%1", identity));
		Check(platform == "Pc", "registry: GetPlatform returns the stored platform", string.Format("got=%1", platform));
		Check(ELIFE_PlayerIdentityRegistry.Has(-901), "registry: Has is true for a stored id", "expected true");

		ELIFE_PlayerIdentityRegistry.Unregister(-901);
	}

	//------------------------------------------------------------------------------------------------
	//! Reverse lookup returns the player id for a stored identity, and 0 for one that is not stored.
	protected static void Test_Registry_FindPlayerIdReverseLookup()
	{
		ELIFE_PlayerIdentityRegistry.SetForTests(-901, "id-901", "");
		ELIFE_PlayerIdentityRegistry.SetForTests(-902, "id-902", "");

		int found = ELIFE_PlayerIdentityRegistry.FindPlayerId("id-902");
		Check(found == -902, "registry: FindPlayerId finds the player of a stored identity", string.Format("got=%1", found));

		int missing = ELIFE_PlayerIdentityRegistry.FindPlayerId("id-not-stored");
		Check(missing == 0, "registry: FindPlayerId gives 0 for an unknown identity", string.Format("got=%1", missing));

		ELIFE_PlayerIdentityRegistry.Unregister(-901);
		ELIFE_PlayerIdentityRegistry.Unregister(-902);
	}

	//------------------------------------------------------------------------------------------------
	//! A player id with nothing registered gives "" for identity and platform, and Has is false.
	protected static void Test_Registry_UnknownIdEmpty()
	{
		string identity = ELIFE_PlayerIdentityRegistry.GetIdentityId(-903);
		string platform = ELIFE_PlayerIdentityRegistry.GetPlatform(-903);
		Check(identity == "", "registry: unknown id gives empty identity", string.Format("got=%1", identity));
		Check(platform == "", "registry: unknown id gives empty platform", string.Format("got=%1", platform));
		Check(!ELIFE_PlayerIdentityRegistry.Has(-903), "registry: Has is false for an unknown id", "expected false");
	}

	//------------------------------------------------------------------------------------------------
	//! Unregister removes the identity and the platform, so Has is false afterwards.
	protected static void Test_Registry_UnregisterRemoves()
	{
		ELIFE_PlayerIdentityRegistry.SetForTests(-901, "id-901", "Xbox");
		ELIFE_PlayerIdentityRegistry.Unregister(-901);

		Check(!ELIFE_PlayerIdentityRegistry.Has(-901), "registry: Unregister makes Has false", "expected false");
		Check(ELIFE_PlayerIdentityRegistry.GetIdentityId(-901) == "", "registry: Unregister clears the identity", "expected empty");
		Check(ELIFE_PlayerIdentityRegistry.GetPlatform(-901) == "", "registry: Unregister clears the platform", "expected empty");
		Check(ELIFE_PlayerIdentityRegistry.FindPlayerId("id-901") == 0, "registry: Unregister removes the reverse lookup", "expected 0");
	}

	//------------------------------------------------------------------------------------------------
	//! A second SetForTests on the same player id replaces the identity and the platform.
	protected static void Test_Registry_OverwriteReplaces()
	{
		ELIFE_PlayerIdentityRegistry.SetForTests(-901, "id-old", "Pc");
		ELIFE_PlayerIdentityRegistry.SetForTests(-901, "id-new", "PlayStation");

		Check(ELIFE_PlayerIdentityRegistry.GetIdentityId(-901) == "id-new", "registry: overwrite replaces the identity", "expected id-new");
		Check(ELIFE_PlayerIdentityRegistry.GetPlatform(-901) == "PlayStation", "registry: overwrite replaces the platform", "expected PlayStation");

		ELIFE_PlayerIdentityRegistry.Unregister(-901);
	}

	//------------------------------------------------------------------------------------------------
	//! Register with a negative id resolves through ELIFE_JoinIdentity (pure in Workbench) and stores the same identity.
	protected static void Test_Registry_RegisterMatchesResolve()
	{
		string expected = ELIFE_JoinIdentity.Resolve(-904);
		string registered = ELIFE_PlayerIdentityRegistry.Register(-904);
		string stored = ELIFE_PlayerIdentityRegistry.GetIdentityId(-904);

		Check(!registered.IsEmpty() && registered == expected, "registry: Register returns the resolved identity", string.Format("registered=%1 expected=%2", registered, expected));
		Check(stored == expected, "registry: GetIdentityId returns the registered identity", string.Format("stored=%1 expected=%2", stored, expected));

		ELIFE_PlayerIdentityRegistry.Unregister(-904);
	}

	//------------------------------------------------------------------------------------------------
	//! Owner link the QR self-tests encode. The matrix pin in Test_Qr_OwnerUrlMatrix belongs to this exact string.
	protected static string QrOwnerUrl()
	{
		return "http://localhost:8180/realms/eliferpg/protocol/openid-connect/auth?client_id=account&response_type=code&kc_action=link-bohemia-gameaccount";
	}

	//------------------------------------------------------------------------------------------------
	//! Bits of value, MSB first, width characters long.
	protected static string BitString(int value, int width)
	{
		string bits = "";
		for (int i = width - 1; i >= 0; i--)
		{
			if (((value >> i) & 1) == 1)
				bits = bits + "1";
			else
				bits = bits + "0";
		}
		return bits;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool SameInts(array<int> a, array<int> b)
	{
		if (a.Count() != b.Count())
			return false;
		for (int i = 0; i < a.Count(); i++)
		{
			if (a.Get(i) != b.Get(i))
				return false;
		}
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! ISO/IEC 18004 worked example (Thonky): 16 data codewords, 10 ECC codewords over GF(256) 0x11D.
	protected static void Test_Qr_ReedSolomonVector()
	{
		array<int> data = new array<int>();
		data.Insert(32);
		data.Insert(91);
		data.Insert(11);
		data.Insert(120);
		data.Insert(209);
		data.Insert(114);
		data.Insert(220);
		data.Insert(77);
		data.Insert(67);
		data.Insert(64);
		data.Insert(236);
		data.Insert(17);
		data.Insert(236);
		data.Insert(17);
		data.Insert(236);
		data.Insert(17);

		array<int> expected = new array<int>();
		expected.Insert(196);
		expected.Insert(35);
		expected.Insert(39);
		expected.Insert(119);
		expected.Insert(235);
		expected.Insert(215);
		expected.Insert(231);
		expected.Insert(226);
		expected.Insert(93);
		expected.Insert(23);

		array<int> ecc = ELIFE_QrCode.ReedSolomonEcc(data, 10);
		Check(SameInts(ecc, expected), "qr: Reed-Solomon matches the spec example", string.Format("first=%1", ecc.Get(0)));
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_Qr_FormatBits()
	{
		// ECC M (00) with mask 0 and mask 5
		string mask0 = BitString(ELIFE_QrCode.FormatBits(0), 15);
		string mask5 = BitString(ELIFE_QrCode.FormatBits(5), 15);

		Check(mask0 == "101010000010010", "qr: format bits M mask 0", "bits=" + mask0);
		Check(mask5 == "100000011001110", "qr: format bits M mask 5", "bits=" + mask5);
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_Qr_VersionBits()
	{
		string v7 = BitString(ELIFE_QrCode.VersionBits(7), 18);

		Check(v7 == "000111110010010100", "qr: version 7 info bits", "bits=" + v7);
	}

	//------------------------------------------------------------------------------------------------
	//! Owner link is version 8 (size 17 + 4 * 8 = 49), and the top-left finder and timing patterns are in place.
	protected static void Test_Qr_FinderAndTiming()
	{
		int size;
		array<bool> modules;
		bool ok = ELIFE_QrCode.Encode(QrOwnerUrl(), size, modules);
		Check(ok && size == 49, "qr: owner url is 49 modules (version 8)", string.Format("ok=%1 size=%2", ok, size));
		if (!ok)
			return;

		bool finderOk = true;
		for (int r = 0; r < 7; r++)
		{
			for (int c = 0; c < 7; c++)
			{
				bool ring = r == 0 || r == 6 || c == 0 || c == 6;
				bool centre = r >= 2 && r <= 4 && c >= 2 && c <= 4;
				if (modules.Get(r * size + c) != (ring || centre))
					finderOk = false;
			}
		}
		for (int i = 0; i < 8; i++)
		{
			if (modules.Get(7 * size + i) || modules.Get(i * size + 7))
				finderOk = false;
		}
		Check(finderOk, "qr: top-left finder ring, centre and light separators", "");

		bool timingOk = true;
		for (int i = 8; i < size - 8; i++)
		{
			bool dark = i % 2 == 0;
			if (modules.Get(6 * size + i) != dark || modules.Get(i * size + 6) != dark)
				timingOk = false;
		}
		Check(timingOk, "qr: timing patterns alternate from module 8", "");
		Check(modules.Get((size - 8) * size + 8), "qr: dark module at (4V+9, 8)", "");
	}

	//------------------------------------------------------------------------------------------------
	//! Full-matrix pin: rows of the owner URL matrix from the Python reference, which was scanned with zbarimg (all 8 masks checked).
	protected static void Test_Qr_OwnerUrlMatrix()
	{
		int size;
		array<bool> modules;
		bool ok = ELIFE_QrCode.Encode(QrOwnerUrl(), size, modules);
		if (!ok)
		{
			Check(false, "qr: owner url matrix encodes", "encode failed");
			return;
		}

		array<string> rows = new array<string>();
		rows.Insert("1111111000100010001010011000011100011100101111111");
		rows.Insert("1000001000110101100001100101000101110111101000001");
		rows.Insert("1011101011010011000101111001110010010001101011101");
		rows.Insert("1011101010110010101100001111010101011101001011101");
		rows.Insert("1011101011001110010001111110011001000100001011101");
		rows.Insert("1000001010111111001100100010010000110010001000001");
		rows.Insert("1111111010101010101010101010101010101010101111111");
		rows.Insert("0000000011111010111000100011101010100111000000000");
		rows.Insert("1011111001111000111111111110010100001111101111100");
		rows.Insert("0101000011001100101001011001011100001100011010110");
		rows.Insert("0010111101100100001010001010010101111110000000011");
		rows.Insert("1001110100000100101110000010111011010000110110001");
		rows.Insert("0000111000110110010001111000011000111010110101101");
		rows.Insert("0111110001011000010110110000001000010100001111110");
		rows.Insert("0001011011101001111011110110110001101011100110011");
		rows.Insert("1110010100110011100110100101010110000010001010001");
		rows.Insert("1110111010111111110011011010100100011101110101100");
		rows.Insert("1101010011110010010100001101111110011100011110000");
		rows.Insert("1011101111000110010111110010110111101010100111011");
		rows.Insert("1110110011110000011110000111101111100000111010001");
		rows.Insert("0101101100101110100011111010000101111101111000110");
		rows.Insert("1011110000010101100100001111011101010100011011010");
		rows.Insert("1000111110100001111111111110000010110010111111101");
		rows.Insert("1001100010100010011110100010111011000000100010001");
		rows.Insert("0101101011011010111011101010001100111000101011111");
		rows.Insert("1100100011011111011100100010001010011100100011010");
		rows.Insert("0110111110010100000000111110000111100011111110011");
		rows.Insert("1011000110100101000001010001110110010100100010010");
		rows.Insert("0111111101110100100100001110011000011101110011110");
		rows.Insert("1111110011111101001001110111011000010101110100100");
		rows.Insert("0110111010100110111100100100100010010111011101111");
		rows.Insert("0001110101011111000001110010011011000000000110000");
		rows.Insert("1101001001001001011011011111001100011101010011100");
		rows.Insert("1100100101101101010101111110001110010001110100000");
		rows.Insert("1101001000010010001101010101001001111011011111011");
		rows.Insert("1010010111011011011011101011111111100110000000011");
		rows.Insert("0100011110000001011101100010011101011001010010110");
		rows.Insert("0101100000010101010101111110111001001101110100100");
		rows.Insert("0100011100000111000000110100100100101010011000111");
		rows.Insert("0111000101001011110100101010110010100100100100011");
		rows.Insert("1110001001111111100110111110010100111101111110100");
		rows.Insert("0000000010011101011100100010011010000101100011000");
		rows.Insert("1111111000100011010011101011000111110011101010101");
		rows.Insert("1000001011100011011001100011101110100000100010010");
		rows.Insert("1011101010100110110000111110010000111110111111111");
		rows.Insert("1011101010010110110110100000011100001101110111101");
		rows.Insert("1011101011000000111010011101000000100110000000100");
		rows.Insert("1000001001011010111111011001010011000111011001001");
		rows.Insert("1111111011000000111010011100100100111101110001111");

		Check(rows.Count() == size, "qr: owner url pin has one row per module row", string.Format("pin=%1 size=%2", rows.Count(), size));

		int mismatches = 0;
		for (int r = 0; r < size; r++)
		{
			string row = rows.Get(r);
			for (int c = 0; c < size; c++)
			{
				bool expectDark = row.ToAscii(c) == 49;
				if (modules.Get(r * size + c) != expectDark)
					mismatches++;
			}
		}
		Check(mismatches == 0, "qr: owner url matrix equals the pinned reference", string.Format("mismatched modules=%1", mismatches));
	}

	//------------------------------------------------------------------------------------------------
	//! Text of count copies of unit (plain ASCII, one byte per character).
	protected static string RepeatedText(string unit, int count)
	{
		string text = "";
		for (int i = 0; i < count; i++)
		{
			text = text + unit;
		}
		return text;
	}

	//------------------------------------------------------------------------------------------------
	//! Smallest version that fits, and the byte limits: 213 bytes fit version 10 and 214 need version 11; 666 bytes fit version 20 and 667 is refused.
	protected static void Test_Qr_VersionChoiceAndLimit()
	{
		int size;
		array<bool> modules;

		ELIFE_QrCode.Encode("A", size, modules);
		Check(size == 21, "qr: one character uses version 1", string.Format("size=%1", size));

		bool v10Fits = ELIFE_QrCode.Encode(RepeatedText("x", 213), size, modules);
		Check(v10Fits && size == 57, "qr: 213 bytes fit version 10", string.Format("fits=%1 size=%2", v10Fits, size));

		bool v11Fits = ELIFE_QrCode.Encode(RepeatedText("x", 214), size, modules);
		Check(v11Fits && size == 61, "qr: 214 bytes need version 11", string.Format("fits=%1 size=%2", v11Fits, size));

		bool v20Fits = ELIFE_QrCode.Encode(RepeatedText("x", 666), size, modules);
		Check(v20Fits && size == 97, "qr: 666 bytes fit version 20", string.Format("fits=%1 size=%2", v20Fits, size));

		bool tooLong = ELIFE_QrCode.Encode(RepeatedText("x", 667), size, modules);
		Check(!tooLong && size == 0 && modules.Count() == 0, "qr: 667 bytes are refused", string.Format("tooLong=%1 size=%2", tooLong, size));
	}

	//------------------------------------------------------------------------------------------------
	//! A 300-byte link needs version 13. Version 12 holds 287 bytes ((290 * 8 - 20) / 8, 20 bits of mode and count); version 13 holds 331 ((334 * 8 - 20) / 8).
	protected static void Test_Qr_LongStringVersion13()
	{
		string text = "https://example.com/" + RepeatedText("a", 280);
		int size;
		array<bool> modules;
		bool ok = ELIFE_QrCode.Encode(text, size, modules);
		Check(ok && text.Length() == 300 && size == 69 && modules.Count() == 69 * 69,
			"qr: 300 bytes encode as version 13 (size 69)",
			string.Format("ok=%1 len=%2 size=%3 count=%4", ok, text.Length(), size, modules.Count()));
	}

	//------------------------------------------------------------------------------------------------
	//! Interleaved codeword count per version at ECC M (the total codewords of the spec table), versions 1-20.
	protected static void Test_Qr_CodewordCounts()
	{
		array<int> totals = new array<int>();
		totals.Insert(26);
		totals.Insert(44);
		totals.Insert(70);
		totals.Insert(100);
		totals.Insert(134);
		totals.Insert(172);
		totals.Insert(196);
		totals.Insert(242);
		totals.Insert(292);
		totals.Insert(346);
		totals.Insert(404);
		totals.Insert(466);
		totals.Insert(532);
		totals.Insert(581);
		totals.Insert(655);
		totals.Insert(733);
		totals.Insert(815);
		totals.Insert(901);
		totals.Insert(991);
		totals.Insert(1085);

		array<int> one = new array<int>();
		one.Insert(65);

		bool ok = true;
		for (int v = 1; v <= 20; v++)
		{
			if (ELIFE_QrCode.BuildCodewords(one, v).Count() != totals.Get(v - 1))
				ok = false;
		}
		Check(ok, "qr: codeword count matches the spec table for versions 1-20", "");
	}

	//------------------------------------------------------------------------------------------------
	protected static ELIFE_JoinCharacterEntry MakeEntry(string id, string name)
	{
		ELIFE_JoinCharacterEntry entry = new ELIFE_JoinCharacterEntry();
		entry.id = id;
		entry.name = name;
		return entry;
	}

	//------------------------------------------------------------------------------------------------
	//! Manager from the game mode. Null outside play mode; RunAll only calls the manager tests when it is non-null, so no FAIL is printed here.
	protected static ELIFE_JoinSessionManagerComponent RequireManager(string name)
	{
		return ELIFE_JoinSessionManagerComponent.GetInstance();
	}

	//------------------------------------------------------------------------------------------------
	//! Hand-built session in the given state. Negative player ids keep Push and kicks off the real controller.
	protected static ELIFE_JoinSession MakeSession(int playerId, string sessionId, ELIFE_EJoinState state, int nowMs)
	{
		ELIFE_JoinSession s = new ELIFE_JoinSession();
		s.m_iPlayerId = playerId;
		s.m_sSessionId = sessionId;
		s.m_sIdentityId = "test-identity";
		s.m_eState = state;
		s.m_iLastActivityMs = nowMs;
		s.m_iStateEnteredAtMs = nowMs;
		return s;
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_AcceptResponse_RejectsStaleToken()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("accept: stale token");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		ELIFE_JoinSession s = MakeSession(-201, "t-accept", ELIFE_EJoinState.CREATING, 100000);
		mgr.InsertSessionForTests(s);

		ELIFE_JoinRequestContext first = mgr.BeginRequest(s);
		ELIFE_JoinRequestContext second = mgr.BeginRequest(s);

		ELIFE_JoinSession found;
		bool staleRefused = !mgr.AcceptResponse(first, found);
		bool currentAccepted = mgr.AcceptResponse(second, found) && found == s;

		Check(staleRefused, "accept: old token refused", "stale token was accepted");
		Check(currentAccepted, "accept: current token accepted", "current token was refused");
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_SetState_RefusesIllegalMove()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("setstate: CREATING->SPAWNING");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		ELIFE_JoinSession s = MakeSession(-202, "t-setstate", ELIFE_EJoinState.CREATING, 100000);
		mgr.InsertSessionForTests(s);

		bool moved = mgr.SetState(s, ELIFE_EJoinState.SPAWNING, "test");

		Check(!moved && s.m_eState == ELIFE_EJoinState.CREATING, "setstate: CREATING->SPAWNING refused", string.Format("moved=%1 state=%2", moved, s.m_eState));
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_OpenCreate_IgnoredInCreating()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("open create: ignored in CREATING");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		ELIFE_JoinSession s = MakeSession(-203, "t-opencreate", ELIFE_EJoinState.CREATING, 100000);
		s.m_sTestLastPayload = "sentinel";
		mgr.InsertSessionForTests(s);

		mgr.HandleRequestOpenCreate(-203);

		Check(s.m_eState == ELIFE_EJoinState.CREATING && s.m_sTestLastPayload == "sentinel", "open create: ignored in CREATING", string.Format("state=%1 payload=%2", s.m_eState, s.m_sTestLastPayload));
	}

	//------------------------------------------------------------------------------------------------
	//! A request in flight blocks the next RPC. Only BackToSelect is used to check it, since it has a clear state change.
	protected static void Test_InFlight_BlocksSecondRequest()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("in flight: second request refused");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		ELIFE_JoinSession s = MakeSession(-204, "t-inflight", ELIFE_EJoinState.CREATING, 100000);
		s.m_aCharacters.Insert(MakeEntry("c-1", "Anna Smith"));
		s.m_bRequestInFlight = true;
		mgr.InsertSessionForTests(s);

		mgr.HandleRequestBackToSelect(-204);

		Check(s.m_eState == ELIFE_EJoinState.CREATING, "in flight: BackToSelect refused while in flight", string.Format("state=%1", s.m_eState));
	}

	//------------------------------------------------------------------------------------------------
	//! Same RPC with nothing in flight goes through, so the in-flight test above is not passing by accident.
	protected static void Test_InFlight_ClearedRequestAccepted()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("in flight: BackToSelect accepted when idle");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		ELIFE_JoinSession s = MakeSession(-205, "t-idle-ok", ELIFE_EJoinState.CREATING, 100000);
		s.m_aCharacters.Insert(MakeEntry("c-1", "Anna Smith"));
		mgr.InsertSessionForTests(s);

		mgr.HandleRequestBackToSelect(-205);

		Check(s.m_eState == ELIFE_EJoinState.SELECTING, "in flight: BackToSelect accepted when idle", string.Format("state=%1", s.m_eState));
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_Tick_KicksRejectedAtDeadline()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("tick: rejected kick at deadline");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		ELIFE_JoinSession s = MakeSession(-206, "t-kickat", ELIFE_EJoinState.REJECTED, 1000);
		s.m_iKickAtMs = 5000;
		mgr.InsertSessionForTests(s);

		mgr.SetClockOverrideForTests(4999);
		mgr.Tick();
		Check(!s.m_bTestKickRequested, "tick: no kick one ms before deadline", "kick requested early");

		mgr.SetClockOverrideForTests(5001);
		mgr.Tick();
		Check(s.m_bTestKickRequested, "tick: kick requested at deadline+1", "no kick after deadline");
	}

	//------------------------------------------------------------------------------------------------
	//! Idle is one second past the configured limit in SELECTING, so it is crossed. A session active 1 s ago is kept.
	protected static void Test_Tick_KicksIdleSelecting()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("tick: idle selecting kick");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		int now = 1000000;
		mgr.SetClockOverrideForTests(now);
		int idleMs = Math.Round(mgr.m_fIdleTimeoutSeconds * 1000);

		ELIFE_JoinSession idle = MakeSession(-207, "t-idle", ELIFE_EJoinState.SELECTING, now - idleMs - 1000);
		ELIFE_JoinSession active = MakeSession(-208, "t-active", ELIFE_EJoinState.SELECTING, now - 1000);
		mgr.InsertSessionForTests(idle);
		mgr.InsertSessionForTests(active);

		mgr.Tick();

		Check(idle.m_bTestKickRequested, "tick: SELECTING idle 301 s kicked", "no idle kick");
		Check(!active.m_bTestKickRequested, "tick: SELECTING active 1 s ago not kicked", "active session kicked");
	}

	//------------------------------------------------------------------------------------------------
	//! Past the configured backend deadline, Tick clears the request, so a reply that arrives late matches nothing.
	protected static void Test_RequestTimeout_LateReplyIgnored()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("timeout: late reply ignored");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		int now = 1000000;
		mgr.SetClockOverrideForTests(now);
		ELIFE_JoinSession s = MakeSession(-209, "t-timeout", ELIFE_EJoinState.CREATING, now);
		mgr.InsertSessionForTests(s);

		ELIFE_JoinRequestContext ctx = mgr.BeginRequest(s);

		mgr.SetClockOverrideForTests(now + Math.Round(mgr.m_fBackendTimeoutSeconds * 1000) + 1000);
		mgr.Tick();

		ELIFE_JoinSession found;
		Check(!s.m_bRequestInFlight, "timeout: in-flight cleared", "still in flight");
		Check(!mgr.AcceptResponse(ctx, found), "timeout: late reply refused", "late reply was accepted");
	}

	//------------------------------------------------------------------------------------------------
	//! Kick armed 5 s ahead of a clock near int max. Both the deadline and the checks cross the wrap; Tick must kick only after the deadline.
	protected static void Test_Tick_KicksAcrossClockWrap()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("tick: kick across clock wrap");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		int now = 2147483000;
		mgr.SetClockOverrideForTests(now);
		ELIFE_JoinSession s = MakeSession(-210, "t-wrap", ELIFE_EJoinState.REJECTED, now);
		s.m_iKickAtMs = now + 5000;
		mgr.InsertSessionForTests(s);

		Check(s.m_iKickAtMs < 0, "wrap: kick deadline crosses int max", string.Format("kickAt=%1", s.m_iKickAtMs));

		mgr.SetClockOverrideForTests(now + 4999);
		mgr.Tick();
		Check(!s.m_bTestKickRequested, "wrap: no kick one ms before deadline", "kick requested early across wrap");

		mgr.SetClockOverrideForTests(now + 5001);
		mgr.Tick();
		Check(s.m_bTestKickRequested, "wrap: kick requested after deadline", "no kick after deadline across wrap");
	}

	//------------------------------------------------------------------------------------------------
	//! Fabricated connect reply. Only the fields the handler reads are set; no backend call is made.
	//! The web account is linked by default, so the active-path tests never reach the link screen. Link-screen tests set webAccountLinked to false themselves.
	protected static ELIFE_PlayerConnectedResponse MakeConnectReply(ELIFE_PlayerSessionStatus status)
	{
		ELIFE_PlayerConnectedResponse reply = new ELIFE_PlayerConnectedResponse();
		reply.status = status;
		reply.accountId = "acc-test";
		reply.webAccountLinked = true;
		return reply;
	}

	//------------------------------------------------------------------------------------------------
	//! Session in SESSION_PENDING with a request in flight, as StartSession leaves it. Returns the context that carries that request.
	protected static ELIFE_JoinRequestContext StartConnectForTests(ELIFE_JoinSessionManagerComponent mgr, int playerId, string sessionId, out ELIFE_JoinSession s)
	{
		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		s = MakeSession(playerId, sessionId, ELIFE_EJoinState.SESSION_PENDING, 100000);
		mgr.InsertSessionForTests(s);
		return mgr.BeginRequest(s);
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_Connect_BlockedRejectsBanned()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("connect: blocked");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartConnectForTests(mgr, -220, "t-blocked", s);
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, MakeConnectReply(ELIFE_PlayerSessionStatus.Blocked));

		Check(s.m_eState == ELIFE_EJoinState.REJECTED && s.m_eTestLastScreen == ELIFE_EJoinScreen.BANNED,
			"connect: blocked rejected as BANNED", string.Format("state=%1 screen=%2", s.m_eState, s.m_eTestLastScreen));
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_Connect_NotWhitelistedRejects()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("connect: not whitelisted");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartConnectForTests(mgr, -221, "t-whitelist", s);
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, MakeConnectReply(ELIFE_PlayerSessionStatus.NotWhitelisted));

		Check(s.m_eState == ELIFE_EJoinState.REJECTED && s.m_eTestLastScreen == ELIFE_EJoinScreen.NOT_WHITELISTED,
			"connect: not whitelisted rejected as NOT_WHITELISTED", string.Format("state=%1 screen=%2", s.m_eState, s.m_eTestLastScreen));
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_Connect_ActiveNoCharactersCreates()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("connect: active, no characters");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartConnectForTests(mgr, -222, "t-create", s);
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, MakeConnectReply(ELIFE_PlayerSessionStatus.Active));

		Check(s.m_eState == ELIFE_EJoinState.CREATING && s.m_eTestLastScreen == ELIFE_EJoinScreen.CREATING,
			"connect: active with no characters goes to CREATING", string.Format("state=%1 screen=%2", s.m_eState, s.m_eTestLastScreen));
	}

	//------------------------------------------------------------------------------------------------
	//! Unlinked is not a new player and gets no link screen: the session stays in SESSION_PENDING with the first retry scheduled at the base delay.
	protected static void Test_Connect_UnlinkedSchedulesRetry()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("connect: unlinked schedules retry");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartConnectForTests(mgr, -223, "t-link", s);
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, MakeConnectReply(ELIFE_PlayerSessionStatus.Unlinked));

		int expectedAt = 100000 + Math.Round(mgr.m_fUnlinkedRetryDelaySeconds * 1000);
		Check(s.m_eState == ELIFE_EJoinState.SESSION_PENDING && !s.m_bRequestInFlight && s.m_eTestLastScreen != ELIFE_EJoinScreen.LINKING,
			"connect: unlinked stays pending, no link screen", string.Format("state=%1 inflight=%2 screen=%3", s.m_eState, s.m_bRequestInFlight, s.m_eTestLastScreen));
		Check(s.m_iUnlinkedRetries == 1 && s.m_iUnlinkedRetryAtMs == expectedAt,
			"connect: unlinked schedules the first retry at the base delay", string.Format("retries=%1 at=%2 expected=%3", s.m_iUnlinkedRetries, s.m_iUnlinkedRetryAtMs, expectedAt));
	}

	//------------------------------------------------------------------------------------------------
	//! When the retry comes due it sends PlayerConnected again: one request in flight, the deadline cleared, the counter unchanged. No-send hook, so nothing reaches the backend.
	protected static void Test_Connect_UnlinkedRetryFires()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("connect: unlinked retry fires");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartConnectForTests(mgr, -274, "t-retry-fires", s);
		mgr.SetNoSendForTests(true);
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, MakeConnectReply(ELIFE_PlayerSessionStatus.Unlinked));
		int retryAt = s.m_iUnlinkedRetryAtMs;
		int tokenBefore = s.m_iRequestToken;

		mgr.SetClockOverrideForTests(retryAt);
		mgr.Tick();

		Check(s.m_eState == ELIFE_EJoinState.SESSION_PENDING && s.m_bRequestInFlight && s.m_iRequestToken == tokenBefore + 1 && s.m_iUnlinkedRetryAtMs == 0 && s.m_iUnlinkedRetries == 1,
			"connect: due retry sends PlayerConnected once and clears its deadline",
			string.Format("state=%1 inflight=%2 token=%3 before=%4 at=%5 retries=%6", s.m_eState, s.m_bRequestInFlight, s.m_iRequestToken, tokenBefore, s.m_iUnlinkedRetryAtMs, s.m_iUnlinkedRetries));
	}

	//------------------------------------------------------------------------------------------------
	//! Every retry is spent in turn (each fired at its own deadline), then the next unlinked answer rejects with ErrorBackend. Runs m_iUnlinkedMaxRetries times, so it adapts to the configured limit.
	protected static void Test_Connect_UnlinkedRetriesExhaust()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("connect: unlinked retries exhausted");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartConnectForTests(mgr, -275, "t-retry-exhaust", s);
		mgr.SetNoSendForTests(true);
		int maxRetries = mgr.m_iUnlinkedMaxRetries;

		bool chainOk = true;
		for (int i = 0; i < maxRetries; i++)
		{
			mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, MakeConnectReply(ELIFE_PlayerSessionStatus.Unlinked));
			if (s.m_eState != ELIFE_EJoinState.SESSION_PENDING || s.m_iUnlinkedRetries != i + 1 || s.m_iUnlinkedRetryAtMs == 0)
			{
				chainOk = false;
				break;
			}

			mgr.SetClockOverrideForTests(s.m_iUnlinkedRetryAtMs);
			mgr.Tick();
			ctx = mgr.RequestContextForTests(-275);
			if (!ctx)
			{
				chainOk = false;
				break;
			}
		}

		if (chainOk && ctx)
			mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, MakeConnectReply(ELIFE_PlayerSessionStatus.Unlinked));

		Check(chainOk && s.m_eState == ELIFE_EJoinState.REJECTED && s.m_eTestLastScreen == ELIFE_EJoinScreen.ERROR
			&& s.m_sTestLastPayload.Contains("#ELIFE-Join_ErrorBackend") && s.m_iUnlinkedRetries == maxRetries,
			"connect: unlinked retries exhausted rejected as ERROR",
			string.Format("chain=%1 state=%2 screen=%3 retries=%4 max=%5", chainOk, s.m_eState, s.m_eTestLastScreen, s.m_iUnlinkedRetries, maxRetries));
	}

	//------------------------------------------------------------------------------------------------
	//! The wait for an unlinked retry is not a timeout: one ms before its deadline nothing is kicked, nothing is in flight and the retry is still armed.
	protected static void Test_Connect_UnlinkedWaitNotTimedOut()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("connect: unlinked wait not timed out");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartConnectForTests(mgr, -276, "t-retry-wait", s);
		mgr.SetNoSendForTests(true);
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, MakeConnectReply(ELIFE_PlayerSessionStatus.Unlinked));
		int retryAt = s.m_iUnlinkedRetryAtMs;

		mgr.SetClockOverrideForTests(retryAt - 1);
		mgr.Tick();

		Check(!s.m_bTestKickRequested && s.m_eState == ELIFE_EJoinState.SESSION_PENDING && !s.m_bRequestInFlight && s.m_iUnlinkedRetryAtMs == retryAt,
			"connect: unlinked wait is not kicked or timed out",
			string.Format("kick=%1 state=%2 inflight=%3 at=%4 expected=%5", s.m_bTestKickRequested, s.m_eState, s.m_bRequestInFlight, s.m_iUnlinkedRetryAtMs, retryAt));
	}

	//------------------------------------------------------------------------------------------------
	//! Session in SESSION_PENDING with a recheck in flight, as HandleRequestRecheck leaves it. Returns the context that carries that request.
	protected static ELIFE_JoinRequestContext StartRecheckForTests(ELIFE_JoinSessionManagerComponent mgr, int playerId, string sessionId, out ELIFE_JoinSession s)
	{
		ELIFE_JoinRequestContext ctx = StartConnectForTests(mgr, playerId, sessionId, s);
		s.m_bRechecking = true;
		s.m_sLinkPin = "39PBKY7H";
		return ctx;
	}

	//------------------------------------------------------------------------------------------------
	//! Check again from LINKING: the session goes to SESSION_PENDING with the PlayerConnected request in flight (no-send hook).
	protected static void Test_Recheck_InLinkingGoesPending()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("recheck: linking goes pending");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		mgr.SetNoSendForTests(true);
		ELIFE_JoinSession s = MakeSession(-260, "t-recheck-pending", ELIFE_EJoinState.LINKING, 100000);
		mgr.InsertSessionForTests(s);

		mgr.HandleRequestRecheck(-260);
		Check(s.m_eState == ELIFE_EJoinState.SESSION_PENDING && s.m_bRequestInFlight && s.m_bRechecking,
			"recheck: LINKING goes to SESSION_PENDING with a request in flight",
			string.Format("state=%1 inflight=%2 rechecking=%3", s.m_eState, s.m_bRequestInFlight, s.m_bRechecking));
	}

	//------------------------------------------------------------------------------------------------
	//! Still unlinked after a recheck: no link screen, a retry is scheduled, and the recheck flag is cleared so the retry is a plain connect.
	protected static void Test_Recheck_UnlinkedAgainSchedulesRetry()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("recheck: unlinked again");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartRecheckForTests(mgr, -261, "t-recheck-unlinked", s);
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, MakeConnectReply(ELIFE_PlayerSessionStatus.Unlinked));

		Check(s.m_eState == ELIFE_EJoinState.SESSION_PENDING && s.m_iUnlinkedRetries == 1 && s.m_iUnlinkedRetryAtMs != 0 && !s.m_bRechecking && s.m_eTestLastScreen != ELIFE_EJoinScreen.LINKING,
			"recheck: unlinked again schedules a retry, no link screen",
			string.Format("state=%1 retries=%2 at=%3 rechecking=%4 screen=%5", s.m_eState, s.m_iUnlinkedRetries, s.m_iUnlinkedRetryAtMs, s.m_bRechecking, s.m_eTestLastScreen));
	}

	//------------------------------------------------------------------------------------------------
	//! Linked after a recheck, with no characters: CREATING, as on a first connect.
	protected static void Test_Recheck_ActiveNoCharactersCreates()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("recheck: active, no characters");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartRecheckForTests(mgr, -262, "t-recheck-create", s);
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, MakeConnectReply(ELIFE_PlayerSessionStatus.Active));

		Check(s.m_eState == ELIFE_EJoinState.CREATING && s.m_eTestLastScreen == ELIFE_EJoinScreen.CREATING && !s.m_bRechecking,
			"recheck: active with no characters goes to CREATING",
			string.Format("state=%1 screen=%2", s.m_eState, s.m_eTestLastScreen));
	}

	//------------------------------------------------------------------------------------------------
	//! A failed recheck is not a rejection: back to LINKING with the ErrorBackend hint, so the player can try again.
	protected static void Test_Recheck_ErrorBackToLinking()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("recheck: error back to linking");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartRecheckForTests(mgr, -263, "t-recheck-error", s);
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.ERROR, null);

		ELIFE_JoinLinkPayload payload = new ELIFE_JoinLinkPayload();
		payload.ExpandFromRAW(s.m_sTestLastPayload);
		Check(s.m_eState == ELIFE_EJoinState.LINKING && s.m_eTestLastScreen == ELIFE_EJoinScreen.LINKING && payload.hint == "#ELIFE-Join_ErrorBackend" && !s.m_bTestKickRequested,
			"recheck: failure goes back to LINKING with ErrorBackend, not REJECTED",
			string.Format("state=%1 screen=%2 hint=%3", s.m_eState, s.m_eTestLastScreen, payload.hint));
	}

	//------------------------------------------------------------------------------------------------
	//! A recheck that runs past the backend deadline also goes back to LINKING with ErrorBackend.
	protected static void Test_Recheck_TimeoutBackToLinking()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("recheck: timeout back to linking");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		StartRecheckForTests(mgr, -264, "t-recheck-timeout", s);
		mgr.SetClockOverrideForTests(100000 + Math.Round(mgr.m_fBackendTimeoutSeconds * 1000));
		mgr.Tick();

		ELIFE_JoinLinkPayload payload = new ELIFE_JoinLinkPayload();
		payload.ExpandFromRAW(s.m_sTestLastPayload);
		Check(s.m_eState == ELIFE_EJoinState.LINKING && payload.hint == "#ELIFE-Join_ErrorBackend" && !s.m_bRequestInFlight && !s.m_bRechecking,
			"recheck: timeout goes back to LINKING with ErrorBackend, flag cleared",
			string.Format("state=%1 hint=%2 inflight=%3 rechecking=%4", s.m_eState, payload.hint, s.m_bRequestInFlight, s.m_bRechecking));
	}

	//------------------------------------------------------------------------------------------------
	//! Leave on the link screen kicks at once, as the rejected screen's close does.
	protected static void Test_AckClosed_LinkingKicks()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("ack closed: linking kicks");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		ELIFE_JoinSession s = MakeSession(-270, "t-leave-linking", ELIFE_EJoinState.LINKING, 100000);
		mgr.InsertSessionForTests(s);

		mgr.HandleAckClosed(-270);
		Check(s.m_bTestKickRequested, "ack closed: Leave in LINKING kicks", string.Format("state=%1", s.m_eState));
	}

	//------------------------------------------------------------------------------------------------
	//! Leave while a recheck is in flight kicks at once; the in-flight request does not block it.
	protected static void Test_AckClosed_RecheckKicks()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("ack closed: recheck kicks");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		StartRecheckForTests(mgr, -271, "t-leave-recheck", s);
		mgr.HandleAckClosed(-271);
		Check(s.m_bTestKickRequested, "ack closed: Leave during a recheck kicks despite the request in flight",
			string.Format("state=%1 inflight=%2", s.m_eState, s.m_bRequestInFlight));
	}

	//------------------------------------------------------------------------------------------------
	//! Leave is still refused outside LINKING, a recheck and REJECTED: a CREATING session is not kicked.
	protected static void Test_AckClosed_CreatingRefused()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("ack closed: creating refused");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		ELIFE_JoinSession s = MakeSession(-272, "t-leave-creating", ELIFE_EJoinState.CREATING, 100000);
		mgr.InsertSessionForTests(s);

		mgr.HandleAckClosed(-272);
		Check(!s.m_bTestKickRequested && s.m_eState == ELIFE_EJoinState.CREATING,
			"ack closed: Leave in CREATING refused", string.Format("state=%1 kick=%2", s.m_eState, s.m_bTestKickRequested));
	}

	//------------------------------------------------------------------------------------------------
	//! A recheck is ignored outside LINKING, and while a request is in flight.
	protected static void Test_Recheck_IgnoredWhenNotLinking()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("recheck: ignored when not linking");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		mgr.SetNoSendForTests(true);
		ELIFE_JoinSession selecting = MakeSession(-265, "t-recheck-selecting", ELIFE_EJoinState.SELECTING, 100000);
		ELIFE_JoinSession busy = MakeSession(-266, "t-recheck-busy", ELIFE_EJoinState.LINKING, 100000);
		busy.m_bRequestInFlight = true;
		mgr.InsertSessionForTests(selecting);
		mgr.InsertSessionForTests(busy);

		mgr.HandleRequestRecheck(-265);
		mgr.HandleRequestRecheck(-266);

		Check(selecting.m_eState == ELIFE_EJoinState.SELECTING && !selecting.m_bRequestInFlight,
			"recheck: ignored in SELECTING", string.Format("state=%1", selecting.m_eState));
		Check(busy.m_eState == ELIFE_EJoinState.LINKING && !busy.m_bRechecking,
			"recheck: ignored in LINKING while a request is in flight", string.Format("state=%1", busy.m_eState));
	}

	//------------------------------------------------------------------------------------------------
	//! Check again inside the 30 s cooldown is ignored (no state change, no new request); one at +31 s is accepted. Fake clock: first at 0 s, then +10 s and +31 s.
	protected static void Test_Recheck_CooldownIgnoresEarly()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("recheck: cooldown");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		mgr.SetNoSendForTests(true);
		ELIFE_JoinSession s = MakeSession(-280, "t-recheck-cooldown", ELIFE_EJoinState.LINKING, 100000);
		mgr.InsertSessionForTests(s);

		mgr.HandleRequestRecheck(-280);
		Check(s.m_eState == ELIFE_EJoinState.SESSION_PENDING && s.m_bRequestInFlight,
			"recheck cooldown: first check accepted", string.Format("state=%1 inflight=%2", s.m_eState, s.m_bRequestInFlight));

		// A failed reply returns the session to LINKING, as the player sees it before pressing again.
		mgr.OnPlayerConnectedResult(mgr.RequestContextForTests(-280), ELIFE_EApiStatusCode.ERROR, null);
		int tokenAfterFirst = s.m_iRequestToken;

		mgr.SetClockOverrideForTests(110000);
		mgr.HandleRequestRecheck(-280);
		Check(s.m_eState == ELIFE_EJoinState.LINKING && !s.m_bRequestInFlight && s.m_iRequestToken == tokenAfterFirst,
			"recheck cooldown: check at +10 s ignored",
			string.Format("state=%1 inflight=%2 token=%3 expected=%4", s.m_eState, s.m_bRequestInFlight, s.m_iRequestToken, tokenAfterFirst));

		mgr.SetClockOverrideForTests(131000);
		mgr.HandleRequestRecheck(-280);
		Check(s.m_eState == ELIFE_EJoinState.SESSION_PENDING && s.m_bRequestInFlight && s.m_iRequestToken == tokenAfterFirst + 1,
			"recheck cooldown: check at +31 s accepted",
			string.Format("state=%1 inflight=%2 token=%3 expected=%4", s.m_eState, s.m_bRequestInFlight, s.m_iRequestToken, tokenAfterFirst + 1));
	}

	//------------------------------------------------------------------------------------------------
	//! LINKING is idle-kicked like CREATING and SELECTING: past the limit it is kicked, a session active 1 s ago is kept.
	protected static void Test_Tick_KicksIdleLinking()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("tick: idle linking kick");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		int now = 1000000;
		mgr.SetClockOverrideForTests(now);
		int idleMs = Math.Round(mgr.m_fIdleTimeoutSeconds * 1000);

		ELIFE_JoinSession idle = MakeSession(-267, "t-idle-link", ELIFE_EJoinState.LINKING, now - idleMs - 1000);
		ELIFE_JoinSession active = MakeSession(-268, "t-active-link", ELIFE_EJoinState.LINKING, now - 1000);
		mgr.InsertSessionForTests(idle);
		mgr.InsertSessionForTests(active);

		mgr.Tick();

		Check(idle.m_bTestKickRequested, "tick: LINKING idle past the limit kicked", "no idle kick");
		Check(!active.m_bTestKickRequested, "tick: LINKING active 1 s ago not kicked", "active session kicked");
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_Connect_ErrorStatusRejects()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("connect: error status");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartConnectForTests(mgr, -224, "t-error", s);
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.ERROR, null);

		Check(s.m_eState == ELIFE_EJoinState.REJECTED && s.m_eTestLastScreen == ELIFE_EJoinScreen.ERROR,
			"connect: backend error rejected as ERROR", string.Format("state=%1 screen=%2", s.m_eState, s.m_eTestLastScreen));
	}

	//------------------------------------------------------------------------------------------------
	//! Success with no body is treated like an error.
	protected static void Test_Connect_NullDataRejects()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("connect: null data");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartConnectForTests(mgr, -225, "t-nulldata", s);
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, null);

		Check(s.m_eState == ELIFE_EJoinState.REJECTED && s.m_eTestLastScreen == ELIFE_EJoinScreen.ERROR,
			"connect: success with null data rejected as ERROR", string.Format("state=%1 screen=%2", s.m_eState, s.m_eTestLastScreen));
	}

	//------------------------------------------------------------------------------------------------
	//! An unknown status is admitted like active. With no characters it goes to CREATING with the account stored, not REJECTED.
	protected static void Test_Connect_UnknownStatusProceedsAsActive()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("connect: unknown status proceeds");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartConnectForTests(mgr, -226, "t-unknown", s);
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, MakeConnectReply(ELIFE_PlayerSessionStatus.Unknown));

		Check(s.m_eState == ELIFE_EJoinState.CREATING && s.m_eTestLastScreen == ELIFE_EJoinScreen.CREATING && s.m_aCharacters.Count() == 0 && s.m_sAccountId == "acc-test",
			"connect: unknown status with no characters goes to CREATING",
			string.Format("state=%1 screen=%2 characters=%3 account=%4", s.m_eState, s.m_eTestLastScreen, s.m_aCharacters.Count(), s.m_sAccountId));
	}

	//------------------------------------------------------------------------------------------------
	//! An unknown status with a character listed goes to SELECTING with that character, as an active reply does.
	protected static void Test_Connect_UnknownStatusWithCharactersSelects()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("connect: unknown status with characters");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartConnectForTests(mgr, -277, "t-unknown-select", s);
		ELIFE_PlayerConnectedResponse reply = MakeConnectReply(ELIFE_PlayerSessionStatus.Unknown);
		ELIFE_CharacterSummary summary = new ELIFE_CharacterSummary();
		summary.characterId = "c-1";
		summary.name = "Anna Smith";
		reply.characters.Insert(summary);
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, reply);

		Check(s.m_eState == ELIFE_EJoinState.SELECTING && s.m_eTestLastScreen == ELIFE_EJoinScreen.SELECTING && s.FindCharacterName("c-1") == "Anna Smith",
			"connect: unknown status with characters goes to SELECTING",
			string.Format("state=%1 screen=%2 characters=%3", s.m_eState, s.m_eTestLastScreen, s.m_aCharacters.Count()));
	}

	//------------------------------------------------------------------------------------------------
	//! The first of two requests is stale once the second is sent. Its reply must change nothing.
	protected static void Test_Connect_StaleTokenIgnored()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("connect: stale token");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext first = StartConnectForTests(mgr, -227, "t-stale-connect", s);
		ELIFE_JoinRequestContext second = mgr.BeginRequest(s);
		s.m_sTestLastPayload = "sentinel";

		mgr.OnPlayerConnectedResult(first, ELIFE_EApiStatusCode.SUCCESS, MakeConnectReply(ELIFE_PlayerSessionStatus.Blocked));

		Check(s.m_eState == ELIFE_EJoinState.SESSION_PENDING && s.m_sTestLastPayload == "sentinel" && s.m_bRequestInFlight,
			"connect: stale reply changes nothing", string.Format("state=%1 payload=%2", s.m_eState, s.m_sTestLastPayload));
	}

	//------------------------------------------------------------------------------------------------
	//! Restart and duplicate paths: the tracked PlayerDisconnected reply clears the wait flag and starts the connect.
	//! Its request is left in flight by the no-send hook, so no backend call is made. The ELIFE_Api-null branch is not exercised here.
	protected static void Test_DuplicateClear_ReplyStartsConnect()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("duplicate clear: reply starts connect");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		mgr.SetNoSendForTests(true);
		ELIFE_JoinSession s = MakeSession(-230, "t-dup-clear", ELIFE_EJoinState.SESSION_PENDING, 100000);
		s.m_bAwaitingDuplicateClear = true;
		mgr.InsertSessionForTests(s);

		ELIFE_JoinRequestContext ctx = mgr.BeginRequest(s);
		int tokenBefore = s.m_iRequestToken;
		mgr.OnPlayerDisconnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, null);

		// The reply's AcceptResponse bumps the token once, and the connect's BeginRequest bumps it again: +2 means the connect was sent.
		Check(!s.m_bAwaitingDuplicateClear && s.m_eState == ELIFE_EJoinState.SESSION_PENDING && s.m_iRequestToken == tokenBefore + 2,
			"duplicate clear: reply clears the wait and starts the connect",
			string.Format("awaiting=%1 state=%2 token=%3 before=%4", s.m_bAwaitingDuplicateClear, s.m_eState, s.m_iRequestToken, tokenBefore));
	}

	//------------------------------------------------------------------------------------------------
	//! No backend client at startup: the connect waits in SESSION_PENDING with CONNECTING shown. No reject, nothing sent.
	protected static void Test_AwaitApi_WaitsInsteadOfReject()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("await api: waits");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		mgr.SetNoSendForTests(true);
		mgr.SetApiMissingForTests(true);
		mgr.StartSession(-240);

		ELIFE_JoinSession s = mgr.GetSession(-240);
		Check(s && s.m_eState == ELIFE_EJoinState.SESSION_PENDING && s.m_bAwaitingApi && !s.m_bRequestInFlight
			&& s.m_eTestLastScreen == ELIFE_EJoinScreen.CONNECTING && !s.m_bTestKickRequested,
			"await api: SESSION_PENDING, awaiting, CONNECTING, no reject",
			string.Format("found=%1", s != null));
	}

	//------------------------------------------------------------------------------------------------
	//! Still no backend at the deadline: one ms before it the wait goes on, at it the session is rejected with ErrorBackend.
	protected static void Test_AwaitApi_DeadlineRejects()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("await api: deadline");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		mgr.SetNoSendForTests(true);
		mgr.SetApiMissingForTests(true);
		mgr.StartSession(-241);

		ELIFE_JoinSession s = mgr.GetSession(-241);
		int timeoutMs = Math.Round(mgr.m_fBackendTimeoutSeconds * 1000);

		mgr.SetClockOverrideForTests(100000 + timeoutMs - 1);
		mgr.Tick();
		Check(s && s.m_eState == ELIFE_EJoinState.SESSION_PENDING && s.m_bAwaitingApi,
			"await api: still waiting one ms before the deadline",
			string.Format("found=%1", s != null));

		mgr.SetClockOverrideForTests(100000 + timeoutMs);
		mgr.Tick();
		Check(s && s.m_eState == ELIFE_EJoinState.REJECTED && s.m_eTestLastScreen == ELIFE_EJoinScreen.ERROR
			&& s.m_sTestLastPayload.Contains("#ELIFE-Join_ErrorBackend") && !s.m_bAwaitingApi,
			"await api: deadline rejects with ErrorBackend",
			string.Format("found=%1", s != null));
	}

	//------------------------------------------------------------------------------------------------
	//! The backend client appears while waiting: the next Tick clears the wait and starts the connect (the in-flight request is
	//! PlayerConnected, left unsent by the no-send hook).
	protected static void Test_AwaitApi_ResumesWhenAvailable()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("await api: resumes");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		mgr.SetNoSendForTests(true);
		mgr.SetApiMissingForTests(true);
		mgr.StartSession(-242);

		ELIFE_JoinSession s = mgr.GetSession(-242);
		int tokenBefore = 0;
		if (s)
			tokenBefore = s.m_iRequestToken;

		mgr.SetApiMissingForTests(false);
		mgr.Tick();

		int tokenAfter = 0;
		if (s)
			tokenAfter = s.m_iRequestToken;

		Check(s && !s.m_bAwaitingApi && s.m_bRequestInFlight && tokenAfter == tokenBefore + 1 && s.m_eState == ELIFE_EJoinState.SESSION_PENDING,
			"await api: available client clears the wait and starts the connect",
			string.Format("found=%1 before=%2 after=%3", s != null, tokenBefore, tokenAfter));
	}

	//------------------------------------------------------------------------------------------------
	//! The player leaves while waiting: the session ends normally, and nothing is sent since the identity never connected.
	protected static void Test_AwaitApi_EndSessionCleansUp()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("await api: end session");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		mgr.SetNoSendForTests(true);
		mgr.SetApiMissingForTests(true);
		mgr.StartSession(-243);

		ELIFE_JoinSession s = mgr.GetSession(-243);
		mgr.EndSession(-243, "test left");

		Check(s && mgr.GetSession(-243) == null && s.m_eState == ELIFE_EJoinState.ENDED && !s.m_bAwaitingApi && !s.m_bRequestInFlight,
			"await api: EndSession while waiting cleans up",
			string.Format("found=%1", s != null));
	}

	//------------------------------------------------------------------------------------------------
	//! A character id outside the session's list is refused with SelectFailed; nothing is in flight.
	protected static void Test_Select_UnknownIdRefused()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("select: unknown id");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		ELIFE_JoinSession s = MakeSession(-240, "t-select-unknown", ELIFE_EJoinState.SELECTING, 100000);
		s.m_aCharacters.Insert(MakeEntry("c-1", "Anna Smith"));
		mgr.InsertSessionForTests(s);

		mgr.HandleRequestSelect(-240, "c-9");

		Check(s.m_eState == ELIFE_EJoinState.SELECTING && !s.m_bRequestInFlight && s.m_sTestLastPayload.Contains("#ELIFE-Join_SelectFailed"),
			"select: unknown id refused with SelectFailed", string.Format("state=%1 inflight=%2 payload=%3", s.m_eState, s.m_bRequestInFlight, s.m_sTestLastPayload));
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_Select_IgnoredInCreating()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("select: ignored in CREATING");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		ELIFE_JoinSession s = MakeSession(-241, "t-select-creating", ELIFE_EJoinState.CREATING, 100000);
		s.m_aCharacters.Insert(MakeEntry("c-1", "Anna Smith"));
		s.m_sTestLastPayload = "sentinel";
		mgr.InsertSessionForTests(s);

		mgr.HandleRequestSelect(-241, "c-1");

		Check(s.m_eState == ELIFE_EJoinState.CREATING && s.m_sTestLastPayload == "sentinel" && !s.m_bRequestInFlight,
			"select: ignored in CREATING", string.Format("state=%1 payload=%2", s.m_eState, s.m_sTestLastPayload));
	}

	//------------------------------------------------------------------------------------------------
	//! A listed id goes to LOADING_CHARACTER. No-send hook: the backend is not called.
	protected static void Test_Select_KnownIdLoads()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("select: known id loads");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetNoSendForTests(true);
		ELIFE_JoinSession s = MakeSession(-242, "t-select-ok", ELIFE_EJoinState.SELECTING, 100000);
		s.m_aCharacters.Insert(MakeEntry("c-1", "Anna Smith"));
		mgr.InsertSessionForTests(s);

		mgr.HandleRequestSelect(-242, "c-1");

		Check(s.m_eState == ELIFE_EJoinState.LOADING_CHARACTER && s.m_bRequestInFlight && s.m_sSelectedCharacterId == "c-1" && s.m_sSelectedCharacterName == "Anna Smith",
			"select: known id goes to LOADING_CHARACTER", string.Format("state=%1 inflight=%2 id=%3", s.m_eState, s.m_bRequestInFlight, s.m_sSelectedCharacterId));
	}

	//------------------------------------------------------------------------------------------------
	//! Name rejected before any filter or backend call: stays CREATING with the key, nothing in flight.
	protected static void Test_Create_ShortNameStaysCreating()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("create: short name");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		ELIFE_JoinSession s = MakeSession(-243, "t-create-short", ELIFE_EJoinState.CREATING, 100000);
		mgr.InsertSessionForTests(s);

		mgr.HandleRequestCreate(-243, "a", "b", 0);

		Check(s.m_eState == ELIFE_EJoinState.CREATING && s.m_eTestLastScreen == ELIFE_EJoinScreen.CREATING && !s.m_bRequestInFlight
			&& s.m_sTestLastPayload.Contains("#ELIFE-Join_NameTooShort"),
			"create: (a, b) stays CREATING with NameTooShort", string.Format("state=%1 inflight=%2 payload=%3", s.m_eState, s.m_bRequestInFlight, s.m_sTestLastPayload));
	}

	//------------------------------------------------------------------------------------------------
	//! An index equal to the count is out of range: refused with AppearanceInvalid, nothing in flight.
	protected static void Test_Create_AppearanceOutOfRangeRefused()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("create: appearance out of range");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		ELIFE_JoinSession s = MakeSession(-244, "t-create-appearance", ELIFE_EJoinState.CREATING, 100000);
		mgr.InsertSessionForTests(s);

		mgr.HandleRequestCreate(-244, "Anna", "Smith", mgr.GetAppearanceCount());

		Check(s.m_eState == ELIFE_EJoinState.CREATING && !s.m_bRequestInFlight && s.m_sTestLastPayload.Contains("#ELIFE-Join_AppearanceInvalid"),
			"create: out-of-range appearance refused", string.Format("state=%1 inflight=%2 payload=%3", s.m_eState, s.m_bRequestInFlight, s.m_sTestLastPayload));
	}

	//------------------------------------------------------------------------------------------------
	//! The second create while the first is in flight is refused; the token stays where the first request left it.
	protected static void Test_Create_SecondRequestIgnoredInFlight()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("create: second ignored in flight");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetNoSendForTests(true);
		ELIFE_JoinSession s = MakeSession(-245, "t-create-twice", ELIFE_EJoinState.CREATING, 100000);
		mgr.InsertSessionForTests(s);

		mgr.HandleRequestCreate(-245, "Anna", "Smith", 0);
		bool firstInFlight = s.m_bRequestInFlight;
		int tokenAfterFirst = s.m_iRequestToken;

		mgr.HandleRequestCreate(-245, "Ben", "Jones", 0);

		Check(firstInFlight && s.m_iRequestToken == tokenAfterFirst && s.m_eState == ELIFE_EJoinState.CREATING,
			"create: second request ignored while in flight", string.Format("inflight=%1 token=%2 before=%3", firstInFlight, s.m_iRequestToken, tokenAfterFirst));
	}

	//------------------------------------------------------------------------------------------------
	//! A create success adds the character and goes straight to LOADING_CHARACTER with it selected.
	protected static void Test_Create_SuccessSelectsNewCharacter()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("create: success selects");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetNoSendForTests(true);
		ELIFE_JoinSession s = MakeSession(-246, "t-create-ok", ELIFE_EJoinState.CREATING, 100000);
		mgr.InsertSessionForTests(s);

		ELIFE_JoinRequestContext ctx = mgr.BeginRequest(s);
		ELIFE_CharacterSummary summary = new ELIFE_CharacterSummary();
		summary.characterId = "c-new";
		summary.name = "Anna Smith";
		mgr.OnCreateCharacterResult(ctx, ELIFE_EApiStatusCode.SUCCESS, summary);

		Check(s.m_eState == ELIFE_EJoinState.LOADING_CHARACTER && s.m_aCharacters.Count() == 1 && s.FindCharacterName("c-new") == "Anna Smith" && s.m_sSelectedCharacterId == "c-new",
			"create: success goes to LOADING_CHARACTER with the new entry", string.Format("state=%1 count=%2 id=%3", s.m_eState, s.m_aCharacters.Count(), s.m_sSelectedCharacterId));
	}

	//------------------------------------------------------------------------------------------------
	//! A failed create shows the create screen again with CreateFailed and the names echoed back.
	protected static void Test_Create_ErrorShowsCreateAgain()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("create: error shows create");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetNoSendForTests(true);
		ELIFE_JoinSession s = MakeSession(-247, "t-create-err", ELIFE_EJoinState.CREATING, 100000);
		mgr.InsertSessionForTests(s);

		ELIFE_JoinRequestContext ctx = mgr.BeginRequest(s);
		ctx.m_sFirstName = "Anna";
		ctx.m_sLastName = "Smith";
		mgr.OnCreateCharacterResult(ctx, ELIFE_EApiStatusCode.ERROR, null);

		Check(s.m_eState == ELIFE_EJoinState.CREATING && !s.m_bRequestInFlight && s.m_sTestLastPayload.Contains("#ELIFE-Join_CreateFailed") && s.m_sTestLastPayload.Contains("Anna"),
			"create: backend error shows CREATING with CreateFailed", string.Format("state=%1 payload=%2", s.m_eState, s.m_sTestLastPayload));
	}

	//------------------------------------------------------------------------------------------------
	//! A failed select returns to SELECTING with SelectFailed, and the list still shows the characters.
	protected static void Test_CharacterSelected_ErrorReturnsToSelecting()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("select result: error");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetNoSendForTests(true);
		ELIFE_JoinSession s = MakeSession(-248, "t-select-err", ELIFE_EJoinState.LOADING_CHARACTER, 100000);
		s.m_aCharacters.Insert(MakeEntry("c-1", "Anna Smith"));
		mgr.InsertSessionForTests(s);

		ELIFE_JoinRequestContext ctx = mgr.BeginRequest(s);
		mgr.OnCharacterSelectedResult(ctx, ELIFE_EApiStatusCode.ERROR, null);

		Check(s.m_eState == ELIFE_EJoinState.SELECTING && s.m_eTestLastScreen == ELIFE_EJoinScreen.SELECTING && s.m_sTestLastPayload.Contains("#ELIFE-Join_SelectFailed") && s.m_sTestLastPayload.Contains("Anna Smith"),
			"select result: error returns to SELECTING with SelectFailed", string.Format("state=%1 payload=%2", s.m_eState, s.m_sTestLastPayload));
	}

	//------------------------------------------------------------------------------------------------
	//! Platform answer that changed the name: the create is refused with NameNotAllowed, the session stays CREATING and the filter is released.
	//! The no-send hook is on so the clean branch, which would call the backend, cannot run.
	protected static void Test_NameFilter_MismatchRefused()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("name filter: mismatch refused");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetNoSendForTests(true);
		ELIFE_JoinSession s = MakeSession(-250, "t-filter-mismatch", ELIFE_EJoinState.CREATING, 100000);
		mgr.InsertSessionForTests(s);

		ELIFE_JoinRequestContext ctx = mgr.BeginRequest(s);
		ctx.m_sFirstName = "Anna";
		ctx.m_sLastName = "Smith";
		ELIFE_JoinNameFilterRequest filter = new ELIFE_JoinNameFilterRequest(mgr);
		filter.m_Context = ctx;
		filter.m_sFullName = "Anna Smith";
		mgr.AddNameFilterForTests(filter);
		int heldBefore = mgr.NameFilterCountForTests();

		mgr.OnNameFiltered(filter, "Anna Sh1t");

		Check(s.m_eState == ELIFE_EJoinState.CREATING && !s.m_bRequestInFlight && s.m_sTestLastPayload.Contains("#ELIFE-Join_NameNotAllowed")
			&& mgr.NameFilterCountForTests() == heldBefore - 1,
			"name filter: mismatch refused with NameNotAllowed, filter released",
			string.Format("state=%1 inflight=%2 payload=%3 held=%4 before=%5", s.m_eState, s.m_bRequestInFlight, s.m_sTestLastPayload, mgr.NameFilterCountForTests(), heldBefore));
	}

	//------------------------------------------------------------------------------------------------
	//! Answer from a request that is no longer current (token moved on): nothing changes, no screen is pushed, the filter is still released.
	//! The text matches, so only the stale check stops the backend call.
	protected static void Test_NameFilter_StaleAnswerDropped()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("name filter: stale answer");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetNoSendForTests(true);
		ELIFE_JoinSession s = MakeSession(-251, "t-filter-stale", ELIFE_EJoinState.CREATING, 100000);
		mgr.InsertSessionForTests(s);

		ELIFE_JoinRequestContext ctx = mgr.BeginRequest(s);
		ctx.m_sFirstName = "Anna";
		ctx.m_sLastName = "Smith";
		s.m_iRequestToken++;
		s.m_sTestLastPayload = "sentinel";
		int tokenNow = s.m_iRequestToken;

		ELIFE_JoinNameFilterRequest filter = new ELIFE_JoinNameFilterRequest(mgr);
		filter.m_Context = ctx;
		filter.m_sFullName = "Anna Smith";
		mgr.AddNameFilterForTests(filter);
		int heldBefore = mgr.NameFilterCountForTests();

		mgr.OnNameFiltered(filter, "Anna Smith");

		Check(s.m_eState == ELIFE_EJoinState.CREATING && s.m_sTestLastPayload == "sentinel" && s.m_bRequestInFlight && s.m_iRequestToken == tokenNow
			&& mgr.NameFilterCountForTests() == heldBefore - 1,
			"name filter: stale answer changes nothing, filter released",
			string.Format("state=%1 payload=%2 inflight=%3 token=%4 held=%5", s.m_eState, s.m_sTestLastPayload, s.m_bRequestInFlight, s.m_iRequestToken, mgr.NameFilterCountForTests()));
	}

	//------------------------------------------------------------------------------------------------
	//! Death in IN_GAME moves to SPAWNING with the respawn armed. A second death event changes nothing.
	protected static void Test_EntityLost_InGameToSpawning()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("entity lost: in game to spawning");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		ELIFE_JoinSession s = MakeSession(-250, "t-lost-ok", ELIFE_EJoinState.IN_GAME, 100000);
		mgr.InsertSessionForTests(s);

		mgr.OnEntityLost(-250);
		int armedAt = s.m_iRespawnAtMs;
		bool firstOk = s.m_eState == ELIFE_EJoinState.SPAWNING && armedAt > 0;

		// A second death event must not touch the armed respawn or the attempt count.
		s.m_iSpawnAttempts = 2;
		mgr.OnEntityLost(-250);
		Check(firstOk && s.m_eState == ELIFE_EJoinState.SPAWNING && s.m_iRespawnAtMs == armedAt && s.m_iSpawnAttempts == 2,
			"entity lost: IN_GAME to SPAWNING once", string.Format("state=%1 respawnAt=%2 first=%3 attempts=%4", s.m_eState, s.m_iRespawnAtMs, armedAt, s.m_iSpawnAttempts));
	}

	//------------------------------------------------------------------------------------------------
	//! Death while SELECTING is not a death of a spawned character: no change and no respawn armed.
	protected static void Test_EntityLost_IgnoredInSelecting()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("entity lost: ignored in SELECTING");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		ELIFE_JoinSession s = MakeSession(-251, "t-lost-sel", ELIFE_EJoinState.SELECTING, 100000);
		mgr.InsertSessionForTests(s);

		mgr.OnEntityLost(-251);
		Check(s.m_eState == ELIFE_EJoinState.SELECTING && s.m_iRespawnAtMs == 0,
			"entity lost: ignored in SELECTING", string.Format("state=%1 respawnAt=%2", s.m_eState, s.m_iRespawnAtMs));
	}

	//------------------------------------------------------------------------------------------------
	//! Three failed spawns in a row reject the player with the SpawnFailed error. Test ids never schedule a real retry.
	protected static void Test_SpawnFailed_ThreeTimesRejects()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("spawn failed: three times rejects");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		ELIFE_JoinSession s = MakeSession(-252, "t-spawn-fail", ELIFE_EJoinState.SPAWNING, 100000);
		mgr.InsertSessionForTests(s);

		mgr.OnSpawnFailed(-252);
		mgr.OnSpawnFailed(-252);
		mgr.OnSpawnFailed(-252);

		Check(s.m_eState == ELIFE_EJoinState.REJECTED && s.m_eTestLastScreen == ELIFE_EJoinScreen.ERROR && s.m_sTestLastPayload.Contains("SpawnFailed"),
			"spawn failed three times: REJECTED with SpawnFailed", string.Format("state=%1 screen=%2 payload=%3", s.m_eState, s.m_eTestLastScreen, s.m_sTestLastPayload));
	}

	//------------------------------------------------------------------------------------------------
	//! Self-tests only. A session in `state` that holds selected character "c-1", read through the confirmed accessor.
	protected static string ConfirmedIdInState(ELIFE_JoinSessionManagerComponent mgr, int playerId, ELIFE_EJoinState state)
	{
		ELIFE_JoinSession s = MakeSession(playerId, string.Format("t-conf-%1", playerId), state, 100000);
		s.m_sSelectedCharacterId = "c-1";
		mgr.InsertSessionForTests(s);
		return mgr.GetConfirmedCharacterId(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Before CharacterSelected answers, and after it fails, the selected id is not confirmed: the accessor gives "".
	protected static void Test_ConfirmedCharacter_UnconfirmedStatesEmpty()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("confirmed: unconfirmed states");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();

		string loading = ConfirmedIdInState(mgr, -300, ELIFE_EJoinState.LOADING_CHARACTER);
		string selecting = ConfirmedIdInState(mgr, -301, ELIFE_EJoinState.SELECTING);
		string creating = ConfirmedIdInState(mgr, -302, ELIFE_EJoinState.CREATING);
		string pending = ConfirmedIdInState(mgr, -303, ELIFE_EJoinState.SESSION_PENDING);
		string rejected = ConfirmedIdInState(mgr, -304, ELIFE_EJoinState.REJECTED);

		Check(loading == "" && selecting == "" && creating == "" && pending == "" && rejected == "",
			"confirmed: LOADING_CHARACTER, SELECTING, CREATING, SESSION_PENDING and REJECTED give empty",
			string.Format("loading=%1 selecting=%2 creating=%3 pending=%4 rejected=%5", loading, selecting, creating, pending, rejected));
	}

	//------------------------------------------------------------------------------------------------
	//! Once CharacterSelected succeeded the session is SPAWNING, and it stays IN_GAME, so both give the selected id.
	protected static void Test_ConfirmedCharacter_ConfirmedStatesGiveId()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("confirmed: confirmed states");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();

		string spawning = ConfirmedIdInState(mgr, -305, ELIFE_EJoinState.SPAWNING);
		string inGame = ConfirmedIdInState(mgr, -306, ELIFE_EJoinState.IN_GAME);

		Check(spawning == "c-1" && inGame == "c-1",
			"confirmed: SPAWNING and IN_GAME give the selected id",
			string.Format("spawning=%1 inGame=%2", spawning, inGame));
	}

	//------------------------------------------------------------------------------------------------
	//! A player with no session has no confirmed character.
	protected static void Test_ConfirmedCharacter_NoSessionEmpty()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("confirmed: no session");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();

		string none = mgr.GetConfirmedCharacterId(-307);

		Check(none == "", "confirmed: unknown player gives empty", string.Format("id=%1", none));
	}

	//------------------------------------------------------------------------------------------------
	//! An appearance index out of range gives the index 0 prefab.
	protected static void Test_Appearance_OutOfRangeIsDefault()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("appearance: out of range is index 0");
		if (!mgr)
			return;

		Check(mgr.GetAppearancePrefab(99) == mgr.GetAppearancePrefab(0) && mgr.GetAppearancePrefab(0) != "",
			"appearance: out of range gives index 0", string.Format("p99=%1 p0=%2", mgr.GetAppearancePrefab(99), mgr.GetAppearancePrefab(0)));
	}

	//------------------------------------------------------------------------------------------------
	//! Active but the web account is not linked, with a code and URL from the reply: the link screen shows both, with no hint on a first connect.
	//! The check names and failure texts never carry the URL or the code.
	protected static void Test_Connect_ActiveNotLinkedShowsLinking()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("connect: not linked shows link screen");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartConnectForTests(mgr, -282, "t-link-show", s);
		ELIFE_PlayerConnectedResponse reply = MakeConnectReply(ELIFE_PlayerSessionStatus.Active);
		reply.webAccountLinked = false;
		reply.linkPin = "PIN-TEST-1";
		reply.linkUrl = "http://test.invalid/link-one";
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, reply);

		ELIFE_JoinLinkPayload payload = new ELIFE_JoinLinkPayload();
		payload.ExpandFromRAW(s.m_sTestLastPayload);
		Check(s.m_eState == ELIFE_EJoinState.LINKING && s.m_eTestLastScreen == ELIFE_EJoinScreen.LINKING
			&& payload.url == reply.linkUrl && payload.code == reply.linkPin && payload.hint == "",
			"connect: active and not linked shows the link screen with its url, code and no hint",
			string.Format("state=%1 screen=%2 hint=%3", s.m_eState, s.m_eTestLastScreen, payload.hint));
	}

	//------------------------------------------------------------------------------------------------
	//! Active and linked goes past the link screen even when a URL is present; with a character listed that is SELECTING.
	protected static void Test_Connect_ActiveLinkedWithCharactersSelects()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("connect: linked with characters");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartConnectForTests(mgr, -283, "t-linked-select", s);
		ELIFE_PlayerConnectedResponse reply = MakeConnectReply(ELIFE_PlayerSessionStatus.Active);
		reply.linkPin = "PIN-TEST-5";
		reply.linkUrl = "http://test.invalid/link-five";
		ELIFE_CharacterSummary summary = new ELIFE_CharacterSummary();
		summary.characterId = "c-1";
		summary.name = "Anna Smith";
		reply.characters.Insert(summary);
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, reply);

		Check(s.m_eState == ELIFE_EJoinState.SELECTING && s.m_eTestLastScreen == ELIFE_EJoinScreen.SELECTING && s.FindCharacterName("c-1") == "Anna Smith",
			"connect: active and linked with a character goes to SELECTING, not LINKING",
			string.Format("state=%1 screen=%2", s.m_eState, s.m_eTestLastScreen));
	}

	//------------------------------------------------------------------------------------------------
	//! Not linked but no URL from the backend: nothing to show on the link screen, so the player goes to the character screen (CREATING, no characters).
	protected static void Test_Connect_NotLinkedNoUrlGoesToCharacters()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("connect: not linked, no url");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartConnectForTests(mgr, -284, "t-no-url", s);
		ELIFE_PlayerConnectedResponse reply = MakeConnectReply(ELIFE_PlayerSessionStatus.Active);
		reply.webAccountLinked = false;
		reply.linkPin = "";
		reply.linkUrl = "";
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, reply);

		Check(s.m_eState == ELIFE_EJoinState.CREATING && s.m_eTestLastScreen == ELIFE_EJoinScreen.CREATING,
			"connect: not linked without a url goes to CREATING, no link screen",
			string.Format("state=%1 screen=%2", s.m_eState, s.m_eTestLastScreen));
	}

	//------------------------------------------------------------------------------------------------
	//! Recheck answered linked with a character: the character screen (SELECTING), not the link screen.
	protected static void Test_Recheck_LinkedWithCharactersSelects()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("recheck: linked with characters");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartRecheckForTests(mgr, -285, "t-recheck-linked", s);
		ELIFE_PlayerConnectedResponse reply = MakeConnectReply(ELIFE_PlayerSessionStatus.Active);
		ELIFE_CharacterSummary summary = new ELIFE_CharacterSummary();
		summary.characterId = "c-1";
		summary.name = "Anna Smith";
		reply.characters.Insert(summary);
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, reply);

		Check(s.m_eState == ELIFE_EJoinState.SELECTING && s.m_eTestLastScreen == ELIFE_EJoinScreen.SELECTING && !s.m_bRechecking,
			"recheck: linked answer goes to SELECTING, not LINKING",
			string.Format("state=%1 screen=%2 rechecking=%3", s.m_eState, s.m_eTestLastScreen, s.m_bRechecking));
	}

	//------------------------------------------------------------------------------------------------
	//! Recheck still not linked: back on the link screen with the NotYet hint and the new code and URL.
	protected static void Test_Recheck_StillNotLinkedShowsNotYet()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("recheck: still not linked");
		if (!mgr)
			return;

		ELIFE_JoinSession s;
		ELIFE_JoinRequestContext ctx = StartRecheckForTests(mgr, -286, "t-recheck-notyet", s);
		ELIFE_PlayerConnectedResponse reply = MakeConnectReply(ELIFE_PlayerSessionStatus.Active);
		reply.webAccountLinked = false;
		reply.linkPin = "PIN-TEST-2";
		reply.linkUrl = "http://test.invalid/link-two";
		mgr.OnPlayerConnectedResult(ctx, ELIFE_EApiStatusCode.SUCCESS, reply);

		ELIFE_JoinLinkPayload payload = new ELIFE_JoinLinkPayload();
		payload.ExpandFromRAW(s.m_sTestLastPayload);
		Check(s.m_eState == ELIFE_EJoinState.LINKING && s.m_eTestLastScreen == ELIFE_EJoinScreen.LINKING && !s.m_bRechecking
			&& payload.hint == "#ELIFE-Join_LinkNotYet" && payload.url == reply.linkUrl && payload.code == reply.linkPin,
			"recheck: still not linked returns to LINKING with LinkNotYet, new url and code",
			string.Format("state=%1 screen=%2 rechecking=%3 hint=%4", s.m_eState, s.m_eTestLastScreen, s.m_bRechecking, payload.hint));
	}

	//------------------------------------------------------------------------------------------------
	//! A recheck that fails shows the link screen again with the last good URL and code, not blank ones. No-send hook: the recheck stays in flight until the test answers it.
	protected static void Test_Recheck_FailureKeepsLastLink()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("recheck: failure keeps last link");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		mgr.SetNoSendForTests(true);
		ELIFE_JoinSession s = MakeSession(-287, "t-recheck-keep", ELIFE_EJoinState.SESSION_PENDING, 100000);
		mgr.InsertSessionForTests(s);

		ELIFE_JoinRequestContext first = mgr.BeginRequest(s);
		ELIFE_PlayerConnectedResponse reply = MakeConnectReply(ELIFE_PlayerSessionStatus.Active);
		reply.webAccountLinked = false;
		reply.linkPin = "PIN-TEST-3";
		reply.linkUrl = "http://test.invalid/link-three";
		mgr.OnPlayerConnectedResult(first, ELIFE_EApiStatusCode.SUCCESS, reply);

		mgr.HandleRequestRecheck(-287);
		ELIFE_JoinRequestContext recheckCtx = mgr.RequestContextForTests(-287);
		if (!recheckCtx)
		{
			Check(false, "recheck: failure keeps last link", "no recheck request in flight");
			return;
		}

		mgr.OnPlayerConnectedResult(recheckCtx, ELIFE_EApiStatusCode.ERROR, null);

		ELIFE_JoinLinkPayload payload = new ELIFE_JoinLinkPayload();
		payload.ExpandFromRAW(s.m_sTestLastPayload);
		Check(s.m_eState == ELIFE_EJoinState.LINKING && payload.hint == "#ELIFE-Join_ErrorBackend" && payload.url == reply.linkUrl && payload.code == reply.linkPin,
			"recheck: failure shows the last link screen again with its url and code",
			string.Format("state=%1 hint=%2", s.m_eState, payload.hint));
	}

	//------------------------------------------------------------------------------------------------
	//! Continue without linking from LINKING with no characters: CREATING, the skip flag set, and the code and URL dropped.
	protected static void Test_SkipLink_NoCharactersCreates()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("skip link: no characters");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		ELIFE_JoinSession s = MakeSession(-288, "t-skip-create", ELIFE_EJoinState.LINKING, 100000);
		s.m_sLinkPin = "PIN-TEST-4";
		s.m_sLinkUrl = "http://test.invalid/link-four";
		mgr.InsertSessionForTests(s);

		mgr.HandleRequestSkipLink(-288);

		Check(s.m_eState == ELIFE_EJoinState.CREATING && s.m_eTestLastScreen == ELIFE_EJoinScreen.CREATING && s.m_bLinkSkipped && s.m_sLinkPin == "" && s.m_sLinkUrl == "",
			"skip link: LINKING with no characters goes to CREATING, flag set, code and url dropped",
			string.Format("state=%1 screen=%2 skipped=%3", s.m_eState, s.m_eTestLastScreen, s.m_bLinkSkipped));
	}

	//------------------------------------------------------------------------------------------------
	//! Continue without linking from LINKING with a character listed: SELECTING with that character.
	protected static void Test_SkipLink_WithCharactersSelects()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("skip link: with characters");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		ELIFE_JoinSession s = MakeSession(-289, "t-skip-select", ELIFE_EJoinState.LINKING, 100000);
		s.m_sLinkPin = "PIN-TEST-6";
		s.m_sLinkUrl = "http://test.invalid/link-six";
		s.m_aCharacters.Insert(MakeEntry("c-1", "Anna Smith"));
		mgr.InsertSessionForTests(s);

		mgr.HandleRequestSkipLink(-289);

		Check(s.m_eState == ELIFE_EJoinState.SELECTING && s.m_eTestLastScreen == ELIFE_EJoinScreen.SELECTING && s.m_bLinkSkipped && s.m_sLinkPin == "" && s.m_sLinkUrl == "",
			"skip link: LINKING with a character goes to SELECTING, flag set, code and url dropped",
			string.Format("state=%1 screen=%2 skipped=%3", s.m_eState, s.m_eTestLastScreen, s.m_bLinkSkipped));
	}

	//------------------------------------------------------------------------------------------------
	//! Skip is refused outside LINKING: a CREATING session keeps its state, its payload and does not get the skip flag.
	protected static void Test_SkipLink_IgnoredOutsideLinking()
	{
		ELIFE_JoinSessionManagerComponent mgr = RequireManager("skip link: ignored outside linking");
		if (!mgr)
			return;

		mgr.ClearSessionsForTests();
		mgr.SetClockOverrideForTests(100000);
		ELIFE_JoinSession s = MakeSession(-290, "t-skip-creating", ELIFE_EJoinState.CREATING, 100000);
		s.m_sTestLastPayload = "sentinel";
		mgr.InsertSessionForTests(s);

		mgr.HandleRequestSkipLink(-290);

		Check(s.m_eState == ELIFE_EJoinState.CREATING && !s.m_bLinkSkipped && s.m_sTestLastPayload == "sentinel",
			"skip link: ignored in CREATING", string.Format("state=%1 skipped=%2 payload=%3", s.m_eState, s.m_bLinkSkipped, s.m_sTestLastPayload));
	}

	//------------------------------------------------------------------------------------------------
	protected static void Test_Connect_ParseStatusString()
	{
		Check(ELIFE_JoinSessionManagerComponent.ParseStatusString("active") == ELIFE_PlayerSessionStatus.Active
			&& ELIFE_JoinSessionManagerComponent.ParseStatusString("unlinked") == ELIFE_PlayerSessionStatus.Unlinked
			&& ELIFE_JoinSessionManagerComponent.ParseStatusString("blocked") == ELIFE_PlayerSessionStatus.Blocked
			&& ELIFE_JoinSessionManagerComponent.ParseStatusString("not_whitelisted") == ELIFE_PlayerSessionStatus.NotWhitelisted
			&& ELIFE_JoinSessionManagerComponent.ParseStatusString("bogus") == ELIFE_PlayerSessionStatus.Unknown,
			"connect: status strings map", "mapping differs from spec");
	}
}
#endif
