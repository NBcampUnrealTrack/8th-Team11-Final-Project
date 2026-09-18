// Copyright Crosswalk Team. All Rights Reserved.
// CWWeatherTypes.h

#pragma once

#include "CoreMinimal.h"
#include "CWWeatherTypes.generated.h"

// 날씨 상태 사이클. 서버가 이 순서대로 순환시킨다 (Calm -> Warning -> Peak -> Decrease -> Calm ...)
UENUM(BlueprintType)
enum class ECWWeatherState : uint8
{
	Calm,
	Warning,
	Peak,
	Decrease
};
