// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "CWTrafficVehicleEntry.generated.h"

class ACWBaseVehicle;

/**
 * 한 차선에 섞어 내보낼 차종 하나와 그 등장 비율.
 * 스포너가 이 목록에서 가중치 추첨으로 차종을 고른다.
 */
USTRUCT(BlueprintType)
struct FCWTrafficVehicleEntry
{
	GENERATED_BODY()

	/** 스폰할 차종 BP. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	TSubclassOf<ACWBaseVehicle> VehicleClass;

	/** 등장 가중치. 3:1로 섞으려면 3과 1을 넣는다. 0이면 목록에 남겨둔 채 등장만 막는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0"))
	float Weight = 1.f;
};
