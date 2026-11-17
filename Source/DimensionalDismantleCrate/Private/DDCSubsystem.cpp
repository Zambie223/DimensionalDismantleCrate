#include "DDCSubsystem.h"

#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "DDCLog.h"
#include "Engine/GameInstance.h"
#if DDC_DEV_COMMANDS
#include "DDCDevTools.h"
#include "Misc/ScopeExit.h"
#endif
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Buildables/FGCentralStorageContainer.h"
#include "FGCentralStorageSubsystem.h"
#include "FGCrate.h"
#include "FGInventoryComponent.h"
#include "FGSchematic.h"
#include "FGSchematicManager.h"
#include "Resources/FGEquipmentDescriptor.h"
#include "Resources/FGItemDescriptor.h"
#include "Subsystem/SubsystemActorManager.h"
#include "TimerManager.h"

namespace
{
	/** Debounce for Mod Options changes; some widgets fire once per step. */
	constexpr float ConfigChangeDebounceSeconds = 0.25f;

	/** Safety net next to the spawn handler: finds crates that appeared without a spawn event (for example during load). */
	constexpr float RescanIntervalSeconds = 10.0f;

	const TCHAR* NetModeName(const UWorld* World)
	{
		if (!World)
		{
			return TEXT("no world");
		}
		switch (World->GetNetMode())
		{
		case NM_Standalone: return TEXT("single player");
		case NM_DedicatedServer: return TEXT("dedicated server");
		case NM_ListenServer: return TEXT("listen server (host)");
		case NM_Client: return TEXT("client");
		default: return TEXT("unknown");
		}
	}

	const TCHAR* CrateTypeName(const EFGCrateType Type)
	{
		switch (Type)
		{
		case EFGCrateType::CT_DismantleCrate: return TEXT("dismantle");
		case EFGCrateType::CT_DeathCrate: return TEXT("death");
		default: return TEXT("none");
		}
	}

	UConfigProperty* FindProperty(UConfigPropertySection* Section, const FString& Key, FString* OutCanonicalKey = nullptr)
	{
		if (!Section)
		{
			return nullptr;
		}
		for (const TPair<FString, TObjectPtr<UConfigProperty>>& Pair : Section->SectionProperties)
		{
			if (Pair.Key.Equals(Key, ESearchCase::IgnoreCase))
			{
				if (OutCanonicalKey)
				{
					*OutCanonicalKey = Pair.Key;
				}
				return Pair.Value;
			}
		}
		return nullptr;
	}

	/** Accepts true/false, on/off, yes/no and 1/0. */
	bool ParseSwitch(const FString& Text, bool& OutValue)
	{
		for (const TCHAR* Word : {TEXT("true"), TEXT("on"), TEXT("yes"), TEXT("1")})
		{
			if (Text.Equals(Word, ESearchCase::IgnoreCase))
			{
				OutValue = true;
				return true;
			}
		}
		for (const TCHAR* Word : {TEXT("false"), TEXT("off"), TEXT("no"), TEXT("0")})
		{
			if (Text.Equals(Word, ESearchCase::IgnoreCase))
			{
				OutValue = false;
				return true;
			}
		}
		return false;
	}

	/** Accepts 0/1/2 or the mode names (percrate, shared, perslot). */
	bool ParseRateMode(const FString& Text, int32& OutValue)
	{
		if (Text.IsNumeric())
		{
			const int32 Number = FCString::Atoi(*Text);
			if (Number >= DDC::RateModeMin && Number <= DDC::RateModeMax && FString::FromInt(Number) == Text)
			{
				OutValue = Number;
				return true;
			}
			return false;
		}
		const FString Compact = Text.Replace(TEXT(" "), TEXT("")).Replace(TEXT("_"), TEXT("")).Replace(TEXT("-"), TEXT(""));
		if (Compact.Equals(TEXT("PerCrate"), ESearchCase::IgnoreCase)) { OutValue = static_cast<int32>(DDC::ERateMode::PerCrate); return true; }
		if (Compact.Equals(TEXT("Shared"), ESearchCase::IgnoreCase)) { OutValue = static_cast<int32>(DDC::ERateMode::Shared); return true; }
		if (Compact.Equals(TEXT("PerSlot"), ESearchCase::IgnoreCase) || Compact.Equals(TEXT("PerStackSlot"), ESearchCase::IgnoreCase))
		{
			OutValue = static_cast<int32>(DDC::ERateMode::PerSlot);
			return true;
		}
		return false;
	}
}

