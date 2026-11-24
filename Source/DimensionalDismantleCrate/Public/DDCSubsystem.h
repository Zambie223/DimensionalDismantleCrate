#pragma once

#include "CoreMinimal.h"
#include "DDCConstants.h"
#include "DDCUploadTimer.h"
#include "Subsystem/ModSubsystem.h"
#include "UObject/ObjectKey.h"
#include "DDCSubsystem.generated.h"

class AFGCentralStorageSubsystem;
class AFGCrate;
class AFGPlayerState;
class AFGResourceSinkSubsystem;
struct FInventoryStack;
class UConfigPropertySection;
class UFGInventoryComponent;
class UFGItemDescriptor;
class UFGSchematic;

/**
 * Uploads the contents of dismantle crates (and optionally death crates) to the Dimensional Depot, at the speed of a
 * Dimensional Depot Uploader (or one of the other rate modes), whenever the depot has room. Optional extras:
 * - dismantle overflow (the refund that doesn't fit the player's inventory) straight to the depot (DDCRefundHook.cpp)
 * - dismantle refunds to the depot before the inventory, for players whose own depot setting says so (DDCRefundHook.cpp)
 * - items the depot is full of go to the AWESOME Sink instead of staying in the crate
 * - chat notices that say why crates aren't emptying
 * Every item added to the depot shares one network budget (DDCConstants.h).
 *
 * Runs only on the server (or in single player / on a listen host) and is never replicated: clients don't need the
 * mod, because crate contents and depot counts already replicate through the game's own objects.
 * Uploads use only public game API: AFGCentralStorageSubsystem::UploadItemFromInventoryToCentralStorage moves one
 * item and refuses it when the depot is at its limit, exactly as the uploader uses it.
 */
UCLASS()
class DIMENSIONALDISMANTLECRATE_API ADimensionalDismantleCrateSubsystem : public AModSubsystem
{
	GENERATED_BODY()

public:
	ADimensionalDismantleCrateSubsystem();

	/** Returns the subsystem in this world, or null (main menu, or on a client, where it never exists). */
	static ADimensionalDismantleCrateSubsystem* Get(UWorld* World);

	/** Returns this mod's live configuration root section, or null if the configuration is not registered. */
	static UConfigPropertySection* GetConfigRootSection(const UWorld* World);

	/** Sets one config value on this machine and saves it; the change handler applies it. Refused on clients. */
	static bool SetConfigValue(const UWorld* World, const FString& Key, const FString& ValueText, FString& OutMessage);

	/** Writes the configuration, gate, upload speed and crate statistics to the output device and the log. */
	void DescribeStatus(FOutputDevice& Ar) const;

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Dismantle overflow (the refund that didn't fit the player's inventory), called by the refund hook on the server
	 * before the game puts it into a crate. Sends what it can straight to the depot and returns what is left for the
	 * crate in OutRemaining. Returns false if the option is off or the gate is closed (the refund is then untouched).
	 */
	bool SendOverflowToDepot(const TArray<FInventoryStack>& Refund, TArray<FInventoryStack>& OutRemaining);

	/**
	 * True if a dismantle by this player should refund to the depot before the inventory: the host option is on, the
	 * gate is open, and the player's own vanilla "take from inventory before depot" setting is off.
	 */
	bool ShouldRefundToDepotFirst(const AFGPlayerState* PlayerState) { return WhyNotRefundToDepotFirst(PlayerState) == nullptr; }

	/** Null if a dismantle by this player refunds to the depot first; otherwise the reason it doesn't. */
	const TCHAR* WhyNotRefundToDepotFirst(const AFGPlayerState* PlayerState);

