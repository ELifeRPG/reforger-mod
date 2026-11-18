//------------------------------------------------------------------------------------------------
//! Server-side: who points a weapon at an NPC (see ELIFE_NpcComponent's threatened state). Vanilla has no such perception.
class ELIFE_NpcThreat
{
	protected static const float RANGE = 20;
	//! How far the barrel's line may pass his chest. A distance, not an angle: the same at 2 m and at 20 m.
	protected static const float MISS_DISTANCE = 0.6;
	protected static const vector CHEST = "0 1.3 0";

	//------------------------------------------------------------------------------------------------
	//! A player's character pointing a raised weapon at target, in range and with a clear line, else null.
	static IEntity FindAimingAt(notnull IEntity target)
	{
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return null;

		array<int> playerIds = {};
		playerManager.GetPlayers(playerIds);
		vector chest = target.GetOrigin() + CHEST;
		foreach (int playerId : playerIds)
		{
			ChimeraCharacter character = ChimeraCharacter.Cast(playerManager.GetPlayerControlledEntity(playerId));
			if (character && character != target && IsAimingAt(character, target, chest))
				return character;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsAimingAt(notnull ChimeraCharacter character, notnull IEntity target, vector point)
	{
		if (character.IsInVehicle() || vector.DistanceSq(character.GetOrigin(), point) > RANGE * RANGE)
			return false;

		CharacterControllerComponent controller = character.GetCharacterController();
		if (!controller || controller.IsDead() || !controller.IsWeaponRaised())
			return false;

		BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(character.FindComponent(BaseWeaponManagerComponent));
		if (!weapons)
			return false;

		BaseWeaponComponent weapon = weapons.GetCurrentWeapon();
		if (!weapon)
			return false;

		EWeaponType type = weapon.GetWeaponType();
		if (type == EWeaponType.WT_NONE || type == EWeaponType.WT_FRAGGRENADE || type == EWeaponType.WT_SMOKEGRENADE)
			return false;

		vector muzzle[4];
		if (!weapons.GetCurrentMuzzleTransform(muzzle))
			return false;

		vector toPoint = point - muzzle[3];
		float along = vector.Dot(toPoint, muzzle[2]);
		if (along <= 0 || (toPoint - muzzle[2] * along).Length() > MISS_DISTANCE)
			return false;

		//! Not through a wall.
		TraceParam trace = new TraceParam();
		trace.Start = muzzle[3];
		trace.End = point;
		trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		trace.LayerMask = EPhysicsLayerDefs.Projectile;
		array<IEntity> exclude = {character, weapon.GetOwner(), target};
		trace.ExcludeArray = exclude;
		return character.GetWorld().TraceMove(trace, null) >= 1;
	}
}