ADimensionalDismantleCrateSubsystem::ADimensionalDismantleCrateSubsystem()
{
	// Server only, never replicated: clients see the results through the crates' and the depot's own replication.
	ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer;
	bReplicates = false;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

ADimensionalDismantleCrateSubsystem* ADimensionalDismantleCrateSubsystem::Get(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	USubsystemActorManager* Manager = World->GetSubsystem<USubsystemActorManager>();
	return Manager ? Manager->GetSubsystemActor<ADimensionalDismantleCrateSubsystem>() : nullptr;
}

UConfigPropertySection* ADimensionalDismantleCrateSubsystem::GetConfigRootSection(const UWorld* World)
{
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UConfigManager* ConfigManager = GameInstance ? GameInstance->GetSubsystem<UConfigManager>() : nullptr;
	if (!ConfigManager)
	{
		return nullptr;
	}
	FConfigId ConfigId;
	ConfigId.ModReference = DDC::ModReference;
	return ConfigManager->GetConfigurationRootSection(ConfigId);
}

// ---------------------------------------------------------------------------------------------------------------------
// Lifecycle

void ADimensionalDismantleCrateSubsystem::Init()
{
	Super::Init();
	if (!HasAuthority())
	{
		return;
	}

	DepotResearchClass = TSoftClassPtr<UFGSchematic>(FSoftObjectPath(DDC::DepotResearchClassPath)).LoadSynchronous();
	if (!DepotResearchClass)
	{
		UE_LOG(LogDimensionalDismantleCrate, Error, TEXT("Could not load the Dimensional Depot research %s; crates will not upload. The game may have moved it."), DDC::DepotResearchClassPath);
	}

	ReadConfig();
	BindConfig();

	if (UWorld* World = GetWorld())
	{
		ActorSpawnedHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &ADimensionalDismantleCrateSubsystem::OnActorSpawned));
	}
	Rescan();
	UE_LOG(LogDimensionalDismantleCrate, Log, TEXT("Init on %s: %d crate(s) in the world"), NetModeName(GetWorld()), Crates.Num());
}

void ADimensionalDismantleCrateSubsystem::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		// SpawnOnServer means this never happens; kept so a policy change can't make clients upload.
		SetActorTickEnabled(false);
		return;
	}
	SetActorTickEnabled(true);
	GetWorldTimerManager().SetTimer(RescanTimer, this, &ADimensionalDismantleCrateSubsystem::Rescan, RescanIntervalSeconds, true);
#if DDC_DEV_COMMANDS
	DDC::Dev::MaybeStartAutoTest(GetWorld());
#endif
}

void ADimensionalDismantleCrateSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
		if (ActorSpawnedHandle.IsValid())
		{
			World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
		}
	}
	ActorSpawnedHandle.Reset();
	if (UConfigPropertySection* Section = BoundConfigSection.Get())
	{
		Section->OnPropertyValueChanged.RemoveDynamic(this, &ADimensionalDismantleCrateSubsystem::OnConfigValueChanged);
	}
	BoundConfigSection.Reset();
	Crates.Reset();
	TrackedKeys.Reset();
	PendingCrates.Reset();
	OverflowOwed.Reset();
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------------------------------------------------
// Crate tracking

void ADimensionalDismantleCrateSubsystem::TrackCrate(AFGCrate* Crate)
{
	if (!Crate)
	{
		return;
	}
	if (bTicking)
	{
		// Something spawned a crate while the upload loop holds references into Crates; add it after the loop.
		PendingCrates.AddUnique(Crate);
		return;
	}
	bool bAlreadyTracked = false;
	TrackedKeys.Add(TObjectKey<AFGCrate>(Crate), &bAlreadyTracked);
	if (bAlreadyTracked)
	{
		return;
	}
	FTrackedCrate& Entry = Crates.AddDefaulted_GetRef();
	Entry.Crate = Crate;
	Entry.Key = TObjectKey<AFGCrate>(Crate);
}

void ADimensionalDismantleCrateSubsystem::OnActorSpawned(AActor* Actor)
{
	// Tracks every crate. The type is only set right after spawning, so eligibility is checked on every tick instead.
	if (AFGCrate* Crate = Cast<AFGCrate>(Actor))
	{
		TrackCrate(Crate);
	}
}

void ADimensionalDismantleCrateSubsystem::Rescan()
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AFGCrate> It(World); It; ++It)
		{
			TrackCrate(*It);
		}
	}
	// Re-bind in case the configuration was registered late or replaced.
	BindConfig();
}

void ADimensionalDismantleCrateSubsystem::PruneInvalidCrates()
{
	const int32 Before = Crates.Num();
	Crates.RemoveAll([this](const FTrackedCrate& Entry)
	{
		const AFGCrate* Crate = Entry.Crate.Get();
		if (IsValid(Crate) && !Crate->IsActorBeingDestroyed())
		{
			return false;
		}
		TrackedKeys.Remove(Entry.Key);
		return true;
	});
	if (Crates.Num() != Before && SharedCursor >= Crates.Num())
	{
		SharedCursor = 0;
	}
}

bool ADimensionalDismantleCrateSubsystem::IsEligible(const AFGCrate* Crate) const
{
	if (!IsValid(Crate) || Crate->IsActorBeingDestroyed() || !Crate->HasActorBegunPlay() || !Crate->GetInventory())
	{
		return false;
	}
	switch (Crate->GetCrateType())
	{
	case EFGCrateType::CT_DismantleCrate: return true;
	case EFGCrateType::CT_DeathCrate: return bIncludeDeathCrates;
	default: return false; // CT_None: crates of unknown origin (for example from other mods) are left alone.
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Gate and settings

int32 ADimensionalDismantleCrateSubsystem::CountUploadersInWorld() const
{
	int32 Count = 0;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AFGCentralStorageContainer> It(World); It; ++It)
		{
			if (IsValid(*It) && !It->IsActorBeingDestroyed())
			{
				++Count;
			}
		}
	}
	return Count;
}

