// CWUIPageConfigDataAsset.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/Enums/CWUITypes.h"
#include "UI/Core/CWUIScreenWidget.h"
#include "CWUIPageConfigDataAsset.generated.h"

// 페이지 하나의 정책
// 화면 클래스와 입력 모드, HUD 숨김 여부를 데이터로 둔다
USTRUCT(BlueprintType)
struct CROSSWALK_API FCWUIPageConfig
{
	GENERATED_BODY()

#pragma region PageConfigData

public:
	// 이 페이지에 표시할 화면 클래스
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|Page")
	TSubclassOf<UCWUIScreenWidget> WidgetClass;

	// 화면이 열릴 때 적용할 입력 모드
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|Page")
	ECWUIInputMode InputMode = ECWUIInputMode::UIOnly;

	// 화면이 열릴 때 HUD 레이어를 숨길지 여부
	// 같은 페이지라도 진입 환경에 따라 다름 (인게임 Result 만 숨김)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|Page")
	bool bHideHUD = false;

#pragma endregion
};

// 한 환경(메뉴/방/인게임)에서 열 수 있는 페이지 정책 모음
// 각 DataAsset 은 그 환경의 페이지만 담는다. 완전성을 요구하지 않는다
UCLASS(BlueprintType)
class CROSSWALK_API UCWUIPageConfigDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

#pragma region Validation

public:
#if WITH_EDITOR
	// 저장 시 빈 맵, None 키, WidgetClass 누락을 검출
	virtual EDataValidationResult IsDataValid(
		class FDataValidationContext& Context
	) const override;
#endif

#pragma endregion

#pragma region PageConfig

public:
	// 페이지에 해당하는 정책을 반환. 없으면 false
	UFUNCTION(BlueprintPure, Category = "CW|UI|Page")
	bool FindPageConfig(
		ECWUIPage Page,
		FCWUIPageConfig& OutPageConfig
	) const;

	// 등록된 페이지 정책 전체를 반환 (Subsystem 병합용)
	const TMap<ECWUIPage, FCWUIPageConfig>& GetPageConfigs() const
	{
		return PageConfigs;
	}

#pragma endregion

#pragma region PageConfigVariables

private:
	// 페이지 종류별 정책
	UPROPERTY(EditAnywhere, Category = "CW|UI|Page")
	TMap<ECWUIPage, FCWUIPageConfig> PageConfigs;

#pragma endregion
};
