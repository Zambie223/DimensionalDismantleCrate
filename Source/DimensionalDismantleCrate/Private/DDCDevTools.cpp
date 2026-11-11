#include "DDCDevTools.h"

#if DDC_DEV_COMMANDS

#include "Engine/World.h"
#include "FGInventoryComponent.h"
#include "FGItemPickup_Spawnable.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Resources/FGItemDescriptor.h"
#include "UObject/UObjectIterator.h"

namespace DDC::Dev
{
	TSubclassOf<UFGItemDescriptor> FindItemClass(const FString& Name)
	{
		const FString Wanted = Name.EndsWith(TEXT("_C")) ? Name : Name + TEXT("_C");
		const FString WantedWithPrefix = Wanted.StartsWith(TEXT("Desc_")) ? Wanted : TEXT("Desc_") + Wanted;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (Class->IsChildOf(UFGItemDescriptor::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract)
				&& (Class->GetName().Equals(Wanted, ESearchCase::IgnoreCase) || Class->GetName().Equals(WantedWithPrefix, ESearchCase::IgnoreCase)))
			{
				return Class;
			}
		}
		return nullptr;
	}

	AFGCrate* SpawnCrate(UWorld* World, const TArray<FItemCount>& Items, const EFGCrateType Type, const FVector& Offset)
	{
		if (!World)
		{
			return nullptr;
		}
		TArray<FInventoryStack> Stacks;
		for (const FItemCount& Entry : Items)
		{
			if (!Entry.Item || Entry.Count <= 0)
			{
				continue;
			}
			// Split into full stacks, the way a dismantle refund arrives.
			const int32 StackSize = FMath::Max(1, UFGItemDescriptor::GetStackSize(Entry.Item));
			for (int32 Left = Entry.Count; Left > 0; Left -= StackSize)
			{
				Stacks.Emplace(FMath::Min(Left, StackSize), Entry.Item);
			}
		}
		if (Stacks.Num() == 0)
		{
			return nullptr;
		}

		FSpawnInventoryCrateParameters Params;
		Params.CrateType = Type;
		Params.bAllowCrateMerging = false;
		FVector Location = Offset;
		APlayerController* Controller = World->GetFirstPlayerController();
		if (APawn* Pawn = Controller ? Controller->GetPawn() : nullptr)
		{
			Location += Pawn->GetActorLocation();
			Params.ActorsToIgnore.Add(Pawn);
		}
		return AFGItemPickup_Spawnable::SpawnInventoryCrate(World, Stacks, Location, Params);
	}

	int32 CountItems(const AFGCrate* Crate)
	{
		return CountItems(Crate, nullptr);
	}

	int32 CountItems(const AFGCrate* Crate, const TSubclassOf<UFGItemDescriptor> Item)
	{
		const UFGInventoryComponent* Inventory = IsValid(Crate) && !Crate->IsActorBeingDestroyed() ? Crate->GetInventory() : nullptr;
		if (!Inventory)
		{
			return 0;
		}
		int32 Total = 0;
		for (int32 Slot = 0; Slot < Inventory->GetSizeLinear(); ++Slot)
		{
			FInventoryStack Stack;
			if (Inventory->GetStackFromIndex(Slot, Stack) && Stack.HasItems() && (!Item || Stack.Item.GetItemClass() == Item))
			{
				Total += Stack.NumItems;
			}
		}
		return Total;
	}

	int32 CountFilledSlots(const AFGCrate* Crate)
	{
		const UFGInventoryComponent* Inventory = IsValid(Crate) && !Crate->IsActorBeingDestroyed() ? Crate->GetInventory() : nullptr;
		if (!Inventory)
		{
			return 0;
		}
		int32 Filled = 0;
		for (int32 Slot = 0; Slot < Inventory->GetSizeLinear(); ++Slot)
		{
			Filled += Inventory->IsIndexEmpty(Slot) ? 0 : 1;
		}
		return Filled;
	}
}

#endif