bool ADimensionalDismantleCrateSubsystem::IsGateOpen()
{
	// Uploaders are counted in the world: AFGCentralStorageSubsystem::IsCentralStorageBuilt() returned false in a
	// loaded save with 16 uploaders (its registration list stayed empty), so the game's own list can't be trusted here.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now - LastUploaderCountSeconds >= 1.0 || LastUploaderCountSeconds < 0.0)
	{
		LastUploaderCountSeconds = Now;
		UploadersInWorld = CountUploadersInWorld();
	}
	if (UploadersInWorld == 0)
	{
		return false;
	}
	if (!bResearchDone)
	{
		// Research can't be undone in normal play, so it is only looked up until it is found.
		const AFGSchematicManager* Schematics = AFGSchematicManager::Get(GetWorld());
		bResearchDone = Schematics && DepotResearchClass && Schematics->IsSchematicPurchased(DepotResearchClass);
	}
	return bResearchDone;
}

void ADimensionalDismantleCrateSubsystem::ReadConfig()
{
	const bool bOldEnabled = bEnabled;
	const DDC::ERateMode OldMode = RateMode;
	const bool bOldDeath = bIncludeDeathCrates;
	const bool bOldDeathEquipment = bUploadDeathCrateEquipment;

	bEnabled = true;
	RateMode = DDC::ERateMode::PerCrate;
	bIncludeDeathCrates = false;
	bUploadDeathCrateEquipment = false;
	bOverflowToDepot = false;

	UConfigPropertySection* Root = GetConfigRootSection(GetWorld());
	if (!Root)
	{
		UE_LOG(LogDimensionalDismantleCrate, Warning, TEXT("Configuration '%s' is not registered; using the defaults"), DDC::ModReference);
	}
	else
	{
		if (const UConfigPropertyBool* Enabled = Cast<UConfigPropertyBool>(FindProperty(Root, DDC::KeyEnabled)))
		{
			bEnabled = Enabled->Value;
		}
		if (const UConfigPropertyInteger* Mode = Cast<UConfigPropertyInteger>(FindProperty(Root, DDC::KeyRateMode)))
		{
			if (Mode->Value >= DDC::RateModeMin && Mode->Value <= DDC::RateModeMax)
			{
				RateMode = static_cast<DDC::ERateMode>(Mode->Value);
			}
			else
			{
				UE_LOG(LogDimensionalDismantleCrate, Warning, TEXT("%s = %d is not %d-%d; using %d (%s)"), DDC::KeyRateMode, Mode->Value,
					DDC::RateModeMin, DDC::RateModeMax, static_cast<int32>(RateMode), DDC::RateModeName(RateMode));
			}
		}
		if (const UConfigPropertyBool* Death = Cast<UConfigPropertyBool>(FindProperty(Root, DDC::KeyIncludeDeathCrates)))
		{
			bIncludeDeathCrates = Death->Value;
		}
		if (const UConfigPropertyBool* Equipment = Cast<UConfigPropertyBool>(FindProperty(Root, DDC::KeyUploadDeathCrateEquipment)))
		{
			bUploadDeathCrateEquipment = Equipment->Value;
		}
		if (const UConfigPropertyBool* Overflow = Cast<UConfigPropertyBool>(FindProperty(Root, DDC::KeyOverflowToDepot)))
		{
			bOverflowToDepot = Overflow->Value;
		}
	}
	if (!bOverflowToDepot)
	{
		// Turned off: overflow already in crates uploads at normal crate speed from now on.
		OverflowOwed.Reset();
	}

	if (RateMode != OldMode || bEnabled != bOldEnabled || bIncludeDeathCrates != bOldDeath || bUploadDeathCrateEquipment != bOldDeathEquipment)
	{
		// A different mode means different timers; start every crate from idle, like a freshly built uploader.
		ResetAllTimers();
	}
	UE_LOG(LogDimensionalDismantleCrate, Log, TEXT("Settings: %s"), *DescribeSettings());
}

FString ADimensionalDismantleCrateSubsystem::DescribeSettings() const
{
	return FString::Printf(TEXT("%s=%s, %s=%d (%s), %s=%s, %s=%s, %s=%s"),
		DDC::KeyEnabled, bEnabled ? TEXT("true") : TEXT("false"),
		DDC::KeyRateMode, static_cast<int32>(RateMode), DDC::RateModeName(RateMode),
		DDC::KeyIncludeDeathCrates, bIncludeDeathCrates ? TEXT("true") : TEXT("false"),
		DDC::KeyUploadDeathCrateEquipment, bUploadDeathCrateEquipment ? TEXT("true") : TEXT("false"),
		DDC::KeyOverflowToDepot, bOverflowToDepot ? TEXT("true") : TEXT("false"));
}

