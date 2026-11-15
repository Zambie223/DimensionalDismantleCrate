// DEV ONLY (DDC_DEV_COMMANDS): an automatic in-game test run, started by launching the game with -DDCAutoTest
// (or with the console command DDC.Dev.RunTests). It spawns its own crates, changes this mod's settings, measures
// upload rates and logs one "[DDCTest] PASS/FAIL/SKIP ..." line per check, then "[DDCTest] DONE pass=.. fail=..".
// Settings are reset to the defaults at the end. Use it on a test save only: it uploads items into that save's depot.

#if DDC_DEV_COMMANDS

#include "CoreMinimal.h"
#include "DDCConstants.h"
#include "DDCDevTools.h"
#include "DDCLog.h"
#include "DDCSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FGCentralStorageSubsystem.h"
#include "FGInventoryComponent.h"
#include "FGCrate.h"
#include "HAL/IConsoleManager.h"
#include "ItemAmount.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/Parse.h"
#include "Resources/FGEquipmentDescriptor.h"
#include "Resources/FGItemDescriptor.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

namespace
{
	using namespace DDC::Dev;

	class FAutoTest : public TSharedFromThis<FAutoTest>
	{
	public:
		explicit FAutoTest(UWorld* InWorld) : World(InWorld) {}

		void Start(const float InitialDelay)
		{
			Log(FString::Printf(TEXT("START in %.0f s"), InitialDelay));
			BuildSteps();
			Schedule(InitialDelay);
		}

	private:
		TWeakObjectPtr<UWorld> World;
		TArray<TFunction<float()>> Steps; // each returns the delay before the next step
		int32 NextStep = 0;
		int32 Passed = 0;
		int32 Failed = 0;
		int32 Skipped = 0;
		FTimerHandle Timer;

		float T = 0.0f;
		float Window = 0.0f;
		TSubclassOf<UFGItemDescriptor> ItemA;   // depot has plenty of room
		TSubclassOf<UFGItemDescriptor> ItemFull; // depot at its limit (may be null)
		TSubclassOf<UFGItemDescriptor> Equipment; // any equipment descriptor (may be null)
		TArray<TWeakObjectPtr<AFGCrate>> CratesUnderTest;
		/** Crates that were already in the save. They upload too (and share Shared mode's turns), so totals count them; never destroyed. */
		TArray<TWeakObjectPtr<AFGCrate>> ExistingCrates;
		TMap<const AFGCrate*, int32> Snapshot;
		int32 DepotSnapshot = 0;
		int32 SlotsAtSnapshot = 0;
		TWeakObjectPtr<AFGCrate> CrateA, CrateB, CrateMulti, CrateFullFirst, CrateIdle, CrateDeath, CrateSmall;
		/** Items taken out of the depot by the idle-resume check. */
		int32 FreedRoom = 0;

		static void Log(const FString& Line) { UE_LOG(LogDimensionalDismantleCrate, Display, TEXT("[DDCTest] %s"), *Line); }