	/**
	 * Depot-first refunds, called by the refund hook on the server for each refund stack the game is about to put into
	 * the player's inventory. Uploads what the depot takes now (within the network budget) and returns that many;
	 * OutLeft is what still goes to the inventory.
	 */
	int32 SendRefundStackToDepot(const FInventoryStack& Stack, FInventoryStack& OutLeft);

#if DDC_DEV_COMMANDS
	/** DEV: true if uploading is allowed right now (Dimensional Depot research done and at least one uploader built). */
	bool IsUploadingAllowed() { return IsGateOpen(); }
	/** DEV: average and worst game-thread cost of this subsystem's Tick since the last reset, in microseconds. */
	void ResetTickStats() { TickSecondsTotal = 0.0; TickSecondsMax = 0.0; TickCount = 0; }
	double GetAverageTickMicroseconds() const { return TickCount > 0 ? TickSecondsTotal * 1.0e6 / TickCount : 0.0; }
	double GetMaxTickMicroseconds() const { return TickSecondsMax * 1.0e6; }
	int64 GetTickCount() const { return TickCount; }
	int32 GetTrackedCrateCount() const { return Crates.Num(); }
	/** DEV: overrides the depot budget's rate and burst (0 restores the defaults), so the test can exercise the limits. */
	void SetDepotBudgetOverride(float ItemsPerSecond, float Burst) { DevBudgetRate = ItemsPerSecond; DevBudgetBurst = Burst; DepotTokens = 0.0f; DepotTokensRefilledAt = -1.0; }
	int64 GetItemsSentDirectly() const { return ItemsSentDirectly; }
	int64 GetItemsUploaded() const { return ItemsUploaded; }
	int64 GetItemsSunk() const { return ItemsSunk; }
	int64 GetItemsRefundedToDepot() const { return ItemsRefundedToDepot; }
	int32 GetUploadersInWorld() const { return UploadersInWorld; }
	int32 GetSinksInWorld() const { return SinksInWorld; }
	int32 GetNoticesSent() const { return NoticesSent; }
	const FString& GetLastNotice() const { return LastNoticeText; }
	bool IsBlockedForNotice(TSubclassOf<UFGItemDescriptor> Item) const { return BlockedItems.Contains(Item); }
	/** DEV: runs the stuck-crate scan now instead of waiting for its timer. */
	void RunNoticeScanNow() { ScanForNotices(); }
	/** DEV: pretends this many uploaders are built (-1 counts them again), so the test can close the gate. */
	void SetUploaderCountOverride(int32 Count) { DevUploadersOverride = Count; LastBuildingCountSeconds = -1.0; if (Count >= 0) { UploadersInWorld = Count; } }
	/** DEV: lifts the 30 s gap after the last notice, so the next notice can be tested right away. */
	void ResetNoticeGapForTest() { LastNoticeSeconds = -1.0; }
	int32 GetOverflowOwedItems() const;
#endif

protected:
	virtual void Init() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FTrackedCrate
	{
		TWeakObjectPtr<AFGCrate> Crate;
		/** Identity for TrackedKeys; stays usable after the crate is gone, so the entry can be removed from the set. */
		TObjectKey<AFGCrate> Key;
		/** Per crate mode. */
		DDC::FUploadTimer Timer;
		/** Per stack slot mode, one per inventory slot. */
		TArray<DDC::FUploadTimer> SlotTimers;
	};

	// Crate tracking
	void TrackCrate(AFGCrate* Crate);
	void OnActorSpawned(AActor* Actor);
	void Rescan();
	void PruneInvalidCrates();
	bool IsEligible(const AFGCrate* Crate) const;

	// Gate and settings
	bool IsGateOpen();
	/** Looks the depot research up until it is found (it can't be undone in normal play). */
	bool IsResearchDone();
	/** Refreshes the uploader count (and the sink count while sinking is on) at most once a second. */
	void RefreshBuildingCounts();
	int32 CountUploadersInWorld() const;
	int32 CountSinksInWorld() const;
	void ReadConfig();
	FString DescribeSettings() const;
	void BindConfig();
	void ResetAllTimers();

	UFUNCTION()
	void OnConfigValueChanged();
	void OnDebounceElapsed();

	// Uploading
	/** What may happen to one crate's items this tick. */
	struct FCrateRules
	{
		/** Equipment and items with state stay (death crates, unless the option is on). */
		bool bSkipEquipment = false;
		/** Items the depot is full of may go to the AWESOME Sink. */
		bool bAllowSink = false;
	};
	FCrateRules RulesFor(const AFGCrate* Crate) const;

