// CWToolUIDataAsset.h

#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/CWToolTypes.h"
#include "Engine/DataAsset.h"
#include "CWToolUIDataAsset.generated.h"

class UTexture2D;

// 도구 하나의 UI 표시 정보
// 도구 이름과 아이콘을 한곳에서 관리하여 여러 위젯이 같은 표시 데이터를 사용하도록 함
USTRUCT(BlueprintType)
struct CROSSWALK_API FCWToolUIData
{
	GENERATED_BODY()

public:
	// 화면에 표시할 현지화 가능한 도구 이름
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|Tool")
	FText DisplayName;

	// 장비 UI에 표시할 도구 아이콘
	// 아직 아이콘이 준비되지 않은 항목은 비워둘 수 있음
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|Tool")
	TObjectPtr<UTexture2D> Icon = nullptr;
};

// 도구 종류별 UI 표시 정보를 관리하는 데이터 애셋
UCLASS(BlueprintType)
class CROSSWALK_API UCWToolUIDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	// 에디터에서 도구 항목 누락과 빈 표시 이름을 검사
	virtual EDataValidationResult IsDataValid(
		FDataValidationContext& Context
	) const override;
#endif

	// 도구 종류에 대응하는 UI 표시 정보를 반환
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Tool")
	bool FindToolUIData(
		ECWToolType ToolType,
		FCWToolUIData& OutToolUIData
	) const;

public:
	// 도구 종류별 화면 표시 정보
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|Tool")
	TMap<ECWToolType, FCWToolUIData> ToolUIDataMap;
};
