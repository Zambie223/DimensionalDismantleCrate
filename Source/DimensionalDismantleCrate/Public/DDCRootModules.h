#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "Module/GameWorldModule.h"
#include "DDCRootModules.generated.h"

/**
 * Root game instance module (SML discovers native root modules as well as Blueprint ones).
 * Registers the Mod Configuration asset, which has to be a Blueprint because SML's config UI widgets come from
 * its Blueprint property classes.
 */
UCLASS()
class DIMENSIONALDISMANTLECRATE_API UDimensionalDismantleCrateGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	UDimensionalDismantleCrateGameInstanceModule();

	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;
};

/** Root game world module: spawns the crate upload subsystem in game worlds (never in the main menu). */
UCLASS()
class DIMENSIONALDISMANTLECRATE_API UDimensionalDismantleCrateGameWorldModule : public UGameWorldModule
{
	GENERATED_BODY()

public:
	UDimensionalDismantleCrateGameWorldModule();
};
