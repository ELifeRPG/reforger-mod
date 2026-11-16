//------------------------------------------------------------------------------------------------
//! Every machine (physics is local). A seat/bed occupant's collision stays at his origin while the clips move the body,
//! so it is off in the slot; in the seated/lying idle a local box around his body blocks others instead.
modded class SCR_CompartmentAccessComponent
{
	//! The engine may still set the layer right after the entry; set ours again after this.
	protected static const int ELIFE_REAPPLY_MS = 1000;
	protected static const int ELIFE_BODY_CHECK_MS = 250;
	//! Added around the bones on each side: they run through the middle of the limbs.
	protected static const float ELIFE_BODY_MARGIN = 0.12;
	protected static const ref array<string> ELIFE_BODY_BONES = {"head", "hips", "pelvis", "spine3", "leftfoot", "rightfoot", "leftknee", "rightknee", "lefthand", "righthand", "leftforearm", "rightforearm"};

	//! His layer from before the slot; -1: collision not changed by us.
	protected int m_iElifeLayerBefore = -1;
	//! His geoms' own layers from before: they are the hit zones, put back so shots still hit him.
	protected ref array<int> m_aElifeGeomLayers = {};
	//! Local, not replicated: the box others bump into while he sits/lies.
	protected IEntity m_ElifeBodyBox;
	protected static AnimationTagID s_iElifeIdleTag = -1;
	protected static AnimationTagID s_iElifeStoodUpTag = -1;

	//------------------------------------------------------------------------------------------------
	override protected void OnCompartmentEntered(IEntity targetEntity, BaseCompartmentManagerComponent manager, int mgrID, int slotID, bool move)
	{
		super.OnCompartmentEntered(targetEntity, manager, mgrID, slotID, move);

		if (manager && ELIFE_PoseStandUp.IsPoseSlot(manager.FindCompartment(slotID, mgrID)))
		{
			ElifeCollisionOff();
			GetGame().GetCallqueue().CallLater(ElifeCollisionOff, ELIFE_REAPPLY_MS);
			GetGame().GetCallqueue().Remove(ElifeUpdateBodyBox);
			GetGame().GetCallqueue().CallLater(ElifeUpdateBodyBox, ELIFE_BODY_CHECK_MS, true);
		}
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnCompartmentLeft(IEntity targetEntity, BaseCompartmentManagerComponent manager, int mgrID, int slotID, bool move)
	{
		super.OnCompartmentLeft(targetEntity, manager, mgrID, slotID, move);

		GetGame().GetCallqueue().Remove(ElifeCollisionOff);
		GetGame().GetCallqueue().Remove(ElifeUpdateBodyBox);
		ElifeRemoveBodyBox();
		Physics physics = GetOwner().GetPhysics();
		if (m_iElifeLayerBefore != -1 && physics)
		{
			physics.SetInteractionLayer(m_iElifeLayerBefore);
			ElifeRestoreGeomLayers(physics);
		}

		m_iElifeLayerBefore = -1;
		m_aElifeGeomLayers.Clear();
	}

	//------------------------------------------------------------------------------------------------
	protected void ElifeRestoreGeomLayers(notnull Physics physics)
	{
		for (int i = 0, count = Math.Min(physics.GetNumGeoms(), m_aElifeGeomLayers.Count()); i < count; i++)
		{
			physics.SetGeomInteractionLayer(i, m_aElifeGeomLayers[i]);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void ElifeCollisionOff()
	{
		Physics physics = GetOwner().GetPhysics();
		if (!physics || !ELIFE_PoseStandUp.IsPoseSlot(GetCompartment()))
			return;

		if (m_iElifeLayerBefore == -1 && physics.GetInteractionLayer() != EPhysicsLayerPresets.CharNoCollide)
		{
			m_iElifeLayerBefore = physics.GetInteractionLayer();
			m_aElifeGeomLayers.Clear();
			for (int i = 0, count = physics.GetNumGeoms(); i < count; i++)
			{
				m_aElifeGeomLayers.Insert(physics.GetGeomInteractionLayer(i));
			}
		}

		physics.SetInteractionLayer(EPhysicsLayerPresets.CharNoCollide);
		ElifeRestoreGeomLayers(physics);
	}

	//------------------------------------------------------------------------------------------------
	//! The box exists only in the seated/lying idle: the get-in and stand-up clips move the body out of it.
	protected void ElifeUpdateBodyBox()
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(GetOwner());
		if (!character || !ELIFE_PoseStandUp.IsPoseSlot(GetCompartment()))
		{
			GetGame().GetCallqueue().Remove(ElifeUpdateBodyBox);
			ElifeRemoveBodyBox();
			return;
		}

		if (s_iElifeIdleTag == -1)
		{
			s_iElifeIdleTag = GameAnimationUtils.RegisterAnimationTag("TagVehicleIdle");
			s_iElifeStoodUpTag = GameAnimationUtils.RegisterAnimationTag("TagELIFEStoodUp");
		}

		CharacterAnimationComponent animation = character.GetAnimationComponent();
		bool idle = animation && animation.IsPrimaryTag(s_iElifeIdleTag) && !animation.IsPrimaryTag(s_iElifeStoodUpTag);
		if (!idle)
			ElifeRemoveBodyBox();
		else if (!m_ElifeBodyBox)
			ElifeCreateBodyBox(character);
	}

	//------------------------------------------------------------------------------------------------
	//! Spans his bones in his own frame, on a layer bullets should pass so shots still hit his body.
	protected void ElifeCreateBodyBox(notnull ChimeraCharacter character)
	{
		Animation anim = character.GetAnimation();
		if (!anim)
			return;

		vector min = "1000 1000 1000";
		vector max = "-1000 -1000 -1000";
		int found;
		vector bone[4];
		foreach (string name : ELIFE_BODY_BONES)
		{
			TNodeId index = anim.GetBoneIndex(name);
			if (index == -1)
				continue;

			anim.GetBoneMatrix(index, bone);
			for (int axis = 0; axis < 3; axis++)
			{
				min[axis] = Math.Min(min[axis], bone[3][axis]);
				max[axis] = Math.Max(max[axis], bone[3][axis]);
			}

			found++;
		}

		if (found < 3)
			return;

		vector margin = Vector(ELIFE_BODY_MARGIN, ELIFE_BODY_MARGIN, ELIFE_BODY_MARGIN);
		min = min - margin;
		max = max + margin;

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		character.GetWorldTransform(params.Transform);
		params.Transform[3] = character.CoordToParent((min + max) * 0.5);
		m_ElifeBodyBox = GetGame().SpawnEntity(GenericEntity, character.GetWorld(), params);
		if (!m_ElifeBodyBox)
			return;

		autoptr PhysicsGeomDef geoms[] = {PhysicsGeomDef("", PhysicsGeom.CreateBox(max - min), "", EPhysicsLayerDefs.Static)};
		Physics.CreateStaticEx(m_ElifeBodyBox, geoms);
	}

	//------------------------------------------------------------------------------------------------
	protected void ElifeRemoveBodyBox()
	{
		if (m_ElifeBodyBox)
			SCR_EntityHelper.DeleteEntityAndChildren(m_ElifeBodyBox);

		m_ElifeBodyBox = null;
	}
}
