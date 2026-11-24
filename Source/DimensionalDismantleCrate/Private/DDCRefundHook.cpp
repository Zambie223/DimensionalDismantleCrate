#include "DDCRefundHook.h"

#include "DDCLog.h"
#include "DDCSubsystem.h"
#include "Engine/World.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "FGCharacterPlayer.h"
#include "FGDismantleInterface.h"
#include "FGInventoryComponent.h"
#include "FGPlayerState.h"
#include "GameFramework/Pawn.h"
#include "Patching/NativeHookManager.h"

#include <atomic>

namespace DDC::RefundHook
{
	namespace
	{
#if DDC_DEV_COMMANDS
		int32 GCallsWithActor = 0;
		int32 GCallsWithoutActor = 0;
		int32 GDepotFirstDismantles = 0;
		/** Every server dismantle the hook saw, and why the last one didn't refund to the depot first. */
		int32 GDismantleHookCalls = 0;
		FString GLastDepotFirstRefusal;
#endif

// Directives can't appear inside SUBSCRIBE_METHOD's macro arguments, so the dev counter is a macro that vanishes here.
#if DDC_DEV_COMMANDS
#define DDC_DEV_COUNT_DEPOT_FIRST() (++GDepotFirstDismantles)
#else
#define DDC_DEV_COUNT_DEPOT_FIRST() ((void)0)
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

		/**
		 * Depot-first refunds. While the game's server dismantle runs for a player whose refunds go to the depot first,
		 * this names the inventory the refund goes to. The game adds each refund stack with AddStack(stack, true) on
		 * that inventory (game 1.2.5.1, Server_DismantleActors_Implementation); the AddStack hook catches exactly those.
		 * Thread-local, because AddStack also runs on factory worker threads, which never see a scope.
		 */
		struct FDepotFirstScope
		{
			UFGInventoryComponent* Inventory = nullptr;
			ADimensionalDismantleCrateSubsystem* Subsystem = nullptr;
		};
		thread_local FDepotFirstScope* GDepotFirst = nullptr;

		/** Returns false; in dev builds also records and logs why this dismantle doesn't refund to the depot first. */
		bool Refuse(const TCHAR* Why)
		{
#if DDC_DEV_COMMANDS
			GLastDepotFirstRefusal = Why;
			UE_LOG(LogDimensionalDismantleCrate, Log, TEXT("Depot-first refunds not used for this dismantle: %s"), Why);
#endif
			return false;
		}

		/** Sets up the scope if this dismantle's refunds go to the depot first. Returns false to leave the dismantle alone. */
		bool BeginDepotFirst(UFGBuildGunStateDismantle* State, FDepotFirstScope& Out)
		{
#if DDC_DEV_COMMANDS
			++GDismantleHookCalls;
#endif
			if (GDepotFirst)
			{
				return Refuse(TEXT("nested dismantle (the outer one already decided)"));
			}
			UWorld* World = State ? State->GetWorld() : nullptr;
			if (!World)
			{
				return Refuse(TEXT("no world"));
			}
			ADimensionalDismantleCrateSubsystem* Subsystem = ADimensionalDismantleCrateSubsystem::Get(World);
			if (!Subsystem)
			{
				return Refuse(TEXT("no subsystem (not the server)"));
			}
			// The game refunds into AFGBuildGun::GetInventory(), which reads the build gun's Owner. The player's setting is
			// read from that same pawn. UFGBuildGunState::GetPlayerState() goes through the Instigator instead, and the
			// Instigator can be unset here (game 1.2.5.1), so it is only the fallback.
			const AFGBuildGun* BuildGun = State->GetBuildGun();
			UFGInventoryComponent* Inventory = BuildGun ? BuildGun->GetInventory() : nullptr;
			if (!Inventory)
			{
				return Refuse(TEXT("no build gun or inventory"));
			}
			const APawn* OwnerPawn = Cast<APawn>(BuildGun->GetOwner());
			const AFGPlayerState* PlayerState = OwnerPawn ? OwnerPawn->GetPlayerState<AFGPlayerState>() : nullptr;
			if (!PlayerState)
			{
				PlayerState = State->GetPlayerState();
			}
			if (const TCHAR* Why = Subsystem->WhyNotRefundToDepotFirst(PlayerState))
			{
				return Refuse(Why);
			}
			Out.Inventory = Inventory;
			Out.Subsystem = Subsystem;
			return true;
		}

#if DDC_DEV_COMMANDS
		/** DEV: counts every AddStack call while on, for the hook-cost estimate. Off during the timing loops. */
		std::atomic<bool> GCountAddStack{false};
		std::atomic<int64> GAddStackCalls{0};
#define DDC_DEV_COUNT_ADD_STACK() do { if (GCountAddStack.load(std::memory_order_relaxed)) { GAddStackCalls.fetch_add(1, std::memory_order_relaxed); } } while (0)
#else
#define DDC_DEV_COUNT_ADD_STACK() ((void)0)
#endif

