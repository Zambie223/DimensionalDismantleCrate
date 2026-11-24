#pragma once

#include "CoreMinimal.h"

namespace DDC
{
	/** Must match the plugin name; SML uses it as the config file name (Configs/DimensionalDismantleCrate.cfg). */
	inline constexpr const TCHAR* ModReference = TEXT("DimensionalDismantleCrate");

	/** Mod Configuration asset, created by Tools/create_config_asset.py. */
	inline constexpr const TCHAR* ConfigClassPath = TEXT("/DimensionalDismantleCrate/DDCConfiguration.DDCConfiguration_C");

	/** The vanilla Dimensional Depot research (MAM, Alien Technology tree). */
	inline constexpr const TCHAR* DepotResearchClassPath = TEXT("/Game/FactoryGame/Schematics/Research/AlienTech_RS/Research_Alien_CentralStorage.Research_Alien_CentralStorage_C");

	/** Keys in the configuration's root section. Changing these renames the values in users' config files. */
	inline constexpr const TCHAR* KeyEnabled = TEXT("Enabled");
	inline constexpr const TCHAR* KeyRateMode = TEXT("RateMode");
	inline constexpr const TCHAR* KeyIncludeDeathCrates = TEXT("IncludeDeathCrates");
	inline constexpr const TCHAR* KeyUploadDeathCrateEquipment = TEXT("UploadDeathCrateEquipment");
	inline constexpr const TCHAR* KeyOverflowToDepot = TEXT("SendDismantleOverflowToDepot");
	inline constexpr const TCHAR* KeyShowStuckNotices = TEXT("ShowStuckNotices");
	inline constexpr const TCHAR* KeySinkWhenDepotFull = TEXT("SinkWhenDepotFull");
	inline constexpr const TCHAR* KeyRefundsFollowDepotPreference = TEXT("RefundsFollowDepotPreference");

	/**
	 * Budget for every item this mod adds to the depot (crate uploads, dismantle overflow, depot-first refunds), in items
	 * per second and how many may go at once. Every item is one reliable network message to every player (the depot has
	 * no bulk add), so with players connected the rate stays inside what the uploader tests showed is safe (about 5,200
	 * messages per second on a dedicated server). Without connected players there are no messages, and the budget only
	 * bounds the cost of one frame.
	 */
	inline constexpr float DepotAddsPerSecondNetworked = 3000.0f;
	inline constexpr float DepotAddsBurstNetworked = 150.0f;
	inline constexpr float DepotAddsPerSecondLocal = 100000.0f;
	inline constexpr float DepotAddsBurstLocal = 5000.0f;

	/** Instant mode: most items moved out of crates in one tick, whatever the budget allows, to bound one frame's cost. */
	inline constexpr int32 InstantMaxItemsPerTick = 500;

	/** Stuck-crate notices: how often crates are checked, the shortest gap between messages, and per item. */
	inline constexpr float StuckScanSeconds = 5.0f;
	inline constexpr double NoticeMinGapSeconds = 30.0;
	inline constexpr double NoticeItemRepeatSeconds = 300.0;

	/**
	 * Values of KeyRateMode, stored as integers in the config file, so never renumber them.
	 * The Mod Options dropdown labels come from the UserDefinedEnum asset made by Tools/create_config_asset.py
	 * (a C++ UENUM's display names don't survive into Shipping builds); keep both in the same order.
	 * Older versions of the mod read an unknown value as PerCrate.
	 */
	enum class ERateMode : uint8
	{
		/** Every crate uploads like one Dimensional Depot Uploader. */
		PerCrate = 0,
		/** All crates together upload like one uploader, taking turns. */
		Shared = 1,
		/** Every occupied crate slot uploads like one uploader (like the player's upload slots). */
		PerSlot = 2,
		/** Crates empty as fast as the network budget allows, ignoring the upload speed. */
		Instant = 3,
		/** All crates together upload like every uploader built in the world, taking turns. */
		PerUploader = 4,
	};
	inline constexpr int32 RateModeMin = 0;
	inline constexpr int32 RateModeMax = 4;

	inline const TCHAR* RateModeName(const ERateMode Mode)
	{
		switch (Mode)
		{
		case ERateMode::PerCrate: return TEXT("Per crate");
		case ERateMode::Shared: return TEXT("Shared");
		case ERateMode::PerSlot: return TEXT("Per stack slot");
		case ERateMode::Instant: return TEXT("Instant");
		case ERateMode::PerUploader: return TEXT("Shared by all uploaders");
		default: return TEXT("Unknown");
		}
	}
}
