// Copyright Crosswalk Team. All Rights Reserved.
// CWWeatherEventPayload.h

#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/CWWeatherTypes.h"
#include "CWWeatherEventPayload.generated.h"

/**
 * 서버가 새 날씨 상태로 넘어갈 때 델리게이트로 전달하는 값. 일부러 Replicated로 두지 않는다 —
 * 실제로 굴려진 랜덤값까지 복제하려면 별도 동기화 구조체+RPC가 필요한데, 순수 연출용 날씨에는
 * 그 비용이 안 맞는다(YAGNI). 클라이언트는 OnRep으로 상태 enum만 받고, 이 struct에 해당하는
 * 값은 DataTable의 기본값(편차 없이)으로 스스로 근사 재현한다 — "정확히 같은 값"이 아니라
 * "얼추 비슷한 연출"이면 충분하다는 판단.
 */
USTRUCT(BlueprintType)
struct FCWWeatherEventPayload
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	ECWWeatherState WeatherState = ECWWeatherState::Calm;

	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float WindSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float WeatherIntensity = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float BlendTime = 0.f;
};
