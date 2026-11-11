//------------------------------------------------------------------------------------------------
//! Client-side. NPC lines heard outside a conversation, shown in vanilla's HUD voiceover subtitles until the voice ends.
class ELIFE_NpcSubtitles
{
	protected static const float RANGE = 30;
	protected static const int CHECK_MS = 200;
	//! Short lines stay readable even if the sound is shorter or missing.
	protected static const int MIN_SHOW_MS = 1500;
	//! As long after the voice as vanilla keeps its subtitles.
	protected static const int LINGER_MS = 500;
	//! A sound that never reports its end still lets the subtitle go.
	protected static const int FAILSAFE_MS = 15000;

	protected static SCR_CommunicationSoundComponent s_Voice;
	protected static AudioHandle s_hVoice = AudioHandle.Invalid;
	protected static int s_iShownTick;

	//------------------------------------------------------------------------------------------------
	//! text and speakerName may be string table keys.
	static void Show(notnull IEntity speaker, string speakerName, string text, SCR_CommunicationSoundComponent voice, AudioHandle voiceHandle)
	{
		if (text.IsEmpty())
			return;

		CameraManager cameras = GetGame().GetCameraManager();
		if (!cameras)
			return;

		CameraBase camera = cameras.CurrentCamera();
		if (!camera || vector.Distance(camera.GetOrigin(), speaker.GetOrigin()) > RANGE)
			return;

		SCR_VoiceoverSubtitles display = GetDisplay(true);
		if (!display)
			return;

		string shown = WidgetManager.Translate(text);
		if (!speakerName.IsEmpty())
			shown = WidgetManager.Translate(speakerName) + ":<br/>" + shown;

		GetGame().GetCallqueue().Remove(Hide);
		GetGame().GetCallqueue().Remove(Check);
		display.ShowSubtitle(shown);

		s_Voice = voice;
		s_hVoice = voiceHandle;
		s_iShownTick = System.GetTickCount();
		GetGame().GetCallqueue().CallLater(Check, CHECK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	//! Hides it now, e.g. when a conversation takes over the line.
	static void Clear()
	{
		GetGame().GetCallqueue().Remove(Check);
		GetGame().GetCallqueue().Remove(Hide);
		s_Voice = null;
		s_hVoice = AudioHandle.Invalid;
		Hide();
	}

	//------------------------------------------------------------------------------------------------
	protected static void Check()
	{
		int shownFor = System.GetTickCount() - s_iShownTick;
		bool speaking = s_Voice && s_hVoice != AudioHandle.Invalid && !s_Voice.IsFinishedPlaying(s_hVoice);
		if (shownFor < MIN_SHOW_MS || (speaking && shownFor < FAILSAFE_MS))
			return;

		GetGame().GetCallqueue().Remove(Check);
		GetGame().GetCallqueue().CallLater(Hide, LINGER_MS);
	}

	//------------------------------------------------------------------------------------------------
	protected static void Hide()
	{
		SCR_VoiceoverSubtitles display = GetDisplay();
		if (display)
			display.Show(false);
	}

	//------------------------------------------------------------------------------------------------
	//! Looked up each time: the HUD is rebuilt when the player respawns.
	protected static SCR_VoiceoverSubtitles GetDisplay(bool warn = false)
	{
		SCR_HUDManagerComponent hud = GetGame().GetHUDManager();
		if (!hud)
			return null;

		SCR_VoiceoverSubtitles display = SCR_VoiceoverSubtitles.Cast(hud.FindInfoDisplay(SCR_VoiceoverSubtitles));
		if (!display && warn)
			Print("ELIFE_NpcSubtitles | the HUD has no SCR_VoiceoverSubtitles", LogLevel.WARNING);

		return display;
	}
}
