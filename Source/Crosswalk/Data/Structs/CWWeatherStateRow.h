// Copyright Crosswalk Team. All Rights Reserved.
// CWWeatherStateRow.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CWWeatherStateRow.generated.h"

/**
 * ECWWeatherState 한 단계에 대한 서버 판정용 데이터. 행 이름은 enum의 짧은 이름과 맞춘다
 * (예: "Warning") — UCWWeatherComponent가 이 이름으로 행을 찾는다.
 */
USTRUCT(BlueprintType)
struct FCWWeatherStateRow : public FTableRowBase
{
	GENERATED_BODY()

public:
	/** 이 상태가 유지되는 기본 시간(초). 실제 값은 여기에 랜덤 편차를 더해 서버가 굴린다. */
	UPROPERTY(EditAnywhere, Category = "Weather")
	float BaseDuration = 20.f;

	/** BaseDuration에 곱해질 랜덤 편차 범위 (예: 0.2면 ±20%). */
	UPROPERTY(EditAnywhere, Category = "Weather", meta = (ClampMin = "0.0"))
	float DurationRandomScale = 0.2f;

	/** 이 상태의 기본 풍속. */
	UPROPERTY(EditAnywhere, Category = "Weather")
	float BaseWindSpeed = 0.f;

	/** BaseWindSpeed에 곱해질 랜덤 편차 범위. */
	UPROPERTY(EditAnywhere, Category = "Weather", meta = (ClampMin = "0.0"))
	float WindRandomScale = 0.2f;

	/** 이전 상태에서 이 상태로 넘어올 때 클라이언트가 값을 Lerp하는 데 걸리는 시간(초). */
	UPROPERTY(EditAnywhere, Category = "Weather", meta = (ClampMin = "0.0"))
	float BlendTime = 3.f;

	/** 이 상태의 기본 강도 (0~1, Niagara SpawnRate/FallSpeedScale 등을 이 값 기준으로 스케일). */
	UPROPERTY(EditAnywhere, Category = "Weather", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WeatherIntensity = 0.5f;

	/** WeatherIntensity에 곱해질 랜덤 편차 범위. */
	UPROPERTY(EditAnywhere, Category = "Weather", meta = (ClampMin = "0.0"))
	float IntensityRandomScale = 0.1f;
};
