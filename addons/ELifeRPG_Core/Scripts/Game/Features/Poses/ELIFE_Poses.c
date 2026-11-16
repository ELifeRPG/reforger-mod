//------------------------------------------------------------------------------------------------
//! Character helpers for seats and beds (compartment slots of ELIFE_PosePropComponent props).
class ELIFE_Poses
{
	//------------------------------------------------------------------------------------------------
	static SCR_CharacterControllerComponent GetController(IEntity character)
	{
		ChimeraCharacter chimera = ChimeraCharacter.Cast(character);
		if (!chimera)
			return null;

		return SCR_CharacterControllerComponent.Cast(chimera.GetCharacterController());
	}

	//------------------------------------------------------------------------------------------------
	//! The seat/bed slot the character is in, or null (also in a vehicle).
	static BaseCompartmentSlot GetPoseSlot(IEntity character)
	{
		ChimeraCharacter chimera = ChimeraCharacter.Cast(character);
		if (!chimera || !chimera.GetCompartmentAccessComponent())
			return null;

		BaseCompartmentSlot slot = chimera.GetCompartmentAccessComponent().GetCompartment();
		if (!ELIFE_PoseStandUp.IsPoseSlot(slot))
			return null;

		return slot;
	}

	//------------------------------------------------------------------------------------------------
	//! Player's machine for players, server for NPCs.
	static bool IsOwned(IEntity character)
	{
		ChimeraCharacter chimera = ChimeraCharacter.Cast(character);
		if (!chimera)
			return false;

		RplComponent rpl = chimera.GetRplComponent();
		return !rpl || rpl.IsOwner();
	}
}
