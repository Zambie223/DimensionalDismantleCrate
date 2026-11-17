#pragma once

#include "CoreMinimal.h"

/**
 * The mod's only hook: the game's "drop the dismantle refund on the ground" helpers, which get the part of a dismantle
 * refund that didn't fit the player's inventory and put it into a dismantle crate. With the overflow option on, the
 * subsystem sends what it can straight to the Dimensional Depot first, and only the rest reaches the crate.
 */
namespace DDC::RefundHook
{
	/** Installs the hooks (game builds only; the editor never dismantles). Called once at module startup. */
	void Install();

#if DDC_DEV_COMMANDS
	/** DEV: how often each helper has run in this process, so the test can tell which path the game used. */
	int32 GetCallsWithActor();
	int32 GetCallsWithoutActor();
#endif
}