	void TickPerCrate(float DeltaSeconds, float TimeToUpload, AFGCentralStorageSubsystem* CentralStorage);
	/** Shared and PerUploader: all crates take turns on one timer, Sources items per TimeToUpload, at most Sources per tick. */
	void TickShared(float DeltaSeconds, float TimeToUpload, int32 Sources, AFGCentralStorageSubsystem* CentralStorage);
	void TickPerSlot(float DeltaSeconds, float TimeToUpload, AFGCentralStorageSubsystem* CentralStorage);
	void TickInstant(float DeltaSeconds, AFGCentralStorageSubsystem* CentralStorage);
	/** Index of the crate to start with this tick: the one after the last crate that moved an item (round robin). */
	int32 RotationStart() const;
	/** Moves the round-robin start to just after this crate, which has just moved an item. */
	void NoteServed(int32 Index);

	/** Equipment, or an item carrying state (fuel, ammo, ...), which the depot would not keep. */
	static bool IsEquipmentOrStateful(const FInventoryStack& Stack);
	/** True if the depot holds this item at its limit. */
	static bool IsDepotFullOf(const AFGCentralStorageSubsystem* CentralStorage, TSubclassOf<UFGItemDescriptor> Item);
	/** True if this stack may go to the AWESOME Sink: sinking allowed, the depot is full of it, and it is worth points. */
	bool CanSinkStack(const FInventoryStack& Stack, const AFGCentralStorageSubsystem* CentralStorage, const FCrateRules& Rules) const;
	bool CanMoveAnySlot(const UFGInventoryComponent* Inventory, const AFGCentralStorageSubsystem* CentralStorage, const FCrateRules& Rules) const;
	bool CanMoveSlot(const UFGInventoryComponent* Inventory, int32 SlotIndex, const AFGCentralStorageSubsystem* CentralStorage, const FCrateRules& Rules) const;
	/** Uploads one item from the first slot that can upload, else sinks one. The crate may be destroyed when this returns Moved. */
	DDC::EMoveResult TryMoveOneFromCrate(UFGInventoryComponent* Inventory, AFGCentralStorageSubsystem* CentralStorage, const FCrateRules& Rules);
	/** Uploads one item from this slot, or sinks it if that's allowed. */
	DDC::EMoveResult TryMoveSlot(UFGInventoryComponent* Inventory, int32 SlotIndex, AFGCentralStorageSubsystem* CentralStorage, const FCrateRules& Rules);
	DDC::EMoveResult TryUploadSlot(UFGInventoryComponent* Inventory, int32 SlotIndex, AFGCentralStorageSubsystem* CentralStorage, const FCrateRules& Rules);
	bool TrySinkSlot(UFGInventoryComponent* Inventory, int32 SlotIndex, const AFGCentralStorageSubsystem* CentralStorage, const FCrateRules& Rules);
	void NoteUpload();

	// Network budget for depot additions
	/** True if the network budget allows one more item into the depot now. */
	bool HasDepotToken() const { return DepotTokens >= 1.0f; }
	void SpendDepotToken() { DepotTokens -= 1.0f; }

	// Stuck-crate notices
	void ScanForNotices();
	void SendNotice(const FString& Text);

	// Dismantle overflow straight to the depot
	/** True if there is a network connection to at least one player, so every upload is a network message. */
	bool HasRemoteClients() const;
	/** Adds the time since the last refill's share of the budget rate to DepotTokens. */
	void RefillDepotTokens();
	/** Sends overflow items that went into crates (because the rate was used up) from those crates, newest first. */
	void TickOverflowFromCrates(AFGCentralStorageSubsystem* CentralStorage);
	/** One-slot inventory the refund passes through, since the game only uploads from an inventory slot. */
	UFGInventoryComponent* GetScratchInventory();
	/**
	 * Uploads as much of Stack as the depot and the budget allow, through the scratch inventory. Returns the number
	 * sent; OutLeft is the rest (empty if all went), and bOutStoppedByBudget says whether the budget stopped it.
	 */
	int32 UploadStackDirect(const FInventoryStack& Stack, AFGCentralStorageSubsystem* CentralStorage, FInventoryStack& OutLeft, bool& bOutStoppedByBudget);

	TArray<FTrackedCrate> Crates;
	/** The crates in Crates, for constant-time duplicate checks when crates are found again by spawn events or rescans. */
	TSet<TObjectKey<AFGCrate>> TrackedKeys;

