// Copyright Crosswalk Team. All Rights Reserved.
// CWStageObjective.h

#pragma once

#include "CoreMinimal.h"
#include "CWStageObjective.generated.h"

/**
 * HUD 체크리스트 한 줄. (기획서 3.6)
 *
 * 존 하나가 목표 하나는 아니다. 횡단보도 줄무늬처럼 같은 작업을 여러 존으로 쪼개 놓으면
 * 존 개수만큼 줄이 늘어난다. 그래서 ObjectiveName 이 같은 존들을 한 줄로 묶는다.
 * "도색 작업 (3/5)" 처럼 부분 진행도까지 보여줄 수 있다.
 */
USTRUCT(BlueprintType)
struct FCWStageObjective
{
	GENERATED_BODY()

public:
	bool operator==(const FCWStageObjective& Other) const
	{
		return CompletedCount == Other.CompletedCount
			&& TotalCount == Other.TotalCount
			&& DisplayName.IdenticalTo(Other.DisplayName);
	}

	bool operator!=(const FCWStageObjective& Other) const
	{
		return !(*this == Other);
	}

	/** 이 목표가 전부 끝났는가. 체크박스 표시에 그대로 쓴다. */
	bool IsCompleted() const { return TotalCount > 0 && CompletedCount >= TotalCount; }

public:
	// 체크리스트에 표시할 이름. 존의 ObjectiveName 이 그대로 온다
	UPROPERTY(BlueprintReadOnly, Category = "Crosswalk|Stage")
	FText DisplayName;

	// 이 이름으로 묶인 존 중 끝난 수
	UPROPERTY(BlueprintReadOnly, Category = "Crosswalk|Stage")
	int32 CompletedCount = 0;

	// 이 이름으로 묶인 존의 총 수
	UPROPERTY(BlueprintReadOnly, Category = "Crosswalk|Stage")
	int32 TotalCount = 0;

	// 정렬 기준. 같은 이름 묶음 중 가장 작은 WorkOrder 를 쓴다. 작업 순서대로 줄이 선다
	UPROPERTY(BlueprintReadOnly, Category = "Crosswalk|Stage")
	int32 WorkOrder = 0;
};