void ADimensionalDismantleCrateSubsystem::BindConfig()
{
	UConfigPropertySection* Root = GetConfigRootSection(GetWorld());
	if (!Root || BoundConfigSection.Get() == Root)
	{
		return;
	}
	const bool bRebind = BoundConfigSection.IsValid();
	if (UConfigPropertySection* Old = BoundConfigSection.Get())
	{
		Old->OnPropertyValueChanged.RemoveDynamic(this, &ADimensionalDismantleCrateSubsystem::OnConfigValueChanged);
	}
	Root->OnPropertyValueChanged.AddUniqueDynamic(this, &ADimensionalDismantleCrateSubsystem::OnConfigValueChanged);
	BoundConfigSection = Root;
	if (bRebind)
	{
		ReadConfig();
	}
}

void ADimensionalDismantleCrateSubsystem::OnConfigValueChanged()
{
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(DebounceTimer, this, &ADimensionalDismantleCrateSubsystem::OnDebounceElapsed, ConfigChangeDebounceSeconds, false);
	}
}

void ADimensionalDismantleCrateSubsystem::OnDebounceElapsed()
{
	ReadConfig();
}

void ADimensionalDismantleCrateSubsystem::ResetAllTimers()
{
	for (FTrackedCrate& Entry : Crates)
	{
		Entry.Timer.Reset();
		Entry.SlotTimers.Reset();
	}
	SharedTimer.Reset();
}

// ---------------------------------------------------------------------------------------------------------------------
// Uploading

void ADimensionalDismantleCrateSubsystem::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
#if DDC_DEV_COMMANDS
	const double TickStart = FPlatformTime::Seconds();
	ON_SCOPE_EXIT
	{
		const double Elapsed = FPlatformTime::Seconds() - TickStart;
		TickSecondsTotal += Elapsed;
		TickSecondsMax = FMath::Max(TickSecondsMax, Elapsed);
		++TickCount;
	};
#endif
	PruneInvalidCrates();
	const bool bCrateWork = bEnabled && Crates.Num() > 0;
	const bool bOverflowWork = bOverflowToDepot && OverflowOwed.Num() > 0;
	if (!bCrateWork && !bOverflowWork)
	{
		return;
	}

	const bool bGateOpen = IsGateOpen();
	if (bGateOpen != bLastGateOpen)
	{
		UE_LOG(LogDimensionalDismantleCrate, Log, TEXT("Crate uploading %s"), bGateOpen
			? TEXT("started (Dimensional Depot researched and an uploader is built)")
			: TEXT("paused (needs the Dimensional Depot research and at least one uploader)"));
		bLastGateOpen = bGateOpen;
		if (!bGateOpen)
		{
			ResetAllTimers();
		}
	}
	if (!bGateOpen)
	{
		OverflowOwed.Reset();
		return;
	}

	AFGCentralStorageSubsystem* CentralStorage = AFGCentralStorageSubsystem::Get(GetWorld());
	if (!CentralStorage)
	{
		return;
	}

	bTicking = true;
	if (bOverflowWork)
	{
		TickOverflowFromCrates(CentralStorage);
	}
	const float TimeToUpload = CentralStorage->GetCentralStorageTimeToUpload();
	if (bCrateWork && FMath::IsFinite(TimeToUpload) && TimeToUpload > 0.0f)
	{
		switch (RateMode)
		{
		case DDC::ERateMode::Shared: TickShared(DeltaSeconds, TimeToUpload, CentralStorage); break;
		case DDC::ERateMode::PerSlot: TickPerSlot(DeltaSeconds, TimeToUpload, CentralStorage); break;
		case DDC::ERateMode::PerCrate:
		default: TickPerCrate(DeltaSeconds, TimeToUpload, CentralStorage); break;
		}
	}
	bTicking = false;

	if (PendingCrates.Num() > 0)
	{
		TArray<TWeakObjectPtr<AFGCrate>> Pending = MoveTemp(PendingCrates);
		PendingCrates.Reset();
		for (const TWeakObjectPtr<AFGCrate>& Crate : Pending)
		{
			TrackCrate(Crate.Get());
		}
	}
}

void ADimensionalDismantleCrateSubsystem::TickPerCrate(const float DeltaSeconds, const float TimeToUpload, AFGCentralStorageSubsystem* CentralStorage)
{
	// An upload can destroy the crate (vanilla destroys empty crates); entries are pruned on the next tick.
	for (int32 Index = 0; Index < Crates.Num(); ++Index)
	{
		FTrackedCrate& Entry = Crates[Index];
		AFGCrate* Crate = Entry.Crate.Get();
		if (!IsEligible(Crate))
		{
			Entry.Timer.Reset();
			continue;
		}
		UFGInventoryComponent* Inventory = Crate->GetInventory();
		const bool bSkipEquipment = ShouldSkipEquipment(Crate);
		Entry.Timer.Step(DeltaSeconds, TimeToUpload, !Inventory->IsEmpty(),
			[&] { return CanUploadAnySlot(Inventory, CentralStorage, bSkipEquipment); },
			[&] { return TryUploadOneFromCrate(Inventory, CentralStorage, bSkipEquipment); });
	}
}

