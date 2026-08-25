// CWUIPageConfigDataAsset.cpp

#include "UI/Data/CWUIPageConfigDataAsset.h"
#include "UI/CWUILog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#pragma region Validation

#if WITH_EDITOR
EDataValidationResult UCWUIPageConfigDataAsset::IsDataValid(
	FDataValidationContext& Context
) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// 페이지 정책이 하나도 없으면 쓸모없는 에셋이다
	if (PageConfigs.Num() == 0)
	{
		Context.AddError(
			NSLOCTEXT(
				"CWUIPageConfig",
				"EmptyConfigs",
				"페이지 설정이 비어 있습니다."
			)
		);

		return EDataValidationResult::Invalid;
	}

	// enum 을 순회하지 않고 가진 항목만 검사한다.
	// 페이지 DataAsset 은 그 환경의 페이지만 담는 게 정상이라
	// 특정 페이지 누락을 오류로 보면 정상 에셋이 오류가 된다
	for (const TPair<ECWUIPage, FCWUIPageConfig>& Pair : PageConfigs)
	{
		// None 은 상태값이지 실제 페이지가 아니다
		if (Pair.Key == ECWUIPage::None)
		{
			Context.AddError(
				NSLOCTEXT(
					"CWUIPageConfig",
					"NoneKey",
					"None 은 페이지 키로 사용할 수 없습니다."
				)
			);

			Result = EDataValidationResult::Invalid;
			continue;
		}

		// 화면 클래스가 없으면 페이지를 열 수 없다
		if (!Pair.Value.WidgetClass)
		{
			Context.AddError(
				FText::Format(
					NSLOCTEXT(
						"CWUIPageConfig",
						"MissingWidget",
						"{0} 페이지의 WidgetClass 가 비어 있습니다."
					),
					FText::FromString(
						UEnum::GetValueAsString(Pair.Key)
					)
				)
			);

			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif

#pragma endregion

#pragma region PageConfig

bool UCWUIPageConfigDataAsset::FindPageConfig(
	ECWUIPage Page,
	FCWUIPageConfig& OutPageConfig
) const
{
	const FCWUIPageConfig* FoundConfig =
		PageConfigs.Find(Page);

	if (FoundConfig == nullptr)
	{
		OutPageConfig = FCWUIPageConfig();

		UE_LOG(
			LogCWUI,
			Warning,
			TEXT("PageConfigDataAsset: page config not found. Page=%s"),
			*UEnum::GetValueAsString(Page)
		);

		return false;
	}

	OutPageConfig = *FoundConfig;
	return true;
}

#pragma endregion