		/** The AddStack handler. Named so dev builds can take the hook off and put it back for the cost measurement. */
		const auto OnAddStack = [](auto& Scope, UFGInventoryComponent* Self, const FInventoryStack& Stack, bool bAllowPartialAdd)
		{
			DDC_DEV_COUNT_ADD_STACK();
			const FDepotFirstScope* DepotFirst = GDepotFirst;
			// Everything else in the game: one thread-local read. The dismantle adds refunds with partial adds allowed;
			// an all-or-nothing add is left alone, since a split would break its promise.
			if (!DepotFirst || Self != DepotFirst->Inventory || !bAllowPartialAdd || !Stack.HasItems())
			{
				return;
			}
			FInventoryStack Left;
			const int32 Sent = DepotFirst->Subsystem->SendRefundStackToDepot(Stack, Left);
			if (Sent <= 0)
			{
				return; // the depot took nothing: the inventory gets the stack as usual
			}
			// The rest goes to the inventory as usual; whatever doesn't fit reaches the crate helpers above.
			const int32 Added = Left.HasItems() ? Scope(Self, Left, bAllowPartialAdd) : 0;
			Scope.Override(Sent + Added);
		};

		FDelegateHandle GAddStackHandle;

		void SubscribeAddStack()
		{
			GAddStackHandle = SUBSCRIBE_METHOD_VIRTUAL(UFGInventoryComponent::AddStack, GetMutableDefault<UFGInventoryComponent>(), OnAddStack);
		}
#endif
	}

	void Install()
	{
#if !WITH_EDITOR
		SUBSCRIBE_METHOD(FDismantleHelpers::DropRefundOnGround, &OnDropWithActor);
		SUBSCRIBE_METHOD(FDismantleHelpers::DropRefundOnGroundNoActor, &OnDropWithoutActor);

		// Virtual hooks: SML resolves the implementation through the sample object's vtable and detours it, so every
		// call reaches the handler, whichever way it is made.
		SUBSCRIBE_METHOD_VIRTUAL(UFGBuildGunStateDismantle::Server_DismantleActors_Implementation, GetMutableDefault<UFGBuildGunStateDismantle>(),
			[](auto& Scope, UFGBuildGunStateDismantle* Self, const TArray<AActor*>& SelectedActors, const TArray<FDismantleLightweightBundle>& LightweightBundles)
			{
				FDepotFirstScope DepotFirst;
				if (!BeginDepotFirst(Self, DepotFirst))
				{
					return; // the game dismantles as usual
				}
				DDC_DEV_COUNT_DEPOT_FIRST();
				GDepotFirst = &DepotFirst;
				Scope(Self, SelectedActors, LightweightBundles);
				GDepotFirst = nullptr;
			});
		SubscribeAddStack();
		UE_LOG(LogDimensionalDismantleCrate, Log, TEXT("Installed the dismantle refund hooks"));
#endif
	}

#if DDC_DEV_COMMANDS
	int32 GetCallsWithActor() { return GCallsWithActor; }
	int32 GetCallsWithoutActor() { return GCallsWithoutActor; }
	int32 GetDepotFirstDismantles() { return GDepotFirstDismantles; }
	int32 GetDismantleHookCalls() { return GDismantleHookCalls; }
	const FString& GetLastDepotFirstRefusal() { return GLastDepotFirstRefusal; }

	bool SetAddStackHookForCostTest(const bool bInstalled)
	{
#if !WITH_EDITOR
		if (bInstalled == GAddStackHandle.IsValid())
		{
			return true;
		}
		if (bInstalled)
		{
			SubscribeAddStack();
			return GAddStackHandle.IsValid();
		}
		// SML removes the detour itself once a hook has no handlers left (HookInvoker::RemoveHandler).
		FDelegateHandle Handle = GAddStackHandle;
		UNSUBSCRIBE_METHOD(UFGInventoryComponent::AddStack, Handle);
		GAddStackHandle.Reset();
		return true;
#else
		return false;
#endif
	}

	bool IsAddStackHookInstalled()
	{
#if !WITH_EDITOR
		return GAddStackHandle.IsValid();
#else
		return false;
#endif
	}

	void SetCountAddStackCalls(const bool bOn)
	{
#if !WITH_EDITOR
		GCountAddStack.store(bOn, std::memory_order_relaxed);
#endif
	}

	int64 GetAddStackCalls()
	{
#if !WITH_EDITOR
		return GAddStackCalls.load(std::memory_order_relaxed);
#else
		return 0;
#endif
	}
#endif
}
