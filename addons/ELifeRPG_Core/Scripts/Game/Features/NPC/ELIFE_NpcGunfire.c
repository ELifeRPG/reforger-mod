//------------------------------------------------------------------------------------------------
//! Server: every gunshot, players' and AI's, frightens NPCs near the shooter (ELIFE_NpcComponent.OnGunfire).
// shortcut: vehicle turrets use SCR_OptimizedMuzzleEffectComponent, not covered (their hits still count); mod it too if turrets fire near NPCs.
modded class SCR_MuzzleEffectComponent
{
	//! A full-auto burst counts as one shot per this.
	protected static const int ELIFE_SHOT_INTERVAL_MS = 250;

	protected int m_iElifeLastShotTick;

	//------------------------------------------------------------------------------------------------
	override void OnFired(IEntity effectEntity, BaseMuzzleComponent muzzle, IEntity projectileEntity)
	{
		super.OnFired(effectEntity, muzzle, projectileEntity);

		if (!muzzle || !Replication.IsServer())
			return;

		int now = System.GetTickCount();
		if (m_iElifeLastShotTick != 0 && now - m_iElifeLastShotTick < ELIFE_SHOT_INTERVAL_MS)
			return;

		m_iElifeLastShotTick = now;
		//! This effect isn't a script component; its weapon is the muzzle's owner.
		IEntity weapon = muzzle.GetOwner();
		//! The character holding it.
		ELIFE_NpcComponent.OnGunfire(weapon.GetRootParent(), weapon.GetOrigin(), ELIFE_NpcComponent.GUNFIRE_SHOT_RANGE);
	}
}

//------------------------------------------------------------------------------------------------
//! Server: anyone hit by a bullet, players and NPCs, frightens NPCs near him; a kill is a hit too.
modded class SCR_CharacterDamageManagerComponent
{
	//! A shotgun's pellets count as one hit per this.
	protected static const int ELIFE_HIT_INTERVAL_MS = 250;

	protected int m_iElifeLastHitTick;

	//------------------------------------------------------------------------------------------------
	override protected void OnDamage(notnull BaseDamageContext damageContext)
	{
		super.OnDamage(damageContext);

		//! Bullets only: falls, melee, bleeding and explosions have their own types.
		if (damageContext.damageType != EDamageType.KINETIC || !Replication.IsServer())
			return;

		int now = System.GetTickCount();
		if (m_iElifeLastHitTick != 0 && now - m_iElifeLastHitTick < ELIFE_HIT_INTERVAL_MS)
			return;

		m_iElifeLastHitTick = now;
		IEntity victim = GetOwner();
		IEntity source;
		if (damageContext.instigator)
			source = damageContext.instigator.GetInstigatorEntity();

		if (!source)
			source = victim;

		ELIFE_NpcComponent.OnGunfire(source, victim.GetOrigin(), ELIFE_NpcComponent.GUNFIRE_HIT_RANGE, victim);
	}
}
