#include "DimensionalDismantleCrate.h"
#include "DDCLog.h"

#if DDC_DEV_COMMANDS
#include "DDCDevTools.h"
#endif

DEFINE_LOG_CATEGORY(LogDimensionalDismantleCrate);

void FDimensionalDismantleCrateModule::StartupModule()
{
	// All behaviour lives in the world subsystem, which SML spawns through the root game world module.
#if DDC_DEV_COMMANDS
	DDC::Dev::StartClientWatchIfRequested();
#endif
}

IMPLEMENT_MODULE(FDimensionalDismantleCrateModule, DimensionalDismantleCrate)
