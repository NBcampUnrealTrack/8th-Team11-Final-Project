// CWToolUIDataAsset.cpp

#include "UI/Data/CWToolUIDataAsset.h"
#include "UI/CWUILog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#pragma region ToolUIData

bool UCWToolUIDataAsset::FindToolUIData(
	ECWToolType ToolType,
	FCWToolUIData& OutToolUIData
) const
{
	const FCWToolUIData* FoundToolUIData =
		ToolUIDataMap.Find(ToolType);

	if (FoundToolUIData != nullptr)
	{
		OutToolUIData = *FoundToolUIData;
		return true;
	}

	// 이전 호출의 데이터가 남지 않도록 실패 시 출력값 초기화
	OutToolUIData = FCWToolUIData();

	UE_LOG(
		LogCWUI,
		Warning,
		TEXT("ToolUIDataAsset: tool UI data not found. ToolType=%s"),
		*UEnum::GetValueAsString(ToolType)
	);

	return false;
}

#pragma endregion

#if WITH_EDITOR

#pragma region DataValidation

EDataValidationResult UCWToolUIDataAsset::IsDataValid(
	FDataValidationContext& Context
) const
{
	EDataValidationResult Result =
		Super::IsDataValid(Context);

	const UEnum* ToolEnum = StaticEnum<ECWToolType>();

	if (ToolEnum == nullptr)
	{
		Context.AddError(
			NSLOCTEXT(
				"CWToolUIData",
				"ToolEnumNotFound",
				"ECWToolType 정보를 찾을 수 없습니다."
			)
		);

		return EDataValidationResult::Invalid;
	}

	const int64 MaxEnumValue =
		ToolEnum->GetMaxEnumValue();

	for (
		int32 Index = 0;
		Index < ToolEnum->NumEnums();
		++Index
		)
	{
		const int64 EnumValue =
			ToolEnum->GetValueByIndex(Index);

		// UHT가 생성한 최대값 항목은 실제 도구가 아니므로 제외
		if (EnumValue == MaxEnumValue)
		{
			continue;
		}

		const ECWToolType ToolType =
			static_cast<ECWToolType>(EnumValue);

		const FCWToolUIData* ToolUIData =
			ToolUIDataMap.Find(ToolType);

		// 도구 종류에 대응하는 데이터가 없는 경우
		if (ToolUIData == nullptr)
		{
			Context.AddError(
				FText::Format(
					NSLOCTEXT(
						"CWToolUIData",
						"MissingEntry",
						"{0} 항목이 데이터 애셋에 없습니다."
					),
					FText::FromString(
						ToolEnum->GetNameStringByIndex(Index)
					)
				)
			);

			Result = EDataValidationResult::Invalid;
			continue;
		}

		// 아이콘은 비워둘 수 있지만 화면 표시 이름은 필수
		if (ToolUIData->DisplayName.IsEmpty())
		{
			Context.AddError(
				FText::Format(
					NSLOCTEXT(
						"CWToolUIData",
						"EmptyDisplayName",
						"{0} 항목의 DisplayName이 비어 있습니다."
					),
					FText::FromString(
						ToolEnum->GetNameStringByIndex(Index)
					)
				)
			);

			Result = EDataValidationResult::Invalid;
		}
	}

	// 부모가 NotValidated를 반환하더라도 이 클래스의 검사를 통과하면 Valid
	if (Result != EDataValidationResult::Invalid)
	{
		Result = EDataValidationResult::Valid;
	}

	return Result;
}

#pragma endregion

#endif
