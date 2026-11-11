#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DDCEditorTools.generated.h"

class UUserDefinedEnum;

/**
 * Editor-only helpers for the headless asset scripts in Tools/ (does nothing in the game).
 *
 * Why: SML's enum dropdown shows UEnum::GetDisplayNameTextByIndex. A C++ UENUM's DisplayName metadata is stripped from
 * Shipping builds, so the game showed raw names ("PerCrate"). A UserDefinedEnum asset keeps its display names at
 * runtime, but editor Python can't add enumerators to one, hence this helper.
 */
UCLASS()
class DIMENSIONALDISMANTLECRATE_API UDDCEditorTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Makes the enum have exactly DisplayNames.Num() enumerators (values 0..N-1) with these display names.
	 * Returns false outside the editor or on failure. The caller saves the asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dimensional Dismantle Crate|Editor")
	static bool SetupUserDefinedEnum(UUserDefinedEnum* Enum, const TArray<FText>& DisplayNames);
};
