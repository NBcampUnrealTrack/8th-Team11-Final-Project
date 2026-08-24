// Copyright Crosswalk Team. All Rights Reserved.
// CWSupplyTypes.h

#pragma once

#include "CoreMinimal.h"
#include "CWSupplyTypes.generated.h"

/**
 * 보급 관련 입력이 무시된 이유.
 *
 * 이유를 값으로 남기는 까닭은 기획서 6-6 때문이다. 도구와 통은 동시에 들 수 없어서
 * 입력이 조용히 씹히는 경우가 자주 생기는데, 아무 반응이 없으면 플레이어는
 * "조작이 안 먹었다" 로 받아들인다. 왜 안 되는지를 화면에 띄우려면 이유가 필요하다.
 */
UENUM(BlueprintType)
enum class ECWSupplyRejectReason : uint8
{
	None			UMETA(DisplayName = "없음"),
	// 작업 도구를 들고 있어서 통을 들 수 없다. (기획서 6-6)
	ToolInHand		UMETA(DisplayName = "도구를 들고 있음"),
	// 통을 들고 있어서 도구를 집을 수 없다. (기획서 6-6)
	CansInHand		UMETA(DisplayName = "페인트 통을 들고 있음"),
	// 보급 박스가 너무 멀다. (기획서 6-1, 6-2)
	NoBoxNearby		UMETA(DisplayName = "근처에 보급 박스가 없음"),
	// 이미 최대 개수를 들고 있다. (기획서 6-1)
	CarryLimit		UMETA(DisplayName = "더 들 수 없음"),
	// 들고 있는 통이 없다.
	NoCans			UMETA(DisplayName = "들고 있는 통이 없음"),
	// 충전할 도구가 사거리 안에 없다. (기획서 6-3)
	NoTargetTool	UMETA(DisplayName = "충전할 도구가 없음"),
	// 다운 중이거나 미니게임 중이라 지금은 받지 않는다. (기획서 6-7)
	Busy			UMETA(DisplayName = "지금은 할 수 없음")
};
