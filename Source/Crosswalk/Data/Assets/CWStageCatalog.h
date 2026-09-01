// CWStageCatalog.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/Structs/CWStageInfo.h"
#include "CWStageCatalog.generated.h"

class FDataValidationContext;

// 선택 가능한 스테이지 전체 목록 (DataAsset 저작)
// UI(스테이지 선택 팝업)와 서버(ServerTravel 대상 맵 조회)가 같은 애셋을 참조함
// 배열 순서가 곧 목록 표시 순서임. 선택값은 배열 인덱스가 아니라 StageId 로 주고받으므로
// 중간 항목을 지워 인덱스가 밀려도 이미 고른 스테이지는 유지됨
UCLASS(BlueprintType)
class CROSSWALK_API UCWStageCatalog : public UPrimaryDataAsset
{
	GENERATED_BODY()

#pragma region Validation

public:
#if WITH_EDITOR
	// 저장 시 빈/중복 StageId 와 GameplayMap 누락을 검출함
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

#pragma endregion

#pragma region Query

public:
	// 저작된 스테이지 전체 (배열 순서 = 목록 표시 순서)
	const TArray<FCWStageInfo>& GetStages() const
	{
		return Stages;
	}

	// StageId 로 스테이지를 찾음. 없으면 nullptr (서버 검증·맵 조회에 사용)
	const FCWStageInfo* FindStageById(FName StageId) const;

#pragma endregion

#pragma region Data

private:
	// 선택 가능한 스테이지 목록
	UPROPERTY(EditAnywhere, Category = "Crosswalk|Stage")
	TArray<FCWStageInfo> Stages;

#pragma endregion
};