void ADimensionalDismantleCrateSubsystem::TickShared(const float DeltaSeconds, const float TimeToUpload, AFGCentralStorageSubsystem* CentralStorage)
{
	TArray<int32, TInlineAllocator<32>> Eligible;
	bool bAnyItems = false;
	for (int32 Index = 0; Index < Crates.Num(); ++Index)
	{
		const AFGCrate* Crate = Crates[Index].Crate.Get();
		if (IsEligible(Crate))
		{
			Eligible.Add(Index);
			bAnyItems |= !Crate->GetInventory()->IsEmpty();
		}
	}

	SharedTimer.Step(DeltaSeconds, TimeToUpload, bAnyItems,
		[&]
		{
			for (const int32 Index : Eligible)
			{
				const AFGCrate* Crate = Crates[Index].Crate.Get();
				if (CanUploadAnySlot(Crate->GetInventory(), CentralStorage, ShouldSkipEquipment(Crate)))
				{
					return true;
				}
			}
			return false;
		},
		[&]
		{
			// Round robin: start at the first eligible crate at or after the cursor, so every crate gets its turn.
			const int32 Count = Eligible.Num();
			int32 Start = 0;
			while (Start < Count && Eligible[Start] < SharedCursor)
			{
				++Start;
			}
			for (int32 Step = 0; Step < Count; ++Step)
			{
				const int32 Index = Eligible[(Start + Step) % Count];
				AFGCrate* Crate = Crates[Index].Crate.Get();
				if (IsEligible(Crate) && TryUploadOneFromCrate(Crate->GetInventory(), CentralStorage, ShouldSkipEquipment(Crate)))
				{
					SharedCursor = Index + 1;
					return true;
				}
			}
			return false;
		});
}

void ADimensionalDismantleCrateSubsystem::TickPerSlot(const float DeltaSeconds, const float TimeToUpload, AFGCentralStorageSubsystem* CentralStorage)
{
	for (int32 Index = 0; Index < Crates.Num(); ++Index)
	{
		FTrackedCrate& Entry = Crates[Index];
		AFGCrate* Crate = Entry.Crate.Get();
		if (!IsEligible(Crate))
		{
			Entry.SlotTimers.Reset();
			continue;
		}
		UFGInventoryComponent* Inventory = Crate->GetInventory();
		const bool bSkipEquipment = ShouldSkipEquipment(Crate);
		// Merging crates can resize the inventory; new slots start idle.
		Entry.SlotTimers.SetNum(Inventory->GetSizeLinear());
		for (int32 Slot = 0; Slot < Entry.SlotTimers.Num(); ++Slot)
		{
			// Uploading the last item destroys the crate during the call, so stop as soon as that happens.
			if (!IsValid(Crate) || Crate->IsActorBeingDestroyed())
			{
				break;
			}
			Entry.SlotTimers[Slot].Step(DeltaSeconds, TimeToUpload, !Inventory->IsIndexEmpty(Slot),
				[&] { return CanUploadSlot(Inventory, Slot, CentralStorage, bSkipEquipment); },
				[&] { return TryUploadSlot(Inventory, Slot, CentralStorage, bSkipEquipment); });
		}
	}
}

bool ADimensionalDismantleCrateSubsystem::ShouldSkipEquipment(const AFGCrate* Crate) const
{
	return Crate && Crate->GetCrateType() == EFGCrateType::CT_DeathCrate && !bUploadDeathCrateEquipment;
}

bool ADimensionalDismantleCrateSubsystem::IsEquipmentOrStateful(const FInventoryStack& Stack)
{
	const UClass* ItemClass = Stack.Item.GetItemClass();
	return Stack.Item.HasState() || (ItemClass && ItemClass->IsChildOf(UFGEquipmentDescriptor::StaticClass()));
}

bool ADimensionalDismantleCrateSubsystem::CanUploadSlot(const UFGInventoryComponent* Inventory, const int32 SlotIndex, const AFGCentralStorageSubsystem* CentralStorage, const bool bSkipEquipment)
{
	FInventoryStack Stack;
	return Inventory->GetStackFromIndex(SlotIndex, Stack) && Stack.HasItems()
		&& !(bSkipEquipment && IsEquipmentOrStateful(Stack))
		&& CentralStorage->CanUploadInventoryItemToCentralStorage(Stack.Item);
}

bool ADimensionalDismantleCrateSubsystem::CanUploadAnySlot(const UFGInventoryComponent* Inventory, const AFGCentralStorageSubsystem* CentralStorage, const bool bSkipEquipment)
{
	const int32 Size = Inventory->GetSizeLinear();
	for (int32 Slot = 0; Slot < Size; ++Slot)
	{
		if (CanUploadSlot(Inventory, Slot, CentralStorage, bSkipEquipment))
		{
			return true;
		}
	}
	return false;
}

bool ADimensionalDismantleCrateSubsystem::TryUploadSlot(UFGInventoryComponent* Inventory, const int32 SlotIndex, AFGCentralStorageSubsystem* CentralStorage, const bool bSkipEquipment)
{
	if (Inventory->IsIndexEmpty(SlotIndex))
	{
		return false;
	}
	if (bSkipEquipment)
	{
		FInventoryStack Stack;
		if (!Inventory->GetStackFromIndex(SlotIndex, Stack) || IsEquipmentOrStateful(Stack))
		{
			return false;
		}
	}
	// UploadItemFromInventoryToCentralStorage checks the item and the depot limit itself, and moves exactly one item.
	if (!CentralStorage->UploadItemFromInventoryToCentralStorage(Inventory, SlotIndex))
	{
		return false;
	}
	NoteUpload();
	return true;
}

