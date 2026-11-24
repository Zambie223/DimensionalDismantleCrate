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
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Buildables/FGBuildableResourceSink.h"
#include "Buildables/FGCentralStorageContainer.h"
#include "FGCentralStorageSubsystem.h"
#include "FGChatManager.h"
#include "FGCrate.h"
#include "FGInventoryComponent.h"
#include "FGPlayerState.h"
#include "FGResourceSinkSubsystem.h"
#include "FGSchematic.h"
#include "FGSchematicManager.h"
#include "Resources/FGEquipmentDescriptor.h"
#include "Resources/FGItemDescriptor.h"
#include "Subsystem/SubsystemActorManager.h"
#include "TimerManager.h"

using DDC::EMoveResult;

namespace
{
	/** Debounce for Mod Options changes; some widgets fire once per step. */
	constexpr float ConfigChangeDebounceSeconds = 0.25f;

	/** Safety net next to the spawn handler: finds crates that appeared without a spawn event (for example during load). */
	constexpr float RescanIntervalSeconds = 10.0f;

	/** Most items named in one notice line; the rest are summed up as "and N more". */
	constexpr int32 MaxItemsNamedPerNotice = 5;

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

	const TCHAR* OnOff(const bool bValue) { return bValue ? TEXT("true") : TEXT("false"); }

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

	bool ReadBool(UConfigPropertySection* Root, const TCHAR* Key, const bool bDefault)
	{
		const UConfigPropertyBool* Property = Cast<UConfigPropertyBool>(FindProperty(Root, Key));
		return Property ? Property->Value : bDefault;
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

	/** Accepts 0-4 or the mode names (percrate, shared, perslot, instant, peruploader). */
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
		struct FModeName { const TCHAR* Text; DDC::ERateMode Mode; };
		for (const FModeName& Name : {
			FModeName{TEXT("PerCrate"), DDC::ERateMode::PerCrate},
			FModeName{TEXT("Shared"), DDC::ERateMode::Shared},
			FModeName{TEXT("PerSlot"), DDC::ERateMode::PerSlot},
			FModeName{TEXT("PerStackSlot"), DDC::ERateMode::PerSlot},
			FModeName{TEXT("Instant"), DDC::ERateMode::Instant},
			FModeName{TEXT("PerUploader"), DDC::ERateMode::PerUploader},
			FModeName{TEXT("Uploaders"), DDC::ERateMode::PerUploader},
			FModeName{TEXT("SharedByAllUploaders"), DDC::ERateMode::PerUploader}})
		{
			if (Compact.Equals(Name.Text, ESearchCase::IgnoreCase))
			{
				OutValue = static_cast<int32>(Name.Mode);
				return true;
			}
		}
		return false;
	}