	/** Crates seen while the upload loop runs; added to Crates after it. */
	TArray<TWeakObjectPtr<AFGCrate>> PendingCrates;
	bool bTicking = false;

	/** Shared modes: one timer for all crates, and the crate to try first next time (round robin). */
	DDC::FUploadTimer SharedTimer;
	int32 SharedCursor = 0;
	/** Other modes: the crate to start with, moved on every tick so a used-up budget doesn't favour the same crates. */
	int32 RotationCursor = 0;

	// Settings in effect (from this machine's Mod Options; only used on the server)
	bool bEnabled = true;
	DDC::ERateMode RateMode = DDC::ERateMode::PerCrate;
	bool bIncludeDeathCrates = false;
	bool bUploadDeathCrateEquipment = false;
	bool bOverflowToDepot = false;
	bool bShowStuckNotices = false;
	bool bSinkWhenDepotFull = false;
	bool bRefundsFollowDepotPreference = false;

	/**
	 * Overflow items per item class that should have gone straight to the depot but went into a crate because the
	 * rate was used up. They are sent from dismantle crates at the overflow rate. Not saved: after a reload the
	 * rest uploads at normal crate speed.
	 */
	TMap<TSubclassOf<UFGItemDescriptor>, int32> OverflowOwed;
	/** Network budget for depot additions (DDCConstants.h), shared by crate uploads, overflow and depot-first refunds. */
	float DepotTokens = 0.0f;
	double DepotTokensRefilledAt = -1.0;
	UPROPERTY(Transient)
	TObjectPtr<UFGInventoryComponent> ScratchInventory;

	// Gate
	UPROPERTY(Transient)
	TSubclassOf<UFGSchematic> DepotResearchClass;
	bool bResearchDone = false;
	bool bLastGateOpen = false;
	/** Uploader buildings (and AWESOME Sinks, while sinking is on) in the world, refreshed at most once a second. */
	int32 UploadersInWorld = 0;
	int32 SinksInWorld = 0;
	double LastBuildingCountSeconds = -1.0;
	/** The resource sink subsystem, looked up at the start of each tick while sinking is on. */
	UPROPERTY(Transient)
	TObjectPtr<AFGResourceSinkSubsystem> TickSinkSubsystem;

	// Statistics (since this world started)
	int64 ItemsUploaded = 0;
	double FirstUploadSeconds = -1.0;
	double LastUploadSeconds = -1.0;
	/** Dismantle overflow items sent to the depot, directly or from a crate at the overflow rate (not in ItemsUploaded). */
	int64 ItemsSentDirectly = 0;
	/** Items from crates that went to the AWESOME Sink. */
	int64 ItemsSunk = 0;
	/** Dismantle refunds sent to the depot before the inventory. */
	int64 ItemsRefundedToDepot = 0;

	// Stuck-crate notices
	/** Items currently stuck in crates, with the time each became stuck. */
	TMap<TSubclassOf<UFGItemDescriptor>, double> BlockedItems;
	/** When each item was last named in a notice. */
	TMap<TSubclassOf<UFGItemDescriptor>, double> LastAnnounced;
	struct FStuckItem
	{
		int32 Count = 0;
		bool bDepotFull = false;
	};
	/** Newly stuck items waiting for the next notice. */
	TMap<TSubclassOf<UFGItemDescriptor>, FStuckItem> PendingNotices;
	double LastNoticeSeconds = -1.0;
	double LastGateNoticeSeconds = -1.0;
	bool bGateNoticeSent = false;
	int32 NoticesSent = 0;
	FString LastNoticeText;

#if DDC_DEV_COMMANDS
	float DevBudgetRate = 0.0f;
	float DevBudgetBurst = 0.0f;
	int32 DevUploadersOverride = -1;
	double TickSecondsTotal = 0.0;
	double TickSecondsMax = 0.0;
	int64 TickCount = 0;
#endif

	FDelegateHandle ActorSpawnedHandle;
	TWeakObjectPtr<UConfigPropertySection> BoundConfigSection;
	FTimerHandle RescanTimer;
	FTimerHandle DebounceTimer;
	FTimerHandle NoticeTimer;
};
