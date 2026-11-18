//------------------------------------------------------------------------------------------------
//! An NPC's frightened pose: a custom loiter from our own anim graph. It turns him towards the gunman, and it ends from
//! the NPC after the out clip, since Workbench-exported clips can't carry vanilla's "Gesture_End" anim event.
modded class SCR_CharacterCommandLoiter
{
	//! How fast a frightened NPC's pose turns him towards the gunman, degrees per second.
	protected static const float ELIFE_POSE_TURN_SPEED = 90;
	//! Closer than this (degrees) he stays as he is.
	protected static const float ELIFE_POSE_TURN_MIN_DEG = 2;

	protected ELIFE_NpcComponent m_ElifeNpc;
	protected bool m_bElifeNpcLooked;

	//------------------------------------------------------------------------------------------------
	//! Without root motion vanilla's loiter keeps the character as he is, and the heading doesn't reach him; a
	//! frightened NPC's pose turns him towards the gunman here instead, on the server and every client alike.
	override void UpdateTransformNoRootMotion(out vector transform[4], float dt)
	{
		if (!m_bElifeNpcLooked)
		{
			m_bElifeNpcLooked = true;
			m_ElifeNpc = ELIFE_NpcComponent.Cast(m_pCharacter.FindComponent(ELIFE_NpcComponent));
		}

		//! Only the custom graph's pose; vanilla's idle and every other loiter keep the transform the engine gives.
		float target;
		if (!m_bUsingGraphAttachment || !m_ElifeNpc || !m_ElifeNpc.GetPoseYaw(target))
			return;

		float yaw = Math.Atan2(transform[2][0], transform[2][2]) * Math.RAD2DEG;
		float turn = target - yaw;
		turn -= 360 * Math.Round(turn / 360);
		if (Math.AbsFloat(turn) < ELIFE_POSE_TURN_MIN_DEG)
			return;

		float step = ELIFE_POSE_TURN_SPEED * dt;
		yaw += Math.Clamp(turn, -step, step);
		float rad = yaw * Math.DEG2RAD;
		//! Upright, facing yaw: 0 faces +Z, 90 faces +X (as ELIFE_NpcComponent.YawTo).
		transform[0] = Vector(Math.Cos(rad), 0, -Math.Sin(rad));
		transform[1] = vector.Up;
		transform[2] = Vector(Math.Sin(rad), 0, Math.Cos(rad));
	}

	//------------------------------------------------------------------------------------------------
	//! Detaches the custom graph and finishes, like the event would, on this machine only (server and every client
	//! each call it; vanilla's finish broadcast doesn't detach on clients).
	void ELIFE_DetachAndFinish()
	{
		if (m_bUsingGraphAttachment && m_customAnimData)
			PreAnim_SetAttachment(m_customAnimData.m_sGraphBindingName, string.Empty, string.Empty, string.Empty);

		//! As vanilla's detach does: a finished command left subscribed would end later loiters on an event or death.
		SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(m_pCharacter.FindComponent(SCR_CharacterControllerComponent));
		if (controller)
		{
			controller.GetOnAnimationEvent().Remove(OnAnimationEvent);
			controller.m_OnLifeStateChanged.Remove(OnLifeStateChanged);
		}

		FinishLoiter();
	}
}
