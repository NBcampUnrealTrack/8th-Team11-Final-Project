// CWTemperatureTypes.h

#pragma once

#include "CoreMinimal.h"
#include "CWTemperatureTypes.generated.h"

/**
 * 체온 구간. (체온 기획서 6.2)
 *
 * 색과 게이지는 체온을 0~1 로 정규화한 값(HeatRatio)으로 "연속적으로" 변한다.
 * 이 enum 은 색을 정하기 위한 것이 아니라, 구간을 넘어선 "순간" 을 잡기 위한 것이다.
 * 그라디언트만 있으면 위험 구간에 들어선 시점을 알 수 없어서, 그 순간에만
 * 플래시나 사운드 같은 1회성 신호를 얹으려고 따로 둔다.
 */
UENUM(BlueprintType)
enum class ECWHeatStage : uint8
{
	Normal		UMETA(DisplayName = "정상"),
	Caution		UMETA(DisplayName = "과열 주의"),
	Overheated	UMETA(DisplayName = "과열")
};
