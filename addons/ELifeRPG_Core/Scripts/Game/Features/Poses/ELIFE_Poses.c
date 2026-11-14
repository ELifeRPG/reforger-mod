//------------------------------------------------------------------------------------------------
//! Character helpers shared by the props characters take a pose on (seats, beds). A pose is a vanilla loiter on the
//! prop's entity (or a seat point of it).
class ELIFE_Poses
{
	protected static ref array<IEntity> s_aStarting = {};

	//------------------------------------------------------------------------------------------------
	static SCR_CharacterControllerComponent GetController(IEntity character)
	{
		ChimeraCharacter chimera = ChimeraCharacter.Cast(character);
		if (!chimera)
			return null;

		return SCR_CharacterControllerComponent.Cast(chimera.GetCharacterController());
	}

	//------------------------------------------------------------------------------------------------
	//! The character is alive and holds a pose on that entity.
	static bool IsInPoseOn(IEntity character, IEntity entity)
	{
		SCR_CharacterControllerComponent controller = GetController(character);
		return controller && entity && !controller.IsDead() && controller.IsLoiteringOnEntity(entity);
	}

	//------------------------------------------------------------------------------------------------
	//! Poses start where the character is owned: the player's machine, the server for NPCs.
	static bool IsOwned(IEntity character)
	{
		ChimeraCharacter chimera = ChimeraCharacter.Cast(character);
		if (!chimera)
			return false;

		RplComponent rpl = chimera.GetRplComponent();
		return !rpl || rpl.IsOwner();
	}

	//------------------------------------------------------------------------------------------------
	//! Where the character is owned: ends his pose at once, also one whose stop is stuck (vanilla ignores further
	//! stops while one runs), and tells the other machines like vanilla does when a pose ends.
	static void FinishPose(IEntity character)
	{
		SCR_CharacterControllerComponent controller = GetController(character);
		if (!controller)
			return;

		CharacterAnimationComponent animation = controller.GetAnimationComponent();
		if (!animation)
			return;

		SCR_CharacterCommandLoiter loiter = SCR_CharacterCommandLoiter.Cast(animation.GetCommandScripted());
		if (!loiter)
			return;

		loiter.FinishLoiter();
		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(animation.GetCommandHandler());
		if (handler)
			handler.BroadCastLoiterFinish();
	}

	//------------------------------------------------------------------------------------------------
	//! Where owned: the character is on his way into a pose that vanilla doesn't see yet (a bed's turn and lie-down).
	static void SetStarting(IEntity character, bool starting)
	{
		for (int i = s_aStarting.Count() - 1; i >= 0; i--)
		{
			if (!s_aStarting[i])
				s_aStarting.Remove(i);
		}

		if (!character)
			return;

		if (!starting)
			s_aStarting.RemoveItem(character);
		else if (!s_aStarting.Contains(character))
			s_aStarting.Insert(character);
	}

	//------------------------------------------------------------------------------------------------
	//! Not busy with another pose, one on its way (SetStarting) or a blocking animation (vanilla hides loiter actions then).
	static bool CanStartPose(IEntity character)
	{
		SCR_ChimeraCharacter chimera = SCR_ChimeraCharacter.Cast(character);
		return chimera && !s_aStarting.Contains(chimera) && !SCR_ScriptedUserAction.IsUserPlayingBlockingAnimation(chimera);
	}
}