bool ADimensionalDismantleCrateSubsystem::TryUploadOneFromCrate(UFGInventoryComponent* Inventory, AFGCentralStorageSubsystem* CentralStorage, const bool bSkipEquipment)
{
	// Same order as the vanilla uploader: the first slot that accepts an item wins.
	const int32 Size = Inventory->GetSizeLinear();
	for (int32 Slot = 0; Slot < Size; ++Slot)
	{
		if (TryUploadSlot(Inventory, Slot, CentralStorage, bSkipEquipment))
		{
			return true;
		}
	}
	return false;
}

void ADimensionalDismantleCrateSubsystem::NoteUpload()
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (FirstUploadSeconds < 0.0)
	{
		FirstUploadSeconds = Now;
	}
	LastUploadSeconds = Now;
	++ItemsUploaded;
}

// ---------------------------------------------------------------------------------------------------------------------
// Dismantle overflow straight to the depot

bool ADimensionalDismantleCrateSubsystem::SendOverflowToDepot(const TArray<FInventoryStack>& Refund, TArray<FInventoryStack>& OutRemaining)
{
	OutRemaining.Reset();
	if (!HasAuthority() || !bOverflowToDepot || Refund.Num() == 0 || !IsGateOpen())
	{
		return false;
	}
	AFGCentralStorageSubsystem* CentralStorage = AFGCentralStorageSubsystem::Get(GetWorld());
	UFGInventoryComponent* Scratch = GetScratchInventory();
	if (!CentralStorage || !Scratch)
	{
		return false;
	}

	RefillOverflowTokens();
	int32 Sent = 0;
	int32 Kept = 0;
	for (const FInventoryStack& Stack : Refund)
	{
		if (!Stack.HasItems())
		{
			continue;
		}
		if (!CentralStorage->CanUploadInventoryItemToCentralStorage(Stack.Item))
		{
			// The depot won't take it (at its limit, or not an item the depot stores): it goes into the crate as before.
			OutRemaining.Add(Stack);
			Kept += Stack.NumItems;
			continue;
		}
		if (OverflowTokens < 1.0f)
		{
			// Rate used up: into the crate, and sent from there at the overflow rate.
			OutRemaining.Add(Stack);
			Kept += Stack.NumItems;
			OverflowOwed.FindOrAdd(Stack.Item.GetItemClass()) += Stack.NumItems;
			continue;
		}

		// The game only uploads from an inventory slot. One slot sized to the whole stack, because refund stacks can be
		// larger than a normal stack; the item (and any state it carries) is uploaded exactly as from a crate.
		Scratch->Empty();
		Scratch->AddArbitrarySlotSize(0, Stack.NumItems);
		if (Scratch->AddStackToIndex(0, Stack, true) != Stack.NumItems)
		{
			// Not expected; the stack goes into the crate untouched.
			Scratch->Empty();
			OutRemaining.Add(Stack);
			Kept += Stack.NumItems;
			continue;
		}
		while (OverflowTokens >= 1.0f && !Scratch->IsIndexEmpty(0) && CentralStorage->UploadItemFromInventoryToCentralStorage(Scratch, 0))
		{
			OverflowTokens -= 1.0f;
			++Sent;
		}
		FInventoryStack Left;
		if (Scratch->GetStackFromIndex(0, Left) && Left.HasItems())
		{
			OutRemaining.Add(Left);
			Kept += Left.NumItems;
			if (OverflowTokens < 1.0f)
			{
				// Stopped by the rate, not by the depot.
				OverflowOwed.FindOrAdd(Left.Item.GetItemClass()) += Left.NumItems;
			}
		}
		Scratch->Empty();
	}

	ItemsSentDirectly += Sent;
	UE_LOG(LogDimensionalDismantleCrate, Log, TEXT("Dismantle overflow: sent %d item(s) straight to the depot, %d item(s) go into a crate"), Sent, Kept);
	return true;
}

UFGInventoryComponent* ADimensionalDismantleCrateSubsystem::GetScratchInventory()
{
	if (!ScratchInventory)
	{
		// Never replicated or saved (this subsystem is neither), and always emptied again before SendOverflowToDepot returns.
		ScratchInventory = NewObject<UFGInventoryComponent>(this, TEXT("DDCOverflowScratch"));
		ScratchInventory->SetIsReplicated(false);
		ScratchInventory->RegisterComponent();
		ScratchInventory->Resize(1);
	}
	return ScratchInventory;
}

bool ADimensionalDismantleCrateSubsystem::HasRemoteClients() const
{
	const UWorld* World = GetWorld();
	const UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
	return Driver && Driver->ClientConnections.Num() > 0;
}

