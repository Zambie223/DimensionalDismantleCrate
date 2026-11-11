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

	/**
	 * Values of KeyRateMode, stored as integers in the config file, so never renumber them.
	 * The Mod Options dropdown labels come from the UserDefinedEnum asset made by Tools/create_config_asset.py
	 * (a C++ UENUM's display names don't survive into Shipping builds); keep both in the same order.
	 */
	enum class ERateMode : uint8
	{
		/** Every crate uploads like one Dimensional Depot Uploader. */
		PerCrate = 0,
		/** All crates together upload like one uploader, taking turns. */
		Shared = 1,
		/** Every occupied crate slot uploads like one uploader (like the player's upload slots). */
		PerSlot = 2,
	};
	inline constexpr int32 RateModeMin = 0;
	inline constexpr int32 RateModeMax = 2;

	inline const TCHAR* RateModeName(const ERateMode Mode)
	{
		switch (Mode)
		{
		case ERateMode::PerCrate: return TEXT("Per crate");
		case ERateMode::Shared: return TEXT("Shared");
		case ERateMode::PerSlot: return TEXT("Per stack slot");
		default: return TEXT("Unknown");
		}
	}
}
