#include "DDCRefundHook.h"

#include "DDCLog.h"
#include "DDCSubsystem.h"
#include "Engine/World.h"
#include "FGCharacterPlayer.h"
#include "FGDismantleInterface.h"
#include "FGInventoryComponent.h"
#include "Patching/NativeHookManager.h"

namespace DDC::RefundHook
{
	namespace
	{
#if DDC_DEV_COMMANDS
		int32 GCallsWithActor = 0;
		int32 GCallsWithoutActor = 0;
#endif

#if !WITH_EDITOR
		/**
		 * Shared by both helpers. Returns true and fills OutRemaining when the refund was changed; the caller then hands
		 * only OutRemaining to the game, or nothing at all when it is empty.
		 */
		bool TakeOverflow(UWorld* World, const TArray<FInventoryStack>& Refund, TArray<FInventoryStack>& OutRemaining)
		{
			// The subsystem only exists on the server, which is the only place refunds are handed out.
			ADimensionalDismantleCrateSubsystem* Subsystem = World ? ADimensionalDismantleCrateSubsystem::Get(World) : nullptr;
			return Subsystem && Subsystem->SendOverflowToDepot(Refund, OutRemaining);
		}

		// Named handlers rather than lambdas inside SUBSCRIBE_METHOD: preprocessor lines (#if) are not allowed inside
		// a macro's arguments.
		using FWithActorScope = TCallScope<decltype(&FDismantleHelpers::DropRefundOnGround)>;
		using FWithoutActorScope = TCallScope<decltype(&FDismantleHelpers::DropRefundOnGroundNoActor)>;

		void OnDropWithActor(FWithActorScope& Scope, AActor* DismantledActor, const FVector& AimHitLocation, const TArray<FInventoryStack>& Refund, AFGCharacterPlayer* Initiator)
		{
#if DDC_DEV_COMMANDS
			++GCallsWithActor;
#endif
			UWorld* World = DismantledActor ? DismantledActor->GetWorld() : (Initiator ? Initiator->GetWorld() : nullptr);
			TArray<FInventoryStack> Remaining;
			if (!TakeOverflow(World, Refund, Remaining))
			{
				return; // untouched: the game drops the whole refund as usual
			}
			if (Remaining.Num() == 0)
			{
				Scope.Cancel(); // everything went to the depot: no crate
				return;
			}
			Scope(DismantledActor, AimHitLocation, Remaining, Initiator);
		}

		void OnDropWithoutActor(FWithoutActorScope& Scope, UWorld* World, const FVector& LightweightLocation, AActor* IgnoreActor, const TArray<FInventoryStack>& Refund, AFGCharacterPlayer* Initiator)
		{
#if DDC_DEV_COMMANDS
			++GCallsWithoutActor;
#endif
			TArray<FInventoryStack> Remaining;
			if (!TakeOverflow(World ? World : (Initiator ? Initiator->GetWorld() : nullptr), Refund, Remaining))
			{
				return;
			}
			if (Remaining.Num() == 0)
			{
				Scope.Cancel();
				return;
			}
			Scope(World, LightweightLocation, IgnoreActor, Remaining, Initiator);
		}
#endif
	}

	void Install()
	{
#if !WITH_EDITOR
		SUBSCRIBE_METHOD(FDismantleHelpers::DropRefundOnGround, &OnDropWithActor);
		SUBSCRIBE_METHOD(FDismantleHelpers::DropRefundOnGroundNoActor, &OnDropWithoutActor);
		UE_LOG(LogDimensionalDismantleCrate, Log, TEXT("Installed the dismantle refund hooks"));
#endif
	}

#if DDC_DEV_COMMANDS
	int32 GetCallsWithActor() { return GCallsWithActor; }
	int32 GetCallsWithoutActor() { return GCallsWithoutActor; }
#endif
}
