// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TrafficVehicleSpawnParams.generated.h"

/**
 * 스포너가 차량 1대를 굴려보낼 때 넘기는 개체별 값.
 * 차종 고유 값(기본 속도, 패링 가능 여부)은 차량 BP가 들고, 여기 있는 건 개체마다 흔들리는 값만이다.
 */
USTRUCT(BlueprintType)
struct FTrafficVehicleSpawnParams
{
	GENERATED_BODY()

	/** 차선 시작점에서의 초기 진행 거리(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	float StartDistance = 0.f;

	/** 차종 기본 속도에 곱할 배율. 1.0이면 BP에 설정된 속도 그대로. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	float SpeedMultiplier = 1.f;

	/** 차선 중심선에서 좌우로 밀어낼 거리(cm). 양수면 진행 방향 기준 오른쪽. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	float LateralOffset = 0.f;
};