void ADimensionalDismantleCrateSubsystem::RefillOverflowTokens()
{
	const bool bNetworked = HasRemoteClients();
	float Rate = bNetworked ? DDC::OverflowItemsPerSecondNetworked : DDC::OverflowItemsPerSecondLocal;
	float Burst = bNetworked ? DDC::OverflowBurstNetworked : DDC::OverflowBurstLocal;
#if DDC_DEV_COMMANDS
	Rate = DevOverflowRate > 0.0f ? DevOverflowRate : Rate;
	Burst = DevOverflowBurst > 0.0f ? DevOverflowBurst : Burst;
#endif
	const double Now = FPlatformTime::Seconds();
	const float Refilled = OverflowTokensRefilledAt < 0.0 ? Burst : OverflowTokens + static_cast<float>((Now - OverflowTokensRefilledAt) * Rate);
	OverflowTokens = FMath::Min(Burst, Refilled);
	OverflowTokensRefilledAt = Now;
}

void ADimensionalDismantleCrateSubsystem::TickOverflowFromCrates(AFGCentralStorageSubsystem* CentralStorage)
{
	RefillOverflowTokens();
	for (auto It = OverflowOwed.CreateIterator(); It && OverflowTokens >= 1.0f; ++It)
	{
		const TSubclassOf<UFGItemDescriptor> Item = It.Key();
		int32& Owed = It.Value();
		bool bDepotRefused = false;
		// Newest crates first: a crate made for the overflow was tracked last. Overflow merged into an older crate is
		// found there too.
		for (int32 Index = Crates.Num() - 1; Index >= 0 && Owed > 0 && OverflowTokens >= 1.0f && !bDepotRefused; --Index)
		{
			AFGCrate* Crate = Crates[Index].Crate.Get();
			UFGInventoryComponent* Inventory = IsValid(Crate) && !Crate->IsActorBeingDestroyed() && Crate->HasActorBegunPlay()
				&& Crate->GetCrateType() == EFGCrateType::CT_DismantleCrate ? Crate->GetInventory() : nullptr;
			if (!Inventory)
			{
				continue;
			}
			const int32 Size = Inventory->GetSizeLinear();
			for (int32 Slot = 0; Slot < Size && Owed > 0 && OverflowTokens >= 1.0f; ++Slot)
			{
				FInventoryStack Stack;
				if (!Inventory->GetStackFromIndex(Slot, Stack) || !Stack.HasItems() || Stack.Item.GetItemClass() != Item)
				{
					continue;
				}
				while (Owed > 0 && OverflowTokens >= 1.0f && !Inventory->IsIndexEmpty(Slot))
				{
					if (!CentralStorage->UploadItemFromInventoryToCentralStorage(Inventory, Slot))
					{
						bDepotRefused = true;
						break;
					}
					--Owed;
					OverflowTokens -= 1.0f;
					++ItemsSentDirectly;
					if (!IsValid(Crate) || Crate->IsActorBeingDestroyed())
					{
						break; // emptied; the game destroys empty crates
					}
				}
				if (bDepotRefused || !IsValid(Crate) || Crate->IsActorBeingDestroyed())
				{
					break;
				}
			}
		}
		// Still owed with rate to spare: the depot is full of the item, or no crate holds it any more (a player took
		// it). Either way the rest is no longer overflow; anything still in a crate uploads at normal crate speed.
		if (bDepotRefused || (Owed > 0 && OverflowTokens >= 1.0f))
		{
			Owed = 0;
		}
		if (Owed <= 0)
		{
			It.RemoveCurrent();
		}
	}
}

#if DDC_DEV_COMMANDS
int32 ADimensionalDismantleCrateSubsystem::GetOverflowOwedItems() const
{
	int32 Total = 0;
	for (const TPair<TSubclassOf<UFGItemDescriptor>, int32>& Pair : OverflowOwed)
	{
		Total += Pair.Value;
	}
	return Total;
}
#endif

// ---------------------------------------------------------------------------------------------------------------------
// Console support

bool ADimensionalDismantleCrateSubsystem::SetConfigValue(const UWorld* World, const FString& Key, const FString& ValueText, FString& OutMessage)
{
	if (World && World->GetNetMode() == NM_Client)
	{
		OutMessage = TEXT("Refused: this is a client. Only the host's or server's settings are used.");
		return false;
	}
	UConfigPropertySection* Root = GetConfigRootSection(World);
	FString CanonicalKey;
	UConfigProperty* Property = FindProperty(Root, Key, &CanonicalKey);
	if (!Property)
	{
		OutMessage = FString::Printf(TEXT("Unknown key '%s' (or the configuration is not registered). Valid keys: %s, %s, %s, %s, %s"),
			*Key, DDC::KeyEnabled, DDC::KeyRateMode, DDC::KeyIncludeDeathCrates, DDC::KeyUploadDeathCrateEquipment, DDC::KeyOverflowToDepot);
		return false;
	}

	if (UConfigPropertyBool* Bool = Cast<UConfigPropertyBool>(Property))
	{
		bool bValue = false;
		if (!ParseSwitch(ValueText, bValue))
		{
			OutMessage = FString::Printf(TEXT("Refused: %s takes true or false."), *CanonicalKey);
			return false;
		}
		Bool->Value = bValue;
		// MarkDirty saves the file, refreshes SML's caches and fires OnPropertyValueChanged, which applies the value.
		Bool->MarkDirty();
		OutMessage = FString::Printf(TEXT("%s set to %s."), *CanonicalKey, bValue ? TEXT("true") : TEXT("false"));
		return true;
	}
	if (UConfigPropertyInteger* Integer = Cast<UConfigPropertyInteger>(Property))
	{
		int32 Value = 0;
		if (!ParseRateMode(ValueText, Value))
		{
			OutMessage = FString::Printf(TEXT("Refused: %s takes 0 (PerCrate), 1 (Shared) or 2 (PerSlot)."), *CanonicalKey);
			return false;
		}
		Integer->Value = Value;
		Integer->MarkDirty();
		OutMessage = FString::Printf(TEXT("%s set to %d (%s)."), *CanonicalKey, Value, DDC::RateModeName(static_cast<DDC::ERateMode>(Value)));
		return true;
	}
	OutMessage = FString::Printf(TEXT("Key '%s' has an unexpected type."), *CanonicalKey);
	return false;
}

