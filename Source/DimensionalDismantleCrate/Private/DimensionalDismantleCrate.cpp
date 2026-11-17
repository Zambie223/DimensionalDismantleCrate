#include "DimensionalDismantleCrate.h"
#include "DDCLog.h"
#include "DDCRefundHook.h"

#if DDC_DEV_COMMANDS
#include "DDCDevTools.h"
#endif

DEFINE_LOG_CATEGORY(LogDimensionalDismantleCrate);

void FDimensionalDismantleCrateModule::StartupModule()
{
	// Behaviour lives in the world subsystem, which SML spawns through the root game world module. The refund hook
	// only forwards dismantle overflow to that subsystem (and does nothing where it doesn't exist, e.g. on clients).
	DDC::RefundHook::Install();
#if DDC_DEV_COMMANDS
	DDC::Dev::StartClientWatchIfRequested();
#endif
}

IMPLEMENT_MODULE(FDimensionalDismantleCrateModule, DimensionalDismantleCrate)
