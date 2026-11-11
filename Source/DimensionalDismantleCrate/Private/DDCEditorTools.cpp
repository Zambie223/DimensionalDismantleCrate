#include "DDCEditorTools.h"

#include "DDCLog.h"
#include "Engine/UserDefinedEnum.h"

#if WITH_EDITOR
#include "Kismet2/EnumEditorUtils.h"
#endif

bool UDDCEditorTools::SetupUserDefinedEnum(UUserDefinedEnum* Enum, const TArray<FText>& DisplayNames)
{
#if WITH_EDITOR
	if (!Enum || DisplayNames.Num() == 0)
	{
		return false;
	}
	Enum->Modify();
	// NumEnums() includes the hidden _MAX entry.
	while (Enum->NumEnums() - 1 < DisplayNames.Num())
	{
		FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
	}
	while (Enum->NumEnums() - 1 > DisplayNames.Num())
	{
		FEnumEditorUtils::RemoveEnumeratorFromUserDefinedEnum(Enum, Enum->NumEnums() - 2);
	}
	bool bOk = true;
	for (int32 Index = 0; Index < DisplayNames.Num(); ++Index)
	{
		// Setting the same name again returns false; only a mismatch afterwards is a real failure.
		FEnumEditorUtils::SetEnumeratorDisplayName(Enum, Index, DisplayNames[Index]);
		if (!Enum->GetDisplayNameTextByIndex(Index).EqualTo(DisplayNames[Index]) || Enum->GetValueByIndex(Index) != Index)
		{
			UE_LOG(LogDimensionalDismantleCrate, Error, TEXT("Enum %s entry %d is '%s' = %lld, expected '%s' = %d"), *Enum->GetName(), Index,
				*Enum->GetDisplayNameTextByIndex(Index).ToString(), Enum->GetValueByIndex(Index), *DisplayNames[Index].ToString(), Index);
			bOk = false;
		}
	}
	return bOk;
#else
	return false;
#endif
}