void ADimensionalDismantleCrateSubsystem::DescribeStatus(FOutputDevice& Ar) const
{
	UWorld* World = GetWorld();
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Dimensional Dismantle Crate status (%s)"), NetModeName(World)));
	Lines.Add(FString::Printf(TEXT("  Settings: %s"), *DescribeSettings()));

	// Non-const: the game's GetCentralStorageContainers() is not const.
	AFGCentralStorageSubsystem* CentralStorage = AFGCentralStorageSubsystem::Get(World);
	const AFGSchematicManager* Schematics = AFGSchematicManager::Get(World);
	const bool bResearch = Schematics && DepotResearchClass && Schematics->IsSchematicPurchased(DepotResearchClass);
	const int32 Uploaders = CountUploadersInWorld();
	const int32 Registered = CentralStorage ? CentralStorage->GetCentralStorageContainers().Num() : 0;
	Lines.Add(FString::Printf(TEXT("  Gate: research %s, uploaders built %d (registered with the depot: %d) -> %s"), bResearch ? TEXT("done") : TEXT("not done"),
		Uploaders, Registered, (bResearch && Uploaders > 0) ? TEXT("open") : TEXT("closed")));
	if (CentralStorage)
	{
		const float Time = CentralStorage->GetCentralStorageTimeToUpload();
		Lines.Add(FString::Printf(TEXT("  Upload speed: %.4f s/item (%.2f items/min per uploader)"), Time, Time > 0.0f ? 60.0f / Time : 0.0f));
	}

	constexpr int32 MaxCratesListed = 20;
	int32 Tracked = 0;
	int32 EligibleCount = 0;
	int64 ItemsInEligible = 0;
	TArray<FString> CrateLines;
	for (const FTrackedCrate& Entry : Crates)
	{
		const AFGCrate* Crate = Entry.Crate.Get();
		if (!IsValid(Crate))
		{
			continue;
		}
		++Tracked;
		int64 ItemsInCrate = 0;
		const UFGInventoryComponent* Inventory = Crate->GetInventory();
		const int32 Size = Inventory ? Inventory->GetSizeLinear() : 0;
		for (int32 Slot = 0; Slot < Size; ++Slot)
		{
			FInventoryStack Stack;
			if (Inventory->GetStackFromIndex(Slot, Stack) && Stack.HasItems())
			{
				ItemsInCrate += Stack.NumItems;
			}
		}
		const bool bEligible = IsEligible(Crate);
		if (bEligible)
		{
			++EligibleCount;
			ItemsInEligible += ItemsInCrate;
		}
		if (CrateLines.Num() < MaxCratesListed)
		{
			CrateLines.Add(FString::Printf(TEXT("    %s: %s crate, %s, %lld item(s) in %d slot(s), timer %.3f"), *Crate->GetName(),
				CrateTypeName(Crate->GetCrateType()), bEligible ? TEXT("eligible") : TEXT("ignored"), ItemsInCrate, Size, Entry.Timer.Time));
		}
	}
	Lines.Add(FString::Printf(TEXT("  Crates: %d tracked, %d eligible, %lld item(s) in eligible crates%s"), Tracked, EligibleCount, ItemsInEligible,
		Tracked > MaxCratesListed ? *FString::Printf(TEXT(" (first %d listed)"), MaxCratesListed) : TEXT("")));
	Lines.Append(CrateLines);

	const double Span = LastUploadSeconds - FirstUploadSeconds;
	Lines.Add(FString::Printf(TEXT("  Uploaded since the world started: %lld item(s)%s"), ItemsUploaded,
		(ItemsUploaded > 1 && Span > 0.0)
			? *FString::Printf(TEXT(", %.2f items/min between the first and last upload"), (ItemsUploaded - 1) * 60.0 / Span)
			: TEXT("")));
	int32 OverflowWaiting = 0;
	for (const TPair<TSubclassOf<UFGItemDescriptor>, int32>& Pair : OverflowOwed)
	{
		OverflowWaiting += Pair.Value;
	}
	Lines.Add(FString::Printf(TEXT("  Dismantle overflow straight to the depot: %s, %lld item(s) sent since the world started, %d waiting in crates"),
		bOverflowToDepot ? TEXT("on") : TEXT("off"), ItemsSentDirectly, OverflowWaiting));

	for (const FString& Line : Lines)
	{
		Ar.Log(Line);
		UE_LOG(LogDimensionalDismantleCrate, Log, TEXT("%s"), *Line);
	}
}
