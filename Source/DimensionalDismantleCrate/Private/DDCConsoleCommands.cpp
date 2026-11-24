// Console commands. On a dedicated server they can be run from the Server Manager console, which the game
// restricts to administrators. Set is refused on clients, where the mod does nothing.

#include "CoreMinimal.h"
#include "DDCConstants.h"
#include "DDCSubsystem.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#if DDC_DEV_COMMANDS
#include "DDCDevTools.h"
#include "DDCLog.h"
#include "EngineUtils.h"
#include "FGCrate.h"
#include "Resources/FGItemDescriptor.h"
#endif

namespace
{
	ADimensionalDismantleCrateSubsystem* FindSubsystem(UWorld* World, FOutputDevice& Ar)
	{
		ADimensionalDismantleCrateSubsystem* Subsystem = ADimensionalDismantleCrateSubsystem::Get(World);
		if (!Subsystem)
		{
			Ar.Log(TEXT("Dimensional Dismantle Crate is not active in this world (main menu, or this is a client: only the host or server runs it)."));
		}
		return Subsystem;
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GStatusCommand(
		TEXT("DDC.Status"),
		TEXT("Shows Dimensional Dismantle Crate's settings, whether uploading is allowed yet, the upload speed and the crates it sees."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			if (const ADimensionalDismantleCrateSubsystem* Subsystem = FindSubsystem(World, Ar))
			{
				Subsystem->DescribeStatus(Ar);
			}
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GSetCommand(
		TEXT("DDC.Set"),
		TEXT("DDC.Set <Enabled|IncludeDeathCrates|UploadDeathCrateEquipment|SendDismantleOverflowToDepot|ShowStuckNotices|SinkWhenDepotFull|RefundsFollowDepotPreference> <true|false>, or DDC.Set RateMode <PerCrate|Shared|PerSlot|Instant|PerUploader> - changes and saves a setting on the host or server."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			if (Args.Num() != 2)
			{
				Ar.Logf(TEXT("Usage: DDC.Set <%s|%s|%s|%s|%s|%s|%s> <true|false>, or DDC.Set %s <PerCrate|Shared|PerSlot|Instant|PerUploader>"),
					DDC::KeyEnabled, DDC::KeyIncludeDeathCrates, DDC::KeyUploadDeathCrateEquipment, DDC::KeyOverflowToDepot,
					DDC::KeyShowStuckNotices, DDC::KeySinkWhenDepotFull, DDC::KeyRefundsFollowDepotPreference, DDC::KeyRateMode);
				return;
			}
			FString Message;
			ADimensionalDismantleCrateSubsystem::SetConfigValue(World, Args[0], Args[1], Message);
			Ar.Log(Message);
		}));

#if DDC_DEV_COMMANDS
	// DEV ONLY: lists the crates this machine sees and their item counts. On a client this is the replicated view,
	// which multiplayer tests compare with the server's DDC.Status to check that uploads reach clients.
	FAutoConsoleCommandWithWorldArgsAndOutputDevice GCratesCommand(
		TEXT("DDC.Dev.Crates"),
		TEXT("DEV: lists every crate this machine sees (type, items, filled slots). Works on clients too."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			int32 Count = 0;
			for (TActorIterator<AFGCrate> It(World); It; ++It)
			{
				AFGCrate* Crate = *It;
				const FString Line = FString::Printf(TEXT("[DDCCrates] %s %s: %d item(s) in %d slot(s)%s"), *Crate->GetName(),
					Crate->GetCrateType() == EFGCrateType::CT_DeathCrate ? TEXT("death") : TEXT("dismantle"),
					DDC::Dev::CountItems(Crate), DDC::Dev::CountFilledSlots(Crate), Crate->IsActorBeingDestroyed() ? TEXT(" (being destroyed)") : TEXT(""));
				Ar.Log(Line);
				UE_LOG(LogDimensionalDismantleCrate, Display, TEXT("%s"), *Line);
				++Count;
			}
			const FString Summary = FString::Printf(TEXT("[DDCCrates] %d crate(s) seen (%s)"), Count,
				World && World->GetNetMode() == NM_Client ? TEXT("client view") : TEXT("server/standalone view"));
			Ar.Log(Summary);
			UE_LOG(LogDimensionalDismantleCrate, Display, TEXT("%s"), *Summary);
		}));

	// DEV ONLY: removed from release builds (DDC_DEV_COMMANDS=0).
	FAutoConsoleCommandWithWorldArgsAndOutputDevice GSpawnCrateCommand(
		TEXT("DDC.Dev.SpawnCrate"),
		TEXT("DEV: DDC.Dev.SpawnCrate <Item> <Count> [death] - spawns a dismantle (or death) crate in front of the first player, never merged. Server only."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			if (!World || World->GetNetMode() == NM_Client)
			{
				Ar.Log(TEXT("Refused: run this on the host or server."));
				return;
			}
			if (Args.Num() < 2)
			{
				Ar.Log(TEXT("Usage: DDC.Dev.SpawnCrate <Item, e.g. IronPlate> <Count> [death]"));
				return;
			}
			const TSubclassOf<UFGItemDescriptor> ItemClass = DDC::Dev::FindItemClass(Args[0]);
			const int32 Count = FCString::Atoi(*Args[1]);
			if (!ItemClass || Count <= 0)
			{
				Ar.Logf(TEXT("Refused: item '%s' not found among loaded descriptors, or count %d is not positive."), *Args[0], Count);
				return;
			}
			const bool bDeath = Args.Num() > 2 && Args[2].Equals(TEXT("death"), ESearchCase::IgnoreCase);
			AFGCrate* Crate = DDC::Dev::SpawnCrate(World, {{ItemClass, Count}}, bDeath ? EFGCrateType::CT_DeathCrate : EFGCrateType::CT_DismantleCrate, FVector(300, 0, 100));
			if (!Crate)
			{
				Ar.Log(TEXT("SpawnInventoryCrate returned null."));
				return;
			}
			Ar.Logf(TEXT("Spawned %s (%s crate) with %d x %s in %d slot(s)"), *Crate->GetName(), bDeath ? TEXT("death") : TEXT("dismantle"),
				Count, *ItemClass->GetName(), DDC::Dev::CountFilledSlots(Crate));
		}));
#endif
}