	/** "A (1), B (2) and C (3)", naming at most MaxItemsNamedPerNotice items. */
	FString JoinItems(const TArray<FString>& Items)
	{
		const int32 Named = FMath::Min(Items.Num(), MaxItemsNamedPerNotice);
		FString Text;
		for (int32 Index = 0; Index < Named; ++Index)
		{
			if (Index > 0)
			{
				Text += (Index == Named - 1 && Named == Items.Num()) ? TEXT(" and ") : TEXT(", ");
			}
			Text += Items[Index];
		}
		if (Items.Num() > Named)
		{
			Text += FString::Printf(TEXT(" and %d more"), Items.Num() - Named);
		}
		return Text;
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
	// Returns straight away while notices are off.
	GetWorldTimerManager().SetTimer(NoticeTimer, this, &ADimensionalDismantleCrateSubsystem::ScanForNotices, DDC::StuckScanSeconds, true);
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
	BlockedItems.Reset();
	PendingNotices.Reset();
	TickSinkSubsystem = nullptr;
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
	if (Crates.Num() != Before)
	{
		if (SharedCursor >= Crates.Num())
		{
			SharedCursor = 0;
		}
		if (RotationCursor >= Crates.Num())
		{
			RotationCursor = 0;
		}
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
		// Includes subclasses, such as Dimensional Sinking Depot's buildings, which upload too.
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

int32 ADimensionalDismantleCrateSubsystem::CountSinksInWorld() const
{
	int32 Count = 0;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AFGBuildableResourceSink> It(World); It; ++It)
		{
			if (IsValid(*It) && !It->IsActorBeingDestroyed())
			{
				++Count;
			}
		}
	}
	return Count;
}

void ADimensionalDismantleCrateSubsystem::RefreshBuildingCounts()
{
	// Uploaders are counted in the world: AFGCentralStorageSubsystem::IsCentralStorageBuilt() returned false in a
	// loaded save with 16 uploaders (its registration list stayed empty), so the game's own list can't be trusted here.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (LastBuildingCountSeconds >= 0.0 && Now - LastBuildingCountSeconds < 1.0)
	{
		return;
	}
	LastBuildingCountSeconds = Now;
	UploadersInWorld = CountUploadersInWorld();
#if DDC_DEV_COMMANDS
	if (DevUploadersOverride >= 0)
	{
		UploadersInWorld = DevUploadersOverride;
	}
#endif
	SinksInWorld = bSinkWhenDepotFull ? CountSinksInWorld() : 0;
}

bool ADimensionalDismantleCrateSubsystem::IsResearchDone()
{
	if (!bResearchDone)
	{
		// Research can't be undone in normal play, so it is only looked up until it is found.
		const AFGSchematicManager* Schematics = AFGSchematicManager::Get(GetWorld());
		bResearchDone = Schematics && DepotResearchClass && Schematics->IsSchematicPurchased(DepotResearchClass);
	}
	return bResearchDone;
}

bool ADimensionalDismantleCrateSubsystem::IsGateOpen()
{
	RefreshBuildingCounts();
	return UploadersInWorld > 0 && IsResearchDone();
}

void ADimensionalDismantleCrateSubsystem::ReadConfig()
{
	const bool bOldEnabled = bEnabled;
	const DDC::ERateMode OldMode = RateMode;
	const bool bOldDeath = bIncludeDeathCrates;
	const bool bOldDeathEquipment = bUploadDeathCrateEquipment;
	const bool bOldSink = bSinkWhenDepotFull;

	RateMode = DDC::ERateMode::PerCrate;
	UConfigPropertySection* Root = GetConfigRootSection(GetWorld());
	if (!Root)
	{
		UE_LOG(LogDimensionalDismantleCrate, Warning, TEXT("Configuration '%s' is not registered; using the defaults"), DDC::ModReference);
	}
	else if (const UConfigPropertyInteger* Mode = Cast<UConfigPropertyInteger>(FindProperty(Root, DDC::KeyRateMode)))
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
	// ReadBool returns the default when the configuration (or the key) is missing.
	bEnabled = ReadBool(Root, DDC::KeyEnabled, true);
	bIncludeDeathCrates = ReadBool(Root, DDC::KeyIncludeDeathCrates, false);
	bUploadDeathCrateEquipment = ReadBool(Root, DDC::KeyUploadDeathCrateEquipment, false);
	bOverflowToDepot = ReadBool(Root, DDC::KeyOverflowToDepot, false);
	bShowStuckNotices = ReadBool(Root, DDC::KeyShowStuckNotices, false);
	bSinkWhenDepotFull = ReadBool(Root, DDC::KeySinkWhenDepotFull, false);
	bRefundsFollowDepotPreference = ReadBool(Root, DDC::KeyRefundsFollowDepotPreference, false);

	if (!bOverflowToDepot)
	{
		// Turned off: overflow already in crates uploads at normal crate speed from now on.
		OverflowOwed.Reset();
	}
	if (!bShowStuckNotices)
	{
		// Starting over: items stuck now are announced again once notices are back on.
		BlockedItems.Reset();
		PendingNotices.Reset();
		bGateNoticeSent = false;
	}
	if (bSinkWhenDepotFull != bOldSink)
	{
		// Count sinks now rather than up to a second later.
		LastBuildingCountSeconds = -1.0;
		if (!bSinkWhenDepotFull)
		{
			TickSinkSubsystem = nullptr;
		}
	}

	if (RateMode != OldMode || bEnabled != bOldEnabled || bIncludeDeathCrates != bOldDeath || bUploadDeathCrateEquipment != bOldDeathEquipment
		|| bSinkWhenDepotFull != bOldSink)
	{
		// Different rules mean different timers (and idle crates may have something to do now); start every crate
		// from idle, like a freshly built uploader.
		ResetAllTimers();
	}
	UE_LOG(LogDimensionalDismantleCrate, Log, TEXT("Settings: %s"), *DescribeSettings());
}

FString ADimensionalDismantleCrateSubsystem::DescribeSettings() const
{
	return FString::Printf(TEXT("%s=%s, %s=%d (%s), %s=%s, %s=%s, %s=%s, %s=%s, %s=%s, %s=%s"),
		DDC::KeyEnabled, OnOff(bEnabled),
		DDC::KeyRateMode, static_cast<int32>(RateMode), DDC::RateModeName(RateMode),
		DDC::KeyIncludeDeathCrates, OnOff(bIncludeDeathCrates),
		DDC::KeyUploadDeathCrateEquipment, OnOff(bUploadDeathCrateEquipment),
		DDC::KeyOverflowToDepot, OnOff(bOverflowToDepot),
		DDC::KeyShowStuckNotices, OnOff(bShowStuckNotices),
		DDC::KeySinkWhenDepotFull, OnOff(bSinkWhenDepotFull),
		DDC::KeyRefundsFollowDepotPreference, OnOff(bRefundsFollowDepotPreference));
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
		TickSinkSubsystem = nullptr;
		return;
	}

