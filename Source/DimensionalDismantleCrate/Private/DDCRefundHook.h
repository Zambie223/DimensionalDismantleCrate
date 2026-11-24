#pragma once

#include "CoreMinimal.h"

/**
 * The mod's hooks, all on the game's server-side dismantle:
 * - The "drop the dismantle refund on the ground" helpers get the part of a refund that didn't fit the player's
 *   inventory and put it into a dismantle crate. With the overflow option on, the subsystem sends what it can straight
 *   to the Dimensional Depot first, and only the rest reaches the crate.
 * - UFGBuildGunStateDismantle::Server_DismantleActors_Implementation hands each refund stack to the player's inventory
 *   (UFGInventoryComponent::AddStack). For players whose refunds go to the depot first, a scope set around the
 *   dismantle lets the AddStack hook send each stack to the depot before the inventory. Outside that scope (every other
 *   AddStack call in the game) the hook only reads one thread-local pointer.
 */
namespace DDC::RefundHook
{
	/** Installs the hooks (game builds only; the editor never dismantles). Called once at module startup. */
	void Install();

#if DDC_DEV_COMMANDS
	/** DEV: how often each helper has run in this process, so the test can tell which path the game used. */
	int32 GetCallsWithActor();
	int32 GetCallsWithoutActor();
	/** DEV: how many dismantles ran with refunds going to the depot first. */
	int32 GetDepotFirstDismantles();
	/** DEV: every server dismantle the hook saw, and why the last refused one didn't refund to the depot first. */
	int32 GetDismantleHookCalls();
	const FString& GetLastDepotFirstRefusal();

	/**
	 * DEV, for the hook-cost measurement only: takes the AddStack hook off or puts it back. SML removes the detour
	 * when the last handler goes, so "off" times the game's own AddStack. Call it on the game thread between frames,
	 * when no factory worker is inside AddStack. Returns false if the hook can't be changed (editor builds).
	 */
	bool SetAddStackHookForCostTest(bool bInstalled);
	bool IsAddStackHookInstalled();
	/** DEV: count every AddStack call (any thread) while on. */
	void SetCountAddStackCalls(bool bOn);
	int64 GetAddStackCalls();
#endif
}
