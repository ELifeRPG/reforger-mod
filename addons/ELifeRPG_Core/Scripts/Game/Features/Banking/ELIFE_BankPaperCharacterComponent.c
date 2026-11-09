//------------------------------------------------------------------------------------------------
[EntityEditorProps(category: "ELifeRPG/Banking", description: "Carries a player's request to tear up a bank paper to the server")]
class ELIFE_BankPaperCharacterComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! Receipts pile up, so a player can tear one up: from the inventory or where it lies. The server deletes it.
class ELIFE_BankPaperCharacterComponent : ScriptComponent
{
	//! Same reach as picking an item up.
	protected static const float TEAR_UP_MAX_DISTANCE = 3;

	//------------------------------------------------------------------------------------------------
	//! False if it isn't a bank paper this character can reach. Asked on the client before offering it, and again on the server.
	bool CanTearUp(IEntity item)
	{
		if (!item || !ELIFE_BankPaperComponent.Find(item))
			return false;

		SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(GetOwner().FindComponent(SCR_InventoryStorageManagerComponent));
		if (inventory && inventory.Contains(item))
			return true;

		InventoryItemComponent inventoryItem = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
		if (!inventoryItem || inventoryItem.GetParentSlot() || inventoryItem.IsLocked() || item.GetParent())
			return false;

		return vector.Distance(item.GetOrigin(), GetOwner().GetOrigin()) <= TEAR_UP_MAX_DISTANCE;
	}

	//------------------------------------------------------------------------------------------------
	void RequestTearUp(IEntity item)
	{
		if (!CanTearUp(item))
			return;

		RplComponent rpl = RplComponent.Cast(item.FindComponent(RplComponent));
		if (rpl)
			Rpc(RpcAsk_TearUp, rpl.Id());
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_TearUp(RplId itemId)
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(GetOwner());
		if (!character || !character.GetCharacterController() || character.GetCharacterController().IsDead())
			return;

		RplComponent rpl = RplComponent.Cast(Replication.FindItem(itemId));
		if (!rpl)
			return;

		IEntity item = rpl.GetEntity();
		if (!CanTearUp(item))
		{
			Print("ELIFE_BankPaperCharacterComponent | tear up rejected, not a bank paper carried or within reach", LogLevel.WARNING);
			Rpc(RpcDo_TearUpRefused, itemId);
			return;
		}

		SCR_EntityHelper.DeleteEntityAndChildren(item);
	}

	//------------------------------------------------------------------------------------------------
	//! The inventory already faded the paper out; it comes back.
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_TearUpRefused(RplId itemId)
	{
		RplComponent rpl = RplComponent.Cast(Replication.FindItem(itemId));
		SCR_InventoryMenuUI menu = SCR_InventoryMenuUI.GetInventoryMenu();
		if (rpl && menu)
			menu.ELIFE_OnTearUpRefused(rpl.GetEntity());
	}
}

//------------------------------------------------------------------------------------------------
//! "Tear up" on a paper lying in the world, beside "Pick up".
class ELIFE_BankPaperTearUpAction : ScriptedUserAction
{
	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		ELIFE_BankPaperCharacterComponent requests = ELIFE_BankPaperCharacterComponent.Cast(pUserEntity.FindComponent(ELIFE_BankPaperCharacterComponent));
		if (requests)
			requests.RequestTearUp(pOwnerEntity);
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		if (!user)
			return false;

		ELIFE_BankPaperCharacterComponent requests = ELIFE_BankPaperCharacterComponent.Cast(user.FindComponent(ELIFE_BankPaperCharacterComponent));
		return requests && requests.CanTearUp(GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	//! The request carries it to the server.
	override bool HasLocalEffectOnlyScript()
	{
		return true;
	}
}
