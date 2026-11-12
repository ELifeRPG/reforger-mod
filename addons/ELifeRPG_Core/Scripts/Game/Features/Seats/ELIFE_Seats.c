//------------------------------------------------------------------------------------------------
//! Seat points are sentinel smart actions tagged TAG, children of a seating prop, numbered by a TAG_n tag
//! because runtime child order isn't the prefab's.
class ELIFE_Seats
{
	static const string TAG = "ELIFE_Seat";

	//! Right at the seat point the pose briefly lifts NPCs onto the prop.
	protected static const float SIT_FORWARD = 0.07;

	//------------------------------------------------------------------------------------------------
	//! Server. Free seats within range of a position, nearest first.
	static void FindFree(vector position, float range, notnull array<SCR_AISmartActionSentinelComponent> outSeats)
	{
		outSeats.Clear();

		World world = GetGame().GetWorld();
		if (!world)
			return;

		AISmartActionSystem system = AISmartActionSystem.Cast(world.FindSystem(AISmartActionSystem));
		if (!system)
			return;

		array<AISmartActionComponent> found = {};
		system.FindSmartActions(found, position, range, {TAG}, EAIFindSmartAction_TagTest.AnySet);

		array<float> distances = {};
		foreach (AISmartActionComponent action : found)
		{
			SCR_AISmartActionSentinelComponent seat = SCR_AISmartActionSentinelComponent.Cast(action);
			if (!seat || !IsFree(seat))
				continue;

			float distance = vector.DistanceSq(position, GetPosition(seat));
			int at = 0;
			while (at < distances.Count() && distances[at] <= distance)
				at++;

			distances.InsertAt(distance, at);
			outSeats.InsertAt(seat, at);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Server. Works best from about 1 m in front of the seat, facing it.
	static bool SitDown(notnull IEntity character, notnull SCR_AISmartActionSentinelComponent seat)
	{
		SCR_CharacterControllerComponent controller = GetController(character);
		if (!controller || !IsFree(seat) || !controller.CanPlayLoiterAnimation(seat.GetLoiterAnimation()))
			return false;

		vector transform[4];
		GetSitTransform(seat, transform);
		//! The pose belongs to the seat point, not the prop: with the prop, alignment to the seat didn't happen.
		controller.StartLoitering(seat.GetOwner(), seat.GetLoiterAnimation(), false, true, true, transform);

		ELIFE_SeatPropComponent prop = GetPropComponent(seat);
		if (prop)
			prop.Take(GetIndex(seat), character);
		else
			seat.SetActionAccessible(false);

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Server. Stands an NPC up from SitDown and frees the seat.
	static void StandUp(notnull IEntity character, notnull SCR_AISmartActionSentinelComponent seat)
	{
		if (IsSitting(character, seat))
		{
			SCR_CharacterControllerComponent controller = GetController(character);
			controller.StopLoitering(false);

			//! The heading is still the walk-in one, facing the seat; without this the NPC turns all the way round first.
			vector transform[4];
			GetSitTransform(seat, transform);
			vector facing = transform[2] * -1;
			float heading = Math.Atan2(facing[0], facing[2]);
			heading -= Math.PI2 * Math.Floor(heading / Math.PI2);
			controller.SetHeadingAngle(heading, true);
		}

		ELIFE_SeatPropComponent prop = GetPropComponent(seat);
		if (prop)
			prop.Release(GetIndex(seat), character);
		else
			seat.SetActionAccessible(true);
	}

	//------------------------------------------------------------------------------------------------
	//! The character holds the sitting pose on that seat.
	static bool IsSitting(notnull IEntity character, notnull SCR_AISmartActionSentinelComponent seat)
	{
		SCR_CharacterControllerComponent controller = GetController(character);
		return controller && !controller.IsDead() && controller.IsLoiteringOnEntity(seat.GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	static ELIFE_SeatPropComponent GetPropComponent(notnull SCR_AISmartActionSentinelComponent seat)
	{
		return ELIFE_SeatPropComponent.Cast(GetProp(seat).FindComponent(ELIFE_SeatPropComponent));
	}

	//------------------------------------------------------------------------------------------------
	//! Same transform vanilla's loiter action uses.
	static void GetSitTransform(notnull SCR_AISmartActionSentinelComponent seat, out vector transform[4])
	{
		vector seatTransform[4];
		seat.GetOwner().GetWorldTransform(seatTransform);

		vector position = seatTransform[3] + seat.GetActionOffset().Multiply3(seatTransform);
		vector look = seatTransform[3] + seat.GetLookPosition().Multiply3(seatTransform);
		Math3D.DirectionAndUpMatrix(look - position, seatTransform[1], transform);

		//! The pose faces away from its target direction, so the sitter looks along -transform[2].
		transform[3] = position - transform[2] * SIT_FORWARD;
	}

	//------------------------------------------------------------------------------------------------
	//! How far, along the ground, the character is from where the pose should have put him.
	static float GetSitError(notnull IEntity character, notnull SCR_AISmartActionSentinelComponent seat)
	{
		vector transform[4];
		GetSitTransform(seat, transform);
		return vector.DistanceXZ(character.GetOrigin(), transform[3]);
	}

	//------------------------------------------------------------------------------------------------
	//! NPCs approach from here so they start the sit facing the seat; otherwise the pose swings them onto the prop.
	static vector GetApproachPoint(notnull SCR_AISmartActionSentinelComponent seat, float distance)
	{
		vector transform[4];
		GetSitTransform(seat, transform);

		return transform[3] - transform[2] * distance;
	}

	//------------------------------------------------------------------------------------------------
	protected static SCR_CharacterControllerComponent GetController(IEntity character)
	{
		ChimeraCharacter chimera = ChimeraCharacter.Cast(character);
		if (!chimera)
			return null;

		return SCR_CharacterControllerComponent.Cast(chimera.GetCharacterController());
	}

	//------------------------------------------------------------------------------------------------
	//! Server. Nobody sits there.
	static bool IsFree(notnull SCR_AISmartActionSentinelComponent seat)
	{
		ELIFE_SeatPropComponent prop = GetPropComponent(seat);
		if (prop)
			return !prop.GetSitter(GetIndex(seat));

		return seat.IsActionAccessible();
	}

	//------------------------------------------------------------------------------------------------
	//! The prop's seat points in order: its own sentinels, then its children's.
	static void GetSeats(IEntity prop, notnull array<SCR_AISmartActionSentinelComponent> outSeats)
	{
		outSeats.Clear();
		if (!prop)
			return;

		AddSeats(prop, outSeats);
		IEntity child = prop.GetChildren();
		while (child)
		{
			AddSeats(child, outSeats);
			child = child.GetSibling();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void AddSeats(notnull IEntity entity, notnull array<SCR_AISmartActionSentinelComponent> outSeats)
	{
		array<Managed> found = {};
		entity.FindComponents(SCR_AISmartActionSentinelComponent, found);
		foreach (Managed component : found)
		{
			outSeats.Insert(SCR_AISmartActionSentinelComponent.Cast(component));
		}
	}

	//------------------------------------------------------------------------------------------------
	static SCR_AISmartActionSentinelComponent GetSeat(IEntity prop, int index)
	{
		if (!prop)
			return null;

		ELIFE_SeatPropComponent component = ELIFE_SeatPropComponent.Cast(prop.FindComponent(ELIFE_SeatPropComponent));
		if (component)
			return component.GetSeat(index);

		array<SCR_AISmartActionSentinelComponent> seats = {};
		GetSeats(prop, seats);
		foreach (SCR_AISmartActionSentinelComponent seat : seats)
		{
			if (GetIndex(seat) == index)
				return seat;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! The prop a seat point belongs to: its parent, or itself if placed alone.
	static IEntity GetProp(notnull SCR_AISmartActionSentinelComponent seat)
	{
		IEntity owner = seat.GetOwner();
		IEntity parent = owner.GetParent();
		if (parent)
			return parent;

		return owner;
	}

	//------------------------------------------------------------------------------------------------
	//! The seat's number from its TAG_n tag, or -1.
	static int GetIndex(notnull SCR_AISmartActionSentinelComponent seat)
	{
		string prefix = TAG + "_";
		array<string> tags = {};
		seat.GetTags(tags);
		foreach (string tag : tags)
		{
			if (tag.StartsWith(prefix))
				return tag.Substring(prefix.Length(), tag.Length() - prefix.Length()).ToInt(-1);
		}

		return -1;
	}

	//------------------------------------------------------------------------------------------------
	//! World position of the seat point's action offset.
	static vector GetPosition(notnull SCR_AISmartActionSentinelComponent seat)
	{
		return seat.GetOwner().CoordToParent(seat.GetActionOffset());
	}
}
