#pragma once

// DEV ONLY (DDC_DEV_COMMANDS): helpers for the dev console commands and the automatic test run.
// Not compiled into release builds.

#if DDC_DEV_COMMANDS

#include "CoreMinimal.h"
#include "FGCrate.h"

class UFGItemDescriptor;

namespace DDC::Dev
{
	/** Finds a loaded item descriptor class by name: Desc_IronPlate_C, Desc_IronPlate or IronPlate. */
	TSubclassOf<UFGItemDescriptor> FindItemClass(const FString& Name);

	/** One stack request for SpawnCrate. Count is split into full stacks. */
	struct FItemCount
	{
		TSubclassOf<UFGItemDescriptor> Item;
		int32 Count = 0;
	};

	/** Spawns a crate of the given type next to the first player (or at Offset from the world origin), never merged. */
	AFGCrate* SpawnCrate(UWorld* World, const TArray<FItemCount>& Items, EFGCrateType Type, const FVector& Offset);

	/** Total number of items in a crate (0 if the crate is gone). */
	int32 CountItems(const AFGCrate* Crate);

	/** Number of items of one class in a crate (0 if the crate is gone). */
	int32 CountItems(const AFGCrate* Crate, TSubclassOf<UFGItemDescriptor> Item);

	/** Number of non-empty slots in a crate. */
	int32 CountFilledSlots(const AFGCrate* Crate);

	/** Starts the automatic test run (DDCDevAutoTest.cpp) if the game was launched with -DDCAutoTest. Server only. */
	void MaybeStartAutoTest(UWorld* World);

	/** Starts the client-side crate log (DDCDevAutoTest.cpp) if the game was launched with -DDCClientWatch. */
	void StartClientWatchIfRequested();
}

#endif
