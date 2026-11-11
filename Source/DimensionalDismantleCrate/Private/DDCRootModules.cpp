#include "DDCRootModules.h"

#include "Configuration/ModConfiguration.h"
#include "DDCConstants.h"
#include "DDCLog.h"
#include "DDCSubsystem.h"

UDimensionalDismantleCrateGameInstanceModule::UDimensionalDismantleCrateGameInstanceModule()
{
	bRootModule = true;
}

void UDimensionalDismantleCrateGameInstanceModule::DispatchLifecycleEvent(const ELifecyclePhase Phase)
{
	// Configurations are registered during INITIALIZATION, so add ours during CONSTRUCTION.
	// Loaded here instead of in the constructor so the mod's content is guaranteed to be mounted.
	if (Phase == ELifecyclePhase::CONSTRUCTION)
	{
		const TSoftClassPtr<UModConfiguration> ConfigClass{FSoftObjectPath(DDC::ConfigClassPath)};
		if (UClass* Loaded = ConfigClass.LoadSynchronous())
		{
			ModConfigurations.AddUnique(Loaded);
		}
		else
		{
			UE_LOG(LogDimensionalDismantleCrate, Error, TEXT("Could not load the Mod Configuration %s; built-in defaults will be used"), DDC::ConfigClassPath);
		}
	}
	Super::DispatchLifecycleEvent(Phase);
}

UDimensionalDismantleCrateGameWorldModule::UDimensionalDismantleCrateGameWorldModule()
{
	bRootModule = true;
	ModSubsystems.Add(ADimensionalDismantleCrateSubsystem::StaticClass());
}
