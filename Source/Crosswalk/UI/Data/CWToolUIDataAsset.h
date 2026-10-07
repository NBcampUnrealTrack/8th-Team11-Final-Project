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

	// 페인트 잔량 게이지가 이 도구의 잔량을 채워 보이는지
	// 게임플레이의 페인트 사용 여부와 다를 수 있어(아스팔트 건은 페인트를 쓰지 않지만 게이지를 보임) 따로 둠
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|Tool")
	bool bShowsFill = false;

	// 어두운 페인트 색으로 채우는지
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|Tool")
	bool bDarkPaint = false;
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

	// 들고 있는 페인트 통의 아이콘 - 도구 종류가 아니라 맵의 키로 둘 수 없어 따로 둠
	UFUNCTION(BlueprintPure, Category = "CW|UI|Tool")
	UTexture2D* GetPaintCanIcon() const { return PaintCanIcon; }

	// 들고 있는 페인트 통의 이름
	UFUNCTION(BlueprintPure, Category = "CW|UI|Tool")
	FText GetPaintCanLabel() const { return PaintCanLabel; }

public:
	// 도구 종류별 화면 표시 정보
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|Tool")
	TMap<ECWToolType, FCWToolUIData> ToolUIDataMap;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|Tool")
	TObjectPtr<UTexture2D> PaintCanIcon = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|Tool")
	FText PaintCanLabel;
};
