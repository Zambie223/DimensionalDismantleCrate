#pragma once

#include "CoreMinimal.h"
#include "DDCConstants.h"
#include "DDCUploadTimer.h"
#include "Subsystem/ModSubsystem.h"
#include "UObject/ObjectKey.h"
#include "DDCSubsystem.generated.h"

class AFGCentralStorageSubsystem;
class AFGCrate;
struct FInventoryStack;
class UConfigPropertySection;
class UFGInventoryComponent;
class UFGSchematic;

/**
 * Uploads the contents of dismantle crates (and optionally death crates) to the Dimensional Depot, at the speed of a
 * Dimensional Depot Uploader, whenever the depot has room.
 *
 * Runs only on the server (or in single player / on a listen host) and is never replicated: clients don't need the
 * mod, because crate contents and depot counts already replicate through the game's own objects.
 * Uses only public game API: AFGCentralStorageSubsystem::UploadItemFromInventoryToCentralStorage moves one item
 * and refuses it when the depot is at its limit, exactly as the uploader uses it.
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

#if DDC_DEV_COMMANDS
	/** DEV: true if uploading is allowed right now (Dimensional Depot research done and at least one uploader built). */
	bool IsUploadingAllowed() { return IsGateOpen(); }
	/** DEV: average and worst game-thread cost of this subsystem's Tick since the last reset, in microseconds. */
	void ResetTickStats() { TickSecondsTotal = 0.0; TickSecondsMax = 0.0; TickCount = 0; }
	double GetAverageTickMicroseconds() const { return TickCount > 0 ? TickSecondsTotal * 1.0e6 / TickCount : 0.0; }
	double GetMaxTickMicroseconds() const { return TickSecondsMax * 1.0e6; }
	int64 GetTickCount() const { return TickCount; }
	int32 GetTrackedCrateCount() const { return Crates.Num(); }
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
	int32 CountUploadersInWorld() const;
	void ReadConfig();
	FString DescribeSettings() const;
	void BindConfig();
	void ResetAllTimers();

	UFUNCTION()
	void OnConfigValueChanged();
	void OnDebounceElapsed();

	// Uploading
	void TickPerCrate(float DeltaSeconds, float TimeToUpload, AFGCentralStorageSubsystem* CentralStorage);
	void TickShared(float DeltaSeconds, float TimeToUpload, AFGCentralStorageSubsystem* CentralStorage);
	void TickPerSlot(float DeltaSeconds, float TimeToUpload, AFGCentralStorageSubsystem* CentralStorage);
	/** True if this crate's equipment and items with state must stay in it (death crates, unless the option is on). */
	bool ShouldSkipEquipment(const AFGCrate* Crate) const;
	/** Equipment, or an item carrying state (fuel, ammo, ...), which the depot would not keep. */
	static bool IsEquipmentOrStateful(const FInventoryStack& Stack);
	static bool CanUploadAnySlot(const UFGInventoryComponent* Inventory, const AFGCentralStorageSubsystem* CentralStorage, bool bSkipEquipment);
	static bool CanUploadSlot(const UFGInventoryComponent* Inventory, int32 SlotIndex, const AFGCentralStorageSubsystem* CentralStorage, bool bSkipEquipment);
	/** Uploads one item from the first slot that accepts it. The crate may be destroyed when this returns true. */
	bool TryUploadOneFromCrate(UFGInventoryComponent* Inventory, AFGCentralStorageSubsystem* CentralStorage, bool bSkipEquipment);
	bool TryUploadSlot(UFGInventoryComponent* Inventory, int32 SlotIndex, AFGCentralStorageSubsystem* CentralStorage, bool bSkipEquipment);
	void NoteUpload();

	TArray<FTrackedCrate> Crates;
	/** The crates in Crates, for constant-time duplicate checks when crates are found again by spawn events or rescans. */
	TSet<TObjectKey<AFGCrate>> TrackedKeys;

	/** Crates seen while the upload loop runs; added to Crates after it. */
	TArray<TWeakObjectPtr<AFGCrate>> PendingCrates;
	bool bTicking = false;

	/** Shared mode: one timer for all crates, and the crate to try first next time (round robin). */
	DDC::FUploadTimer SharedTimer;
	int32 SharedCursor = 0;

	// Settings in effect (from this machine's Mod Options; only used on the server)
	bool bEnabled = true;
	DDC::ERateMode RateMode = DDC::ERateMode::PerCrate;
	bool bIncludeDeathCrates = false;
	bool bUploadDeathCrateEquipment = false;

	// Gate
	UPROPERTY(Transient)
	TSubclassOf<UFGSchematic> DepotResearchClass;
	bool bResearchDone = false;
	bool bLastGateOpen = false;
	/** Uploader buildings in the world, refreshed at most once a second. */
	int32 UploadersInWorld = 0;
	double LastUploaderCountSeconds = -1.0;

	// Statistics (since this world started)
	int64 ItemsUploaded = 0;
	double FirstUploadSeconds = -1.0;
	double LastUploadSeconds = -1.0;

#if DDC_DEV_COMMANDS
	double TickSecondsTotal = 0.0;
	double TickSecondsMax = 0.0;
	int64 TickCount = 0;
#endif

	FDelegateHandle ActorSpawnedHandle;
	TWeakObjectPtr<UConfigPropertySection> BoundConfigSection;
	FTimerHandle RescanTimer;
	FTimerHandle DebounceTimer;
};