		void Check(const bool bOk, const FString& Name, const FString& Detail)
		{
			(bOk ? Passed : Failed)++;
			Log(FString::Printf(TEXT("%s %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *Name, *Detail));
		}

		void Skip(const FString& Name, const FString& Why)
		{
			++Skipped;
			Log(FString::Printf(TEXT("SKIP %s: %s"), *Name, *Why));
		}

		void Schedule(const float Delay)
		{
			UWorld* W = World.Get();
			if (!W)
			{
				return;
			}
			// The lambda holds a strong reference, which keeps the runner alive until the last step.
			TSharedRef<FAutoTest> Self = AsShared();
			W->GetTimerManager().SetTimer(Timer, FTimerDelegate::CreateLambda([Self]
			{
				Self->RunNext();
			}), FMath::Max(Delay, 0.01f), false);
		}

		void RunNext()
		{
			if (!World.IsValid() || NextStep >= Steps.Num())
			{
				return;
			}
			const float Delay = Steps[NextStep++]();
			if (NextStep < Steps.Num())
			{
				Schedule(Delay);
			}
		}

		AFGCentralStorageSubsystem* Depot() const { return AFGCentralStorageSubsystem::Get(World.Get()); }

		void Set(const TCHAR* Key, const TCHAR* Value)
		{
			FString Message;
			const bool bOk = ADimensionalDismantleCrateSubsystem::SetConfigValue(World.Get(), Key, Value, Message);
			Log(FString::Printf(TEXT("set %s=%s -> %s %s"), Key, Value, bOk ? TEXT("ok") : TEXT("REFUSED"), *Message));
		}

		void TakeSnapshot(TSubclassOf<UFGItemDescriptor> Item)
		{
			Snapshot.Reset();
			SlotsAtSnapshot = 0;
			for (const TArray<TWeakObjectPtr<AFGCrate>>* List : {&CratesUnderTest, &ExistingCrates})
			{
				for (const TWeakObjectPtr<AFGCrate>& Crate : *List)
				{
					Snapshot.Add(Crate.Get(), CountItems(Crate.Get()));
					SlotsAtSnapshot += UploadableSlots(Crate.Get());
				}
			}
			DepotSnapshot = Item ? Depot()->GetNumItemsFromCentralStorage(Item) : 0;
		}

		int32 UploadedSince(const AFGCrate* Crate) const
		{
			const int32* Before = Snapshot.Find(Crate);
			return Before ? *Before - CountItems(Crate) : 0;
		}

		/** Items uploaded since the snapshot by the test's crates and, with bIncludeExisting, the save's own crates. */
		int32 TotalUploadedSince(const bool bIncludeExisting = true) const
		{
			int32 Total = 0;
			for (const TWeakObjectPtr<AFGCrate>& Crate : CratesUnderTest)
			{
				Total += UploadedSince(Crate.Get());
			}
			if (bIncludeExisting)
			{
				for (const TWeakObjectPtr<AFGCrate>& Crate : ExistingCrates)
				{
					Total += UploadedSince(Crate.Get());
				}
			}
			return Total;
		}

		/** Filled slots of an eligible crate that the depot would accept now (each is one source in Per stack slot mode). */
		int32 UploadableSlots(const AFGCrate* Crate) const
		{
			const AFGCentralStorageSubsystem* CS = Depot();
			const UFGInventoryComponent* Inventory = IsValid(Crate) && !Crate->IsActorBeingDestroyed() ? Crate->GetInventory() : nullptr;
			if (!Inventory || !CS || !IsEligibleNow(Crate))
			{
				return 0;
			}
			int32 Slots = 0;
			for (int32 Slot = 0; Slot < Inventory->GetSizeLinear(); ++Slot)
			{
				FInventoryStack Stack;
				Slots += (Inventory->GetStackFromIndex(Slot, Stack) && Stack.HasItems() && CS->CanUploadInventoryItemToCentralStorage(Stack.Item)) ? 1 : 0;
			}
			return Slots;
		}

		/** Mirrors the mod's eligibility for the settings the test uses at the time (dismantle crates; death crates only in step 5). */
		bool IsEligibleNow(const AFGCrate* Crate) const
		{
			return Crate && (Crate->GetCrateType() == EFGCrateType::CT_DismantleCrate || (Crate->GetCrateType() == EFGCrateType::CT_DeathCrate && bDeathCratesOn));
		}
		bool bDeathCratesOn = false;

		/** Rate check: Got items in Window seconds at Sources parallel uploaders; one item of slack each way per source. */
		void CheckRate(const FString& Name, const int32 Got, const int32 Sources)
		{
			const float Expected = Sources * Window / T;
			const bool bOk = Got >= FMath::FloorToInt(Expected) - Sources && Got <= FMath::CeilToInt(Expected) + Sources;
			Check(bOk, Name, FString::Printf(TEXT("uploaded %d in %.2f s, expected %.1f (%d source(s) x %.2f/s)"), Got, Window, Expected, Sources, 1.0f / T));
		}

		void Track(AFGCrate* Crate, TWeakObjectPtr<AFGCrate>& Slot)
		{
			Slot = Crate;
			CratesUnderTest.Add(Crate);
		}

		void ClearCrates()
		{
			for (const TWeakObjectPtr<AFGCrate>& Crate : CratesUnderTest)
			{
				if (AFGCrate* Live = Crate.Get())
				{
					Live->Destroy();
				}
			}
			CratesUnderTest.Reset();
		}

		void BuildSteps()
		{
			// 0. Preconditions and test items.
			Steps.Add([this]
			{
				ADimensionalDismantleCrateSubsystem* Subsystem = ADimensionalDismantleCrateSubsystem::Get(World.Get());
				AFGCentralStorageSubsystem* CS = Depot();
				if (!Subsystem || !CS)
				{
					Check(false, TEXT("setup"), TEXT("subsystem or central storage missing"));
					NextStep = Steps.Num() - 1; // jump to the summary
					return 0.1f;
				}
				FOutputDeviceNull Discard;
				Subsystem->DescribeStatus(Discard);
				if (!Subsystem->IsUploadingAllowed())
				{
					Check(false, TEXT("setup"), TEXT("uploading is not allowed in this save (needs the depot research and an uploader); nothing else can be tested"));
					NextStep = Steps.Num() - 1;
					return 0.1f;
				}

				for (TActorIterator<AFGCrate> It(World.Get()); It; ++It)
				{
					ExistingCrates.Add(*It);
				}
				Log(FString::Printf(TEXT("%d crate(s) already in the save; they are counted in totals and never destroyed"), ExistingCrates.Num()));

				T = CS->GetCentralStorageTimeToUpload();
				Window = FMath::Clamp(16.0f * T, 4.0f, 32.0f);
				Log(FString::Printf(TEXT("time to upload %.4f s, measuring windows of %.2f s"), T, Window));

				// Item A: a common part with lots of room in the depot.
				for (const TCHAR* Name : {TEXT("IronPlate"), TEXT("IronRod"), TEXT("Wire"), TEXT("Cable"), TEXT("Cement"), TEXT("IronScrew"), TEXT("CopperSheet")})
				{
					const TSubclassOf<UFGItemDescriptor> Candidate = FindItemClass(Name);
					if (Candidate && CS->GetCentralStorageItemLimit(Candidate) - CS->GetNumItemsFromCentralStorage(Candidate) >= 400)
					{
						ItemA = Candidate;
						break;
					}
				}
				// An item the depot holds at its limit.
				TArray<FItemAmount> Stored;
				CS->GetAllItemsFromCentralStorage(Stored);
				for (const FItemAmount& Amount : Stored)
				{
					if (Amount.ItemClass && Amount.ItemClass != ItemA && Amount.Amount >= CS->GetCentralStorageItemLimit(Amount.ItemClass)
						&& !Amount.ItemClass->IsChildOf(UFGEquipmentDescriptor::StaticClass()))
					{
						ItemFull = Amount.ItemClass;
						break;
					}
				}
				// Any loaded concrete equipment descriptor; a jetpack if one is loaded. The game's own upload rules
				// may refuse some equipment, which this test doesn't depend on (it checks that the mod keeps it back).
				for (TObjectIterator<UClass> It; It; ++It)
				{
					if (It->IsChildOf(UFGEquipmentDescriptor::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)
						&& !It->GetName().StartsWith(TEXT("SKEL_")) && !It->GetName().StartsWith(TEXT("REINST_")))
					{
						if (!Equipment || It->GetName().Contains(TEXT("JetPack")))
						{
							Equipment = *It;
						}
					}
				}
				Log(FString::Printf(TEXT("item A=%s (depot %d/%d), full item=%s, equipment=%s"),
					ItemA ? *ItemA->GetName() : TEXT("none"),
					ItemA ? CS->GetNumItemsFromCentralStorage(ItemA) : 0, ItemA ? CS->GetCentralStorageItemLimit(ItemA) : 0,
					ItemFull ? *ItemFull->GetName() : TEXT("none"), Equipment ? *Equipment->GetName() : TEXT("none")));
				if (!ItemA || !(T > 0.0f))
				{
					Check(false, TEXT("setup"), TEXT("no test item with room in the depot, or invalid upload time"));
					NextStep = Steps.Num() - 1;
					return 0.1f;
				}
				Set(DDC::KeyEnabled, TEXT("true"));
				Set(DDC::KeyRateMode, TEXT("PerCrate"));
				Set(DDC::KeyIncludeDeathCrates, TEXT("false"));
				Set(DDC::KeyUploadDeathCrateEquipment, TEXT("false"));
				return 1.0f;
			});

			// 1. Per crate: two crates, each at one uploader's rate.
			Steps.Add([this]
			{
				Track(SpawnCrate(World.Get(), {{ItemA, 200}}, EFGCrateType::CT_DismantleCrate, FVector(300, 0, 100)), CrateA);
				Track(SpawnCrate(World.Get(), {{ItemA, 200}}, EFGCrateType::CT_DismantleCrate, FVector(300, 300, 100)), CrateB);
				Check(CrateA.IsValid() && CrateB.IsValid(), TEXT("spawn"), TEXT("two dismantle crates of 200"));
				return 1.0f;
			});
			Steps.Add([this] { TakeSnapshot(ItemA); return Window; });
			Steps.Add([this]
			{
				CheckRate(TEXT("per crate, crate A"), UploadedSince(CrateA.Get()), 1);
				CheckRate(TEXT("per crate, crate B"), UploadedSince(CrateB.Get()), 1);
				// Only the test crates: they hold item A alone, while the save's own crates upload other items.
				const int32 DepotGain = Depot()->GetNumItemsFromCentralStorage(ItemA) - DepotSnapshot;
				const int32 TestCratesLost = TotalUploadedSince(/*bIncludeExisting=*/false);
				Check(DepotGain == TestCratesLost, TEXT("depot count matches"), FString::Printf(TEXT("depot +%d item A, test crates -%d"), DepotGain, TestCratesLost));
				Set(DDC::KeyRateMode, TEXT("Shared"));
				return 1.0f;
			});

			// 2. Shared: the same two crates together at one uploader's rate.
			Steps.Add([this] { TakeSnapshot(ItemA); return Window; });
			Steps.Add([this]
			{
				CheckRate(TEXT("shared, all crates together"), TotalUploadedSince(), 1);
				const int32 A = UploadedSince(CrateA.Get());
				const int32 B = UploadedSince(CrateB.Get());
				Check(FMath::Abs(A - B) <= 1, TEXT("shared, round robin"), FString::Printf(TEXT("crate A %d, crate B %d"), A, B));
				Set(DDC::KeyRateMode, TEXT("PerSlot"));
				return 0.5f;
			});

			// 3. Per stack slot: a crate with several full stacks uploads once per filled slot.
			Steps.Add([this]
			{
				const int32 StackSize = FMath::Max(1, UFGItemDescriptor::GetStackSize(ItemA));
				Track(SpawnCrate(World.Get(), {{ItemA, 3 * StackSize}}, EFGCrateType::CT_DismantleCrate, FVector(600, 0, 100)), CrateMulti);
				Check(CountFilledSlots(CrateMulti.Get()) == 3, TEXT("spawn"), FString::Printf(TEXT("multi-stack crate has %d filled slot(s)"), CountFilledSlots(CrateMulti.Get())));
				return 1.0f;
			});
			Steps.Add([this] { TakeSnapshot(ItemA); return Window; });
			Steps.Add([this]
			{
				CheckRate(TEXT("per slot, multi-stack crate"), UploadedSince(CrateMulti.Get()), 3);
				CheckRate(TEXT("per slot, all crates"), TotalUploadedSince(), SlotsAtSnapshot);
				Set(DDC::KeyRateMode, TEXT("PerCrate"));
				return 0.5f;
			});

			// 4. A full depot item stays in the crate while the next slot keeps uploading.
			Steps.Add([this]
			{
				if (!ItemFull)
				{
					Skip(TEXT("full item stays"), TEXT("no item in this depot is at its limit"));
					return 0.1f;
				}
				Track(SpawnCrate(World.Get(), {{ItemFull, 10}, {ItemA, 50}}, EFGCrateType::CT_DismantleCrate, FVector(600, 300, 100)), CrateFullFirst);
				return 1.0f;
			});
			Steps.Add([this]
			{
				if (!ItemFull)
				{
					return 0.1f;
				}
				TakeSnapshot(ItemA);
				return Window;
			});
			Steps.Add([this]
			{
				if (!ItemFull)
				{
					return 0.1f;
				}
				const AFGCrate* Crate = CrateFullFirst.Get();
				Check(CountItems(Crate, ItemFull) == 10, TEXT("full item stays"), FString::Printf(TEXT("%d of 10 left"), CountItems(Crate, ItemFull)));
				CheckRate(TEXT("next slot keeps uploading"), UploadedSince(Crate), 1);
				return 0.1f;
			});

			// 4b. An idle crate (nothing the depot can take) starts again once the depot has room. Idle crates only
			//     re-check every FUploadTimer::IdleRecheckSeconds, so this covers that path. The other test crate holding
			//     the full item is removed first so it can't take the room.
			Steps.Add([this]
			{
				if (!ItemFull)
				{
					Skip(TEXT("idle crate resumes"), TEXT("no item in this depot is at its limit"));
					return 0.1f;
				}
				if (AFGCrate* Live = CrateFullFirst.Get())
				{
					Live->Destroy();
				}
				Track(SpawnCrate(World.Get(), {{ItemFull, 5}}, EFGCrateType::CT_DismantleCrate, FVector(600, 600, 100)), CrateIdle);
				return 1.0f;
			});
			Steps.Add([this]
			{
				if (!ItemFull)
				{
					return 0.1f;
				}
				Check(CountItems(CrateIdle.Get(), ItemFull) == 5, TEXT("idle crate waits while the depot is full"),
					FString::Printf(TEXT("%d of 5 left"), CountItems(CrateIdle.Get(), ItemFull)));
				FreedRoom = Depot()->TryRemoveItemsFromCentralStorage(ItemFull, 3);
				return 3.0f * T + DDC::FUploadTimer::IdleRecheckSeconds + 1.0f;
			});
			Steps.Add([this]
			{
				if (!ItemFull)
				{
					return 0.1f;
				}
				const AFGCentralStorageSubsystem* CS = Depot();
				const int32 Left = CountItems(CrateIdle.Get(), ItemFull);
				const bool bRefilled = CS->GetNumItemsFromCentralStorage(ItemFull) >= CS->GetCentralStorageItemLimit(ItemFull);
				// The save's own crates may hold the same item and share the freed room, so the test crate only has to take part.
				Check(FreedRoom > 0 && bRefilled && Left < 5, TEXT("idle crate resumes"),
					FString::Printf(TEXT("freed %d, depot %s, test crate %d of 5 left"), FreedRoom, bRefilled ? TEXT("full again") : TEXT("NOT full"), Left));
				return 0.1f;
			});

			// 5. Death crates: ignored by default; with the option, items upload but equipment stays.
			Steps.Add([this]
			{
				TArray<FItemCount> Items{{ItemA, 50}};
				if (Equipment)
				{
					Items.Insert(FItemCount{Equipment, 1}, 0);
				}
				Track(SpawnCrate(World.Get(), Items, EFGCrateType::CT_DeathCrate, FVector(900, 0, 100)), CrateDeath);
				TakeSnapshot(ItemA);
				return Window;
			});
			Steps.Add([this]
			{
				Check(UploadedSince(CrateDeath.Get()) == 0, TEXT("death crate ignored by default"), FString::Printf(TEXT("%d uploaded"), UploadedSince(CrateDeath.Get())));
				Set(DDC::KeyIncludeDeathCrates, TEXT("true"));
				bDeathCratesOn = true;
				return 1.0f;
			});
			Steps.Add([this] { TakeSnapshot(ItemA); return Window; });
			Steps.Add([this]
			{
				CheckRate(TEXT("death crate uploads with the option"), UploadedSince(CrateDeath.Get()), 1);
				if (Equipment)
				{
					Check(CountItems(CrateDeath.Get(), Equipment) == 1, TEXT("death crate keeps equipment"), FString::Printf(TEXT("%d equipment left"), CountItems(CrateDeath.Get(), Equipment)));
				}
				else
				{
					Skip(TEXT("death crate keeps equipment"), TEXT("no equipment descriptor loaded"));
				}
				Set(DDC::KeyUploadDeathCrateEquipment, TEXT("true"));
				return Window;
			});
			Steps.Add([this]
			{
				if (Equipment)
				{
					// Informational: the game's own rules decide whether this equipment can be uploaded at all.
					Log(FString::Printf(TEXT("INFO equipment option on: %d equipment left in the death crate"), CountItems(CrateDeath.Get(), Equipment)));
				}
				Set(DDC::KeyIncludeDeathCrates, TEXT("false"));
				bDeathCratesOn = false;
				Set(DDC::KeyUploadDeathCrateEquipment, TEXT("false"));
				return 0.5f;
			});

			// 6. An emptied crate disappears.
			Steps.Add([this]
			{
				Track(SpawnCrate(World.Get(), {{ItemA, 3}}, EFGCrateType::CT_DismantleCrate, FVector(900, 300, 100)), CrateSmall);
				return 3.0f * T + 1.5f;
			});
			Steps.Add([this]
			{
				Check(!CrateSmall.IsValid() || CrateSmall->IsActorBeingDestroyed(), TEXT("empty crate removed"), CrateSmall.IsValid()
					? FString::Printf(TEXT("still there with %d item(s)"), CountItems(CrateSmall.Get())) : FString(TEXT("gone")));
				Set(DDC::KeyEnabled, TEXT("false"));
				return 1.0f;
			});

			// 7. Disabled: nothing uploads.
			Steps.Add([this] { TakeSnapshot(ItemA); return Window; });
			Steps.Add([this]
			{
				Check(TotalUploadedSince() == 0, TEXT("disabled uploads nothing"), FString::Printf(TEXT("%d uploaded"), TotalUploadedSince()));
				Set(DDC::KeyEnabled, TEXT("true"));
				return 0.5f;
			});

			// 8. Performance: 200 crates that can't upload (depot full for their item) are the worst steady case, since
			//    every idle crate re-checks its slots (at most every FUploadTimer::IdleRecheckSeconds). Budget: 1 ms average per tick.
			Steps.Add([this]
			{
				if (!ItemFull)
				{
					Skip(TEXT("200 idle crates"), TEXT("no item in this depot is at its limit"));
					return 0.1f;
				}
				for (int32 Index = 0; Index < 200; ++Index)
				{
					// Two slots each, so the per-tick check scans more than one slot per crate.
					AFGCrate* Crate = SpawnCrate(World.Get(), {{ItemFull, 2 * FMath::Max(1, UFGItemDescriptor::GetStackSize(ItemFull))}},
						EFGCrateType::CT_DismantleCrate, FVector(1200 + (Index % 20) * 150, (Index / 20) * 150, 100));
					CratesUnderTest.Add(Crate);
				}
				return 2.0f;
			});
			Steps.Add([this]
			{
				if (ADimensionalDismantleCrateSubsystem* Subsystem = ADimensionalDismantleCrateSubsystem::Get(World.Get()))
				{
					Subsystem->ResetTickStats();
				}
				return 5.0f;
			});
			Steps.Add([this]
			{
				if (!ItemFull)
				{
					return 0.1f;
				}
				if (ADimensionalDismantleCrateSubsystem* Subsystem = ADimensionalDismantleCrateSubsystem::Get(World.Get()))
				{
					const double Avg = Subsystem->GetAverageTickMicroseconds();
					Check(Avg < 1000.0, TEXT("200 idle crates tick cost"), FString::Printf(TEXT("%d tracked crates, avg %.1f us, max %.1f us over %lld ticks"),
						Subsystem->GetTrackedCrateCount(), Avg, Subsystem->GetMaxTickMicroseconds(), Subsystem->GetTickCount()));
				}
				return 0.1f;
			});

			// Summary (always the last step).
			Steps.Add([this]
			{
				Set(DDC::KeyRateMode, TEXT("PerCrate"));
				ClearCrates();
				if (ADimensionalDismantleCrateSubsystem* Subsystem = ADimensionalDismantleCrateSubsystem::Get(World.Get()))
				{
					FOutputDeviceNull Discard;
					Subsystem->DescribeStatus(Discard);
				}
				Log(FString::Printf(TEXT("DONE pass=%d fail=%d skip=%d"), Passed, Failed, Skipped));
				return 0.0f;
			});
		}
	};

	void StartAutoTest(UWorld* World, const float Delay)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			return;
		}
		MakeShared<FAutoTest>(World)->Start(Delay);
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GRunTestsCommand(
		TEXT("DDC.Dev.RunTests"),
		TEXT("DEV: runs the automatic test sequence on this test save (uploads items into its depot). Results go to the log as [DDCTest] lines."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			StartAutoTest(World, 1.0f);
			Ar.Log(TEXT("Started; see the log for [DDCTest] lines."));
		}));

}

namespace DDC::Dev
{
	/**
	 * DEV: with -DDCClientWatch, every client world logs the crates it sees every 5 s ("[DDCClient] ..."), so a
	 * multiplayer test can compare the client's replicated view with the server. Clients don't run this mod's
	 * subsystem, so this runs from a core ticker started at module load.
	 */
	void StartClientWatchIfRequested()
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("DDCClientWatch")))
		{
			return;
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			if (!GEngine)
			{
				return true;
			}
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (!World || World->GetNetMode() != NM_Client)
				{
					continue;
				}
				int32 Crates = 0;
				int32 Items = 0;
				FString Details;
				for (TActorIterator<AFGCrate> It(World); It; ++It)
				{
					++Crates;
					const int32 Count = CountItems(*It);
					Items += Count;
					Details += FString::Printf(TEXT(" %s=%d"), *It->GetName(), Count);
				}
				UE_LOG(LogDimensionalDismantleCrate, Display, TEXT("[DDCClient] %d crate(s), %d item(s):%s"), Crates, Items, *Details.Left(1500));
			}
			return true;
		}), 5.0f);
		UE_LOG(LogDimensionalDismantleCrate, Display, TEXT("[DDCClient] watch started"));
	}

	void MaybeStartAutoTest(UWorld* World)
	{
		// Once per process: only the first game world (the loaded test save) runs the sequence.
		static bool bStarted = false;
		if (bStarted || !FParse::Param(FCommandLine::Get(), TEXT("DDCAutoTest")))
		{
			return;
		}
		bStarted = true;
		StartAutoTest(World, 20.0f);
	}
}

#endif