	AFGCentralStorageSubsystem* CentralStorage = AFGCentralStorageSubsystem::Get(GetWorld());
	if (!CentralStorage)
	{
		return;
	}
	RefillDepotTokens();
	TickSinkSubsystem = (bSinkWhenDepotFull && SinksInWorld > 0) ? AFGResourceSinkSubsystem::Get(GetWorld()) : nullptr;

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
		case DDC::ERateMode::Shared: TickShared(DeltaSeconds, TimeToUpload, 1, CentralStorage); break;
		case DDC::ERateMode::PerUploader: TickShared(DeltaSeconds, TimeToUpload, FMath::Max(1, UploadersInWorld), CentralStorage); break;
		case DDC::ERateMode::PerSlot: TickPerSlot(DeltaSeconds, TimeToUpload, CentralStorage); break;
		case DDC::ERateMode::Instant: TickInstant(DeltaSeconds, CentralStorage); break;
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

int32 ADimensionalDismantleCrateSubsystem::RotationStart() const
{
	return Crates.Num() > 0 ? RotationCursor % Crates.Num() : 0;
}

void ADimensionalDismantleCrateSubsystem::NoteServed(const int32 Index)
{
	// Round robin: next tick starts after the last crate that moved an item. While the network budget is used up, the
	// crate that got the last item goes to the back of the line, so every crate takes its turn.
	RotationCursor = Crates.Num() > 0 ? (Index + 1) % Crates.Num() : 0;
}

ADimensionalDismantleCrateSubsystem::FCrateRules ADimensionalDismantleCrateSubsystem::RulesFor(const AFGCrate* Crate) const
{
	FCrateRules Rules;
	const bool bDeath = Crate && Crate->GetCrateType() == EFGCrateType::CT_DeathCrate;
	Rules.bSkipEquipment = bDeath && !bUploadDeathCrateEquipment;
	// Never from death crates: those are a player's belongings.
	Rules.bAllowSink = TickSinkSubsystem != nullptr && Crate && Crate->GetCrateType() == EFGCrateType::CT_DismantleCrate;
	return Rules;
}

void ADimensionalDismantleCrateSubsystem::TickPerCrate(const float DeltaSeconds, const float TimeToUpload, AFGCentralStorageSubsystem* CentralStorage)
{
	// An upload can destroy the crate (vanilla destroys empty crates); entries are pruned on the next tick.
	// Starts after the last crate that moved an item, so a used-up network budget is shared out over the crates.
	const int32 Num = Crates.Num();
	const int32 Start = RotationStart();
	for (int32 Step = 0; Step < Num; ++Step)
	{
		const int32 Index = (Start + Step) % Num;
		FTrackedCrate& Entry = Crates[Index];
		AFGCrate* Crate = Entry.Crate.Get();
		if (!IsEligible(Crate))
		{
			Entry.Timer.Reset();
			continue;
		}
		UFGInventoryComponent* Inventory = Crate->GetInventory();
		const FCrateRules Rules = RulesFor(Crate);
		if (Entry.Timer.Step(DeltaSeconds, TimeToUpload, !Inventory->IsEmpty(),
			[&] { return CanMoveAnySlot(Inventory, CentralStorage, Rules); },
			[&] { return TryMoveOneFromCrate(Inventory, CentralStorage, Rules); }))
		{
			NoteServed(Index);
		}
	}
}

void ADimensionalDismantleCrateSubsystem::TickShared(const float DeltaSeconds, const float TimeToUpload, const int32 Sources, AFGCentralStorageSubsystem* CentralStorage)
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

