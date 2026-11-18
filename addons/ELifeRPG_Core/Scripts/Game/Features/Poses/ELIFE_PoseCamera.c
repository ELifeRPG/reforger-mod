//------------------------------------------------------------------------------------------------
//! First person on seats/beds: the upper body's rotation since getting in is applied under the free look. The view
//! starts where his camera looked before getting in and that offset fades out over CARRY_FADE_MS.
modded class CharacterCamera1stPersonVehicle
{
	protected bool m_bElifeInPose;
	//! Upper body axes when he got in (model space) and the turn that makes the first view the one from before.
	protected vector m_aElifeHeadRef[3];
	protected vector m_aElifeCarry[3];
	protected float m_aElifeCarryQuat[4];
	protected int m_iElifeCarrySince;

	protected static const int CARRY_FADE_MS = 800;

	//------------------------------------------------------------------------------------------------
	override void OnUpdate(float pDt, out ScriptedCameraItemResult pOutResult)
	{
		super.OnUpdate(pDt, pOutResult);

		if (!m_OwnerCharacter || !ELIFE_Poses.GetPoseSlot(m_OwnerCharacter))
		{
			m_bElifeInPose = false;
			return;
		}

		Animation anim = m_OwnerCharacter.GetAnimation();
		if (!anim)
			return;

		// Not the head bone: head aiming follows the free look, which would count it twice.
		TNodeId headBone = anim.GetBoneIndex("Spine5");
		if (headBone == -1)
			headBone = anim.GetBoneIndex("Spine4");

		if (headBone == -1)
			headBone = anim.GetBoneIndex("Spine3");

		if (headBone == -1)
			return;

		vector head[4];
		anim.GetBoneMatrix(headBone, head);
		vector view[3] = {pOutResult.m_CameraTM[0], pOutResult.m_CameraTM[1], pOutResult.m_CameraTM[2]};
		if (!m_bElifeInPose)
		{
			m_bElifeInPose = true;
			m_aElifeHeadRef[0] = head[0];
			m_aElifeHeadRef[1] = head[1];
			m_aElifeHeadRef[2] = head[2];
			ElifeSetCarry(view);
			Math3D.MatrixToQuat(m_aElifeCarry, m_aElifeCarryQuat);
			m_iElifeCarrySince = System.GetTickCount();
		}

		float fade = Math.Clamp((System.GetTickCount() - m_iElifeCarrySince) / (float)CARRY_FADE_MS, 0, 1);
		fade = 1 - (1 - fade) * (1 - fade);
		float identity[4];
		Math3D.QuatIdentity(identity);
		float carryQuat[4];
		Math3D.QuatLerp(carryQuat, m_aElifeCarryQuat, identity, fade);
		Math3D.QuatNormalize(carryQuat);
		vector carry[3];
		Math3D.QuatToMatrix(carryQuat, carry);

		// Rotation since getting in, model space: Ref^T * Now.
		vector headNow[3] = {head[0], head[1], head[2]};
		vector refT[3];
		ElifeTranspose(m_aElifeHeadRef, refT);
		vector turn[3];
		ElifeMultiply(refT, headNow, turn);

		vector carried[3];
		ElifeMultiply(view, carry, carried);

		vector result[3];
		ElifeMultiply(carried, turn, result);
		pOutResult.m_CameraTM[0] = result[0];
		pOutResult.m_CameraTM[1] = result[1];
		pOutResult.m_CameraTM[2] = result[2];
	}

	//------------------------------------------------------------------------------------------------
	protected void ElifeSetCarry(vector view[3])
	{
		m_aElifeCarry[0] = "1 0 0";
		m_aElifeCarry[1] = "0 1 0";
		m_aElifeCarry[2] = "0 0 1";
		vector before[3];
		if (!ELIFE_PosePropComponent.TakeLookCarry(m_OwnerCharacter, before))
			return;

		vector local[3];
		for (int i = 0; i < 3; i++)
		{
			local[i] = m_OwnerCharacter.VectorToLocal(before[i]);
		}

		// view * Carry = local  ->  Carry = view^T * local
		vector viewT[3];
		ElifeTranspose(view, viewT);
		ElifeMultiply(viewT, local, m_aElifeCarry);
	}

	//------------------------------------------------------------------------------------------------
	protected static void ElifeTranspose(vector m[3], out vector t[3])
	{
		t[0] = Vector(m[0][0], m[1][0], m[2][0]);
		t[1] = Vector(m[0][1], m[1][1], m[2][1]);
		t[2] = Vector(m[0][2], m[1][2], m[2][2]);
	}

	//------------------------------------------------------------------------------------------------
	//! o = a * b, row vectors.
	protected static void ElifeMultiply(vector a[3], vector b[3], out vector o[3])
	{
		for (int r = 0; r < 3; r++)
		{
			// a[r][i] on an array parameter with a variable index doesn't compile.
			vector row = a[r];
			o[r] = b[0] * row[0] + b[1] * row[1] + b[2] * row[2];
		}
	}
}
