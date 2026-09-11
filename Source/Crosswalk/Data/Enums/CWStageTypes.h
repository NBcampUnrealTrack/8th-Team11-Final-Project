// Copyright Crosswalk Team. All Rights Reserved.
// CWStageTypes.h

#pragma once

#include "CoreMinimal.h"
#include "CWStageTypes.generated.h"

/** 스테이지 한 판의 상태. */
UENUM(BlueprintType)
enum class ECWStageState : uint8
{
	// 작업 목표가 아직 남아 있다.
	InProgress UMETA(DisplayName = "진행 중"),

	// 모든 작업 목표를 달성했다. (기획서 2절 코어 루프의 마지막 단계)
	Cleared UMETA(DisplayName = "클리어"),

	// 실패 조건은 "없음" 으로 확정됐다 (팀 노션 기준). 미정이 아니라 결정이다.
	// 그래서 이 상태로 넘어가는 경로가 의도적으로 없다. 덜 만든 것이 아니므로 채우지 말 것.
	// 나중에 실패 조건이 생기면 게임모드에서 전이시키면 된다.
	Failed UMETA(DisplayName = "실패")
};