	// Sources uploaders together: one item every TimeToUpload / Sources seconds, and at most one per uploader per tick.
	SharedTimer.StepMulti(DeltaSeconds, TimeToUpload / Sources, Sources, bAnyItems,
		[&]
		{
			for (const int32 Index : Eligible)
			{
				const AFGCrate* Crate = Crates[Index].Crate.Get();
				if (IsEligible(Crate) && CanMoveAnySlot(Crate->GetInventory(), CentralStorage, RulesFor(Crate)))
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
			bool bThrottled = false;
			for (int32 Step = 0; Step < Count; ++Step)
			{
				const int32 Index = Eligible[(Start + Step) % Count];
				AFGCrate* Crate = Crates[Index].Crate.Get();
				if (!IsEligible(Crate))
				{
					continue;
				}
				const EMoveResult Result = TryMoveOneFromCrate(Crate->GetInventory(), CentralStorage, RulesFor(Crate));
				if (Result == EMoveResult::Moved)
				{
					SharedCursor = Index + 1;
					return EMoveResult::Moved;
				}
				bThrottled |= Result == EMoveResult::Throttled;
			}
			return bThrottled ? EMoveResult::Throttled : EMoveResult::Nothing;
		});
}

void ADimensionalDismantleCrateSubsystem::TickPerSlot(const float DeltaSeconds, const float TimeToUpload, AFGCentralStorageSubsystem* CentralStorage)
{
	const int32 Num = Crates.Num();
	const int32 Start = RotationStart();
	for (int32 Step = 0; Step < Num; ++Step)
	{
		const int32 Index = (Start + Step) % Num;
		FTrackedCrate& Entry = Crates[Index];
		AFGCrate* Crate = Entry.Crate.Get();
		if (!IsEligible(Crate))
		{
			Entry.SlotTimers.Reset();
			continue;
		}
		UFGInventoryComponent* Inventory = Crate->GetInventory();
		const FCrateRules Rules = RulesFor(Crate);
		// Merging crates can resize the inventory; new slots start idle.
		Entry.SlotTimers.SetNum(Inventory->GetSizeLinear());
		for (int32 Slot = 0; Slot < Entry.SlotTimers.Num(); ++Slot)
		{
			// Uploading the last item destroys the crate during the call, so stop as soon as that happens.
			if (!IsValid(Crate) || Crate->IsActorBeingDestroyed())
			{
				break;
			}
			if (Entry.SlotTimers[Slot].Step(DeltaSeconds, TimeToUpload, !Inventory->IsIndexEmpty(Slot),
				[&] { return CanMoveSlot(Inventory, Slot, CentralStorage, Rules); },
				[&] { return TryMoveSlot(Inventory, Slot, CentralStorage, Rules); }))
			{
				NoteServed(Index);
			}
		}
	}
}

void ADimensionalDismantleCrateSubsystem::TickInstant(const float DeltaSeconds, AFGCentralStorageSubsystem* CentralStorage)
{
	// Crates to work on this tick: eligible, holding items, and not waiting out an idle recheck.
	TArray<int32, TInlineAllocator<64>> Active;
	const int32 Num = Crates.Num();
	const int32 Start = RotationStart();
	for (int32 Step = 0; Step < Num; ++Step)
	{
		const int32 Index = (Start + Step) % Num;
		FTrackedCrate& Entry = Crates[Index];
		const AFGCrate* Crate = Entry.Crate.Get();
		if (!IsEligible(Crate))
		{
			Entry.Timer.Reset();
			continue;
		}
		if (Entry.Timer.ReadyForInstant(DeltaSeconds, !Crate->GetInventory()->IsEmpty()))
		{
			Active.Add(Index);
		}
	}

	// Share this tick's item cap over the active crates, pass after pass, until it is used up or no crate can move anything.
	int32 Budget = DDC::InstantMaxItemsPerTick;
	while (Active.Num() > 0 && Budget > 0)
	{
		const int32 Share = FMath::Max(1, Budget / Active.Num());
		for (int32 Position = 0; Position < Active.Num() && Budget > 0;)
		{
			FTrackedCrate& Entry = Crates[Active[Position]];
			AFGCrate* Crate = Entry.Crate.Get();
			bool bDone = !IsEligible(Crate);
			if (!bDone)
			{
				UFGInventoryComponent* Inventory = Crate->GetInventory();
				const FCrateRules Rules = RulesFor(Crate);
				for (int32 Moved = 0; Moved < Share && Budget > 0; ++Moved)
				{
					const EMoveResult Result = TryMoveOneFromCrate(Inventory, CentralStorage, Rules);
					if (Result == EMoveResult::Moved)
					{
						--Budget;
						NoteServed(Active[Position]);
						if (!IsValid(Crate) || Crate->IsActorBeingDestroyed())
						{
							bDone = true; // emptied; the game destroys empty crates
							break;
						}
						continue;
					}
					if (Result == EMoveResult::Nothing)
					{
						Entry.Timer.GoIdle();
					}
					// Nothing, or Throttled (the budget is used up; the crate stays ready and tries again next tick).
					bDone = true;
					break;
				}
			}
			if (bDone)
			{
				Active.RemoveAtSwap(Position);
			}
			else
			{
				++Position;
			}
		}
	}
}

bool ADimensionalDismantleCrateSubsystem::IsEquipmentOrStateful(const FInventoryStack& Stack)
{
	const UClass* ItemClass = Stack.Item.GetItemClass();
	return Stack.Item.HasState() || (ItemClass && ItemClass->IsChildOf(UFGEquipmentDescriptor::StaticClass()));
}

bool ADimensionalDismantleCrateSubsystem::IsDepotFullOf(const AFGCentralStorageSubsystem* CentralStorage, const TSubclassOf<UFGItemDescriptor> Item)
{
	return Item && CentralStorage->GetNumItemsFromCentralStorage(Item) >= CentralStorage->GetCentralStorageItemLimit(Item);
}

bool ADimensionalDismantleCrateSubsystem::CanSinkStack(const FInventoryStack& Stack, const AFGCentralStorageSubsystem* CentralStorage, const FCrateRules& Rules) const
{
	if (!Rules.bAllowSink || !TickSinkSubsystem || !Stack.HasItems() || IsEquipmentOrStateful(Stack))
	{
		return false;
	}
	const TSubclassOf<UFGItemDescriptor> Item = Stack.Item.GetItemClass();
	// Only what the depot is full of: anything the depot refuses for another reason stays in the crate.
	return IsDepotFullOf(CentralStorage, Item) && TickSinkSubsystem->GetResourceSinkPointsForItem(Item) > 0;
}

bool ADimensionalDismantleCrateSubsystem::CanMoveSlot(const UFGInventoryComponent* Inventory, const int32 SlotIndex, const AFGCentralStorageSubsystem* CentralStorage, const FCrateRules& Rules) const
{
	FInventoryStack Stack;
	if (!Inventory->GetStackFromIndex(SlotIndex, Stack) || !Stack.HasItems() || (Rules.bSkipEquipment && IsEquipmentOrStateful(Stack)))
	{
		return false;
	}
	return CentralStorage->CanUploadInventoryItemToCentralStorage(Stack.Item) || CanSinkStack(Stack, CentralStorage, Rules);
}

bool ADimensionalDismantleCrateSubsystem::CanMoveAnySlot(const UFGInventoryComponent* Inventory, const AFGCentralStorageSubsystem* CentralStorage, const FCrateRules& Rules) const
{
	const int32 Size = Inventory->GetSizeLinear();
	for (int32 Slot = 0; Slot < Size; ++Slot)
	{
		if (CanMoveSlot(Inventory, Slot, CentralStorage, Rules))
		{
			return true;
		}
	}
	return false;
}

EMoveResult ADimensionalDismantleCrateSubsystem::TryUploadSlot(UFGInventoryComponent* Inventory, const int32 SlotIndex, AFGCentralStorageSubsystem* CentralStorage, const FCrateRules& Rules)
{
	FInventoryStack Stack;
	if (!Inventory->GetStackFromIndex(SlotIndex, Stack) || !Stack.HasItems() || (Rules.bSkipEquipment && IsEquipmentOrStateful(Stack)))
	{
		return EMoveResult::Nothing;
	}
	if (!HasDepotToken())
	{
		return CentralStorage->CanUploadInventoryItemToCentralStorage(Stack.Item) ? EMoveResult::Throttled : EMoveResult::Nothing;
	}
	// UploadItemFromInventoryToCentralStorage checks the item and the depot limit itself, and moves exactly one item.
	if (!CentralStorage->UploadItemFromInventoryToCentralStorage(Inventory, SlotIndex))
	{
		return EMoveResult::Nothing;
	}
	SpendDepotToken();
	NoteUpload();
	return EMoveResult::Moved;
}

bool ADimensionalDismantleCrateSubsystem::TrySinkSlot(UFGInventoryComponent* Inventory, const int32 SlotIndex, const AFGCentralStorageSubsystem* CentralStorage, const FCrateRules& Rules)
{
	FInventoryStack Stack;
	if (!Inventory->GetStackFromIndex(SlotIndex, Stack) || !CanSinkStack(Stack, CentralStorage, Rules))
	{
		return false;
	}
	// Points first: if the sink refuses the item, it stays in the crate.
	if (!TickSinkSubsystem->AddPoints_ThreadSafe(Stack.Item.GetItemClass()))
	{
		return false;
	}
	Inventory->RemoveFromIndex(SlotIndex, 1);
	++ItemsSunk;
	return true;
}

EMoveResult ADimensionalDismantleCrateSubsystem::TryMoveSlot(UFGInventoryComponent* Inventory, const int32 SlotIndex, AFGCentralStorageSubsystem* CentralStorage, const FCrateRules& Rules)
{
	const EMoveResult Upload = TryUploadSlot(Inventory, SlotIndex, CentralStorage, Rules);
	if (Upload == EMoveResult::Moved)
	{
		return Upload;
	}
	return (Rules.bAllowSink && TrySinkSlot(Inventory, SlotIndex, CentralStorage, Rules)) ? EMoveResult::Moved : Upload;
}

EMoveResult ADimensionalDismantleCrateSubsystem::TryMoveOneFromCrate(UFGInventoryComponent* Inventory, AFGCentralStorageSubsystem* CentralStorage, const FCrateRules& Rules)
{
	// Uploads first, in the vanilla uploader's order: the first slot that accepts an item wins.
	const int32 Size = Inventory->GetSizeLinear();
	bool bThrottled = false;
	if (HasDepotToken())
	{
		for (int32 Slot = 0; Slot < Size; ++Slot)
		{
			const EMoveResult Result = TryUploadSlot(Inventory, Slot, CentralStorage, Rules);
			if (Result == EMoveResult::Moved)
			{
				return Result;
			}
			bThrottled |= Result == EMoveResult::Throttled;
		}
	}
	else
	{
		// Budget used up: don't scan for uploads. If the crate has nothing to upload, it learns that once the budget is back.
		bThrottled = true;
	}
	// Then the sink, for items the depot is full of. Sinking sends nothing to the depot, so it doesn't use the budget.
	if (Rules.bAllowSink)
	{
		for (int32 Slot = 0; Slot < Size; ++Slot)
		{
			if (TrySinkSlot(Inventory, Slot, CentralStorage, Rules))
			{
				return EMoveResult::Moved;
			}
		}
	}
	return bThrottled ? EMoveResult::Throttled : EMoveResult::Nothing;
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
// Dismantle refunds straight to the depot (overflow, and depot first)

UFGInventoryComponent* ADimensionalDismantleCrateSubsystem::GetScratchInventory()
{
	if (!ScratchInventory)
	{
		// Never replicated or saved (this subsystem is neither), and always emptied again before UploadStackDirect returns.
		ScratchInventory = NewObject<UFGInventoryComponent>(this, TEXT("DDCOverflowScratch"));
		ScratchInventory->SetIsReplicated(false);
		ScratchInventory->RegisterComponent();
		ScratchInventory->Resize(1);
	}
	return ScratchInventory;
}

int32 ADimensionalDismantleCrateSubsystem::UploadStackDirect(const FInventoryStack& Stack, AFGCentralStorageSubsystem* CentralStorage, FInventoryStack& OutLeft, bool& bOutStoppedByBudget)
{
	OutLeft = Stack;
	bOutStoppedByBudget = false;
	UFGInventoryComponent* Scratch = GetScratchInventory();
	if (!Stack.HasItems() || !Scratch || !CentralStorage->CanUploadInventoryItemToCentralStorage(Stack.Item))
	{
		return 0;
	}
	if (!HasDepotToken())
	{
		bOutStoppedByBudget = true;
		return 0;
	}
	// The game only uploads from an inventory slot. One slot sized to the whole stack, because refund stacks can be
	// larger than a normal stack; the item (and any state it carries) is uploaded exactly as from a crate.
	Scratch->Empty();
	Scratch->AddArbitrarySlotSize(0, Stack.NumItems);
	if (Scratch->AddStackToIndex(0, Stack, true) != Stack.NumItems)
	{
		// Not expected; the stack stays where it was going.
		Scratch->Empty();
		return 0;
	}
	int32 Sent = 0;
	while (!Scratch->IsIndexEmpty(0))
	{
		if (!HasDepotToken())
		{
			bOutStoppedByBudget = true;
			break;
		}
		if (!CentralStorage->UploadItemFromInventoryToCentralStorage(Scratch, 0))
		{
			break; // the depot reached its limit for this item
		}
		SpendDepotToken();
		++Sent;
	}
	FInventoryStack Left;
	OutLeft = (Scratch->GetStackFromIndex(0, Left) && Left.HasItems()) ? Left : FInventoryStack();
	Scratch->Empty();
	return Sent;
}

bool ADimensionalDismantleCrateSubsystem::SendOverflowToDepot(const TArray<FInventoryStack>& Refund, TArray<FInventoryStack>& OutRemaining)
{
	OutRemaining.Reset();
	if (!HasAuthority() || !bOverflowToDepot || Refund.Num() == 0 || !IsGateOpen())
	{
		return false;
	}
	AFGCentralStorageSubsystem* CentralStorage = AFGCentralStorageSubsystem::Get(GetWorld());
	if (!CentralStorage || !GetScratchInventory())
	{
		return false;
	}

	RefillDepotTokens();
	int32 Sent = 0;
	int32 Kept = 0;
	for (const FInventoryStack& Stack : Refund)
	{
		if (!Stack.HasItems())
		{
			continue;
		}
		FInventoryStack Left;
		bool bStoppedByBudget = false;
		Sent += UploadStackDirect(Stack, CentralStorage, Left, bStoppedByBudget);
		if (Left.HasItems())
		{
			// The depot won't take it (at its limit, or not an item the depot stores), or the budget is used up: it
			// goes into the crate. Held back by the budget, it is sent from there at the budget's rate.
			OutRemaining.Add(Left);
			Kept += Left.NumItems;
			if (bStoppedByBudget)
			{
				OverflowOwed.FindOrAdd(Left.Item.GetItemClass()) += Left.NumItems;
			}
		}
	}

	ItemsSentDirectly += Sent;
	UE_LOG(LogDimensionalDismantleCrate, Log, TEXT("Dismantle overflow: sent %d item(s) straight to the depot, %d item(s) go into a crate"), Sent, Kept);
	return true;
}

const TCHAR* ADimensionalDismantleCrateSubsystem::WhyNotRefundToDepotFirst(const AFGPlayerState* PlayerState)
{
	if (!HasAuthority())
	{
		return TEXT("not the server");
	}
	if (!bRefundsFollowDepotPreference)
	{
		return TEXT("the option is off");
	}
	if (!PlayerState)
	{
		return TEXT("no player state");
	}
	// The player's own vanilla setting: "take from inventory before depot" off means the depot comes first for them.
	if (PlayerState->GetTakeFromInventoryBeforeCentralStorage())
	{
		return TEXT("the player takes from the inventory before the depot");
	}
	if (!IsGateOpen())
	{
		return TEXT("the depot research or an uploader is missing");
	}
	return nullptr;
}

int32 ADimensionalDismantleCrateSubsystem::SendRefundStackToDepot(const FInventoryStack& Stack, FInventoryStack& OutLeft)
{
	OutLeft = Stack;
	AFGCentralStorageSubsystem* CentralStorage = AFGCentralStorageSubsystem::Get(GetWorld());
	if (!Stack.HasItems() || !CentralStorage)
	{
		return 0;
	}
	RefillDepotTokens();
	// What the budget holds back goes to the inventory, like anything else the depot doesn't take.
	bool bStoppedByBudget = false;
	const int32 Sent = UploadStackDirect(Stack, CentralStorage, OutLeft, bStoppedByBudget);
	ItemsRefundedToDepot += Sent;
	if (Sent > 0)
	{
		UE_LOG(LogDimensionalDismantleCrate, Verbose, TEXT("Depot-first refund: sent %d %s to the depot, %d go to the inventory%s"), Sent,
			*GetNameSafe(Stack.Item.GetItemClass()), OutLeft.NumItems, bStoppedByBudget ? TEXT(" (network budget used up)") : TEXT(""));
	}
	return Sent;
}

bool ADimensionalDismantleCrateSubsystem::HasRemoteClients() const
{
	const UWorld* World = GetWorld();
	const UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
	return Driver && Driver->ClientConnections.Num() > 0;
}

void ADimensionalDismantleCrateSubsystem::RefillDepotTokens()
{
	const bool bNetworked = HasRemoteClients();
	float Rate = bNetworked ? DDC::DepotAddsPerSecondNetworked : DDC::DepotAddsPerSecondLocal;
	float Burst = bNetworked ? DDC::DepotAddsBurstNetworked : DDC::DepotAddsBurstLocal;
#if DDC_DEV_COMMANDS
	Rate = DevBudgetRate > 0.0f ? DevBudgetRate : Rate;
	Burst = DevBudgetBurst > 0.0f ? DevBudgetBurst : Burst;
#endif
	const double Now = FPlatformTime::Seconds();
	const float Refilled = DepotTokensRefilledAt < 0.0 ? Burst : DepotTokens + static_cast<float>((Now - DepotTokensRefilledAt) * Rate);
	DepotTokens = FMath::Min(Burst, Refilled);
	DepotTokensRefilledAt = Now;
}

void ADimensionalDismantleCrateSubsystem::TickOverflowFromCrates(AFGCentralStorageSubsystem* CentralStorage)
{
	// Tokens were refilled at the start of this tick.
	for (auto It = OverflowOwed.CreateIterator(); It && HasDepotToken(); ++It)
	{
		const TSubclassOf<UFGItemDescriptor> Item = It.Key();
		int32& Owed = It.Value();
		bool bDepotRefused = false;
		// Newest crates first: a crate made for the overflow was tracked last. Overflow merged into an older crate is
		// found there too.
		for (int32 Index = Crates.Num() - 1; Index >= 0 && Owed > 0 && HasDepotToken() && !bDepotRefused; --Index)
		{
			AFGCrate* Crate = Crates[Index].Crate.Get();
			UFGInventoryComponent* Inventory = IsValid(Crate) && !Crate->IsActorBeingDestroyed() && Crate->HasActorBegunPlay()
				&& Crate->GetCrateType() == EFGCrateType::CT_DismantleCrate ? Crate->GetInventory() : nullptr;
			if (!Inventory)
			{
				continue;
			}
			const int32 Size = Inventory->GetSizeLinear();
			for (int32 Slot = 0; Slot < Size && Owed > 0 && HasDepotToken(); ++Slot)
			{
				FInventoryStack Stack;
				if (!Inventory->GetStackFromIndex(Slot, Stack) || !Stack.HasItems() || Stack.Item.GetItemClass() != Item)
				{
					continue;
				}
				while (Owed > 0 && HasDepotToken() && !Inventory->IsIndexEmpty(Slot))
				{
					if (!CentralStorage->UploadItemFromInventoryToCentralStorage(Inventory, Slot))
					{
						bDepotRefused = true;
						break;
					}
					--Owed;
					SpendDepotToken();
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
		// Still owed with budget to spare: the depot is full of the item, or no crate holds it any more (a player took
		// it). Either way the rest is no longer overflow; anything still in a crate uploads at normal crate speed.
		if (bDepotRefused || (Owed > 0 && HasDepotToken()))
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
// Stuck-crate notices

void ADimensionalDismantleCrateSubsystem::ScanForNotices()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World || !bEnabled || !bShowStuckNotices)
	{
		return;
	}
	if (World->GetNumPlayerControllers() == 0)
	{
		// Nobody to tell; keep the state, so whatever is stuck is announced to the next player who joins.
		return;
	}
	const double Now = World->GetTimeSeconds();
	const bool bGapPassed = LastNoticeSeconds < 0.0 || Now - LastNoticeSeconds >= DDC::NoticeMinGapSeconds;

	bool bAnyItems = false;
	for (const FTrackedCrate& Entry : Crates)
	{
		const AFGCrate* Crate = Entry.Crate.Get();
		if (IsEligible(Crate) && !Crate->GetInventory()->IsEmpty())
		{
			bAnyItems = true;
			break;
		}
	}

	if (!IsGateOpen())
	{
		BlockedItems.Reset();
		PendingNotices.Reset();
		// Before the research nobody expects uploading, so only the missing uploader is worth a word.
		if (bAnyItems && UploadersInWorld == 0 && IsResearchDone() && !bGateNoticeSent && bGapPassed
			&& (LastGateNoticeSeconds < 0.0 || Now - LastGateNoticeSeconds >= DDC::NoticeItemRepeatSeconds))
		{
			SendNotice(TEXT("dismantle crates start uploading to the Dimensional Depot once a Dimensional Depot Uploader is built."));
			bGateNoticeSent = true;
			LastGateNoticeSeconds = Now;
		}
		return;
	}
	bGateNoticeSent = false;

	const AFGCentralStorageSubsystem* CentralStorage = AFGCentralStorageSubsystem::Get(World);
	if (!CentralStorage)
	{
		return;
	}
	// Items that can't go anywhere: not uploadable now and not sinkable. Equipment a death crate keeps by the player's
	// own setting is not stuck, so it is left out.
	TMap<TSubclassOf<UFGItemDescriptor>, FStuckItem> Stuck;
	for (const FTrackedCrate& Entry : Crates)
	{
		const AFGCrate* Crate = Entry.Crate.Get();
		if (!IsEligible(Crate))
		{
			continue;
		}
		const UFGInventoryComponent* Inventory = Crate->GetInventory();
		const FCrateRules Rules = RulesFor(Crate);
		const int32 Size = Inventory->GetSizeLinear();
		for (int32 Slot = 0; Slot < Size; ++Slot)
		{
			FInventoryStack Stack;
			if (!Inventory->GetStackFromIndex(Slot, Stack) || !Stack.HasItems() || (Rules.bSkipEquipment && IsEquipmentOrStateful(Stack))
				|| CentralStorage->CanUploadInventoryItemToCentralStorage(Stack.Item) || CanSinkStack(Stack, CentralStorage, Rules))
			{
				continue;
			}
			const TSubclassOf<UFGItemDescriptor> Item = Stack.Item.GetItemClass();
			FStuckItem& StuckEntry = Stuck.FindOrAdd(Item);
			StuckEntry.Count += Stack.NumItems;
			StuckEntry.bDepotFull = IsDepotFullOf(CentralStorage, Item);
		}
	}

	// Items that got moving again are forgotten, so they are announced again if they get stuck later (at most every
	// NoticeItemRepeatSeconds per item).
	for (auto It = BlockedItems.CreateIterator(); It; ++It)
	{
		if (!Stuck.Contains(It.Key()))
		{
			PendingNotices.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
	for (const TPair<TSubclassOf<UFGItemDescriptor>, FStuckItem>& Pair : Stuck)
	{
		if (!BlockedItems.Contains(Pair.Key))
		{
			BlockedItems.Add(Pair.Key, Now);
			const double* Last = LastAnnounced.Find(Pair.Key);
			if (!Last || Now - *Last >= DDC::NoticeItemRepeatSeconds)
			{
				PendingNotices.Add(Pair.Key, Pair.Value);
			}
		}
		else if (FStuckItem* Pending = PendingNotices.Find(Pair.Key))
		{
			*Pending = Pair.Value;
		}
	}
	if (PendingNotices.Num() == 0 || !bGapPassed)
	{
		return;
	}

	TArray<FString> Full;
	TArray<FString> Refused;
	for (const TPair<TSubclassOf<UFGItemDescriptor>, FStuckItem>& Pair : PendingNotices)
	{
		const FString Name = FString::Printf(TEXT("%s (%d)"), *UFGItemDescriptor::GetItemName(Pair.Key).ToString(), Pair.Value.Count);
		(Pair.Value.bDepotFull ? Full : Refused).Add(Name);
		LastAnnounced.Add(Pair.Key, Now);
	}
	PendingNotices.Reset();
	FString Text;
	if (Full.Num() > 0)
	{
		Text = FString::Printf(TEXT("the Dimensional Depot is full of %s; %s in crates until it has room."), *JoinItems(Full),
			Full.Num() == 1 ? TEXT("it stays") : TEXT("they stay"));
	}
	if (Refused.Num() > 0)
	{
		Text += FString::Printf(TEXT("%sthe Dimensional Depot doesn't accept %s; %s in crates."), Text.IsEmpty() ? TEXT("") : TEXT(" Also, "),
			*JoinItems(Refused), Refused.Num() == 1 ? TEXT("it stays") : TEXT("they stay"));
	}
	SendNotice(Text);
}

void ADimensionalDismantleCrateSubsystem::SendNotice(const FString& Text)
{
	UWorld* World = GetWorld();
	AFGChatManager* Chat = World ? AFGChatManager::Get(World) : nullptr;
	if (!Chat)
	{
		return;
	}
	// A system message through the game's own chat, which reaches every player, with or without this mod.
	FChatMessageStruct Message;
	Message.MessageType = EFGChatMessageType::CMT_SystemMessage;
	Message.MessageText = FText::FromString(FString::Printf(TEXT("Dimensional Dismantle Crate: %s"), *Text));
	Chat->BroadcastChatMessage(Message, nullptr);
	LastNoticeSeconds = World->GetTimeSeconds();
	++NoticesSent;
	LastNoticeText = Message.MessageText.ToString();
	UE_LOG(LogDimensionalDismantleCrate, Log, TEXT("Notice: %s"), *LastNoticeText);
}

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
		OutMessage = FString::Printf(TEXT("Unknown key '%s' (or the configuration is not registered). Valid keys: %s, %s, %s, %s, %s, %s, %s, %s"),
			*Key, DDC::KeyEnabled, DDC::KeyRateMode, DDC::KeyIncludeDeathCrates, DDC::KeyUploadDeathCrateEquipment, DDC::KeyOverflowToDepot,
			DDC::KeyShowStuckNotices, DDC::KeySinkWhenDepotFull, DDC::KeyRefundsFollowDepotPreference);
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
		OutMessage = FString::Printf(TEXT("%s set to %s."), *CanonicalKey, OnOff(bValue));
		return true;
	}
	if (UConfigPropertyInteger* Integer = Cast<UConfigPropertyInteger>(Property))
	{
		int32 Value = 0;
		if (!ParseRateMode(ValueText, Value))
		{
			OutMessage = FString::Printf(TEXT("Refused: %s takes 0 (PerCrate), 1 (Shared), 2 (PerSlot), 3 (Instant) or 4 (PerUploader)."), *CanonicalKey);
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
	Lines.Add(FString::Printf(TEXT("  Network budget: %s (%.0f items/s, burst %.0f), %.1f available"), HasRemoteClients() ? TEXT("players connected") : TEXT("no remote players"),
		HasRemoteClients() ? DDC::DepotAddsPerSecondNetworked : DDC::DepotAddsPerSecondLocal,
		HasRemoteClients() ? DDC::DepotAddsBurstNetworked : DDC::DepotAddsBurstLocal, DepotTokens));
	if (bSinkWhenDepotFull)
	{
		Lines.Add(FString::Printf(TEXT("  Sinking: %d AWESOME Sink(s) built -> %s; %lld item(s) sunk since the world started"), CountSinksInWorld(),
			CountSinksInWorld() > 0 ? TEXT("on") : TEXT("waiting for a sink"), ItemsSunk));
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
		OnOff(bOverflowToDepot), ItemsSentDirectly, OverflowWaiting));
	Lines.Add(FString::Printf(TEXT("  Dismantle refunds to the depot first (players whose 'take from inventory before depot' is off): %s, %lld item(s) since the world started"),
		OnOff(bRefundsFollowDepotPreference), ItemsRefundedToDepot));
	if (World)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* Controller = It->Get();
			const AFGPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AFGPlayerState>() : nullptr;
			if (PlayerState)
			{
				Lines.Add(FString::Printf(TEXT("    %s: take from inventory before depot = %s"), *PlayerState->GetPlayerName(),
					OnOff(PlayerState->GetTakeFromInventoryBeforeCentralStorage())));
			}
		}
	}
	Lines.Add(FString::Printf(TEXT("  Stuck-crate notices: %s, %d sent, %d item type(s) stuck now"), OnOff(bShowStuckNotices), NoticesSent, BlockedItems.Num()));

	for (const FString& Line : Lines)
	{
		Ar.Log(Line);
		UE_LOG(LogDimensionalDismantleCrate, Log, TEXT("%s"), *Line);
	}
}
