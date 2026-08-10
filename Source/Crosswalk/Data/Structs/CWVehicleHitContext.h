// Copyright Crosswalk Team. All Rights Reserved.
// CWVehicleHitContext.h

#pragma once

#include "CoreMinimal.h"
#include "CWVehicleHitContext.generated.h"

USTRUCT(BlueprintType)
struct FCWVehicleHitContext
{
    GENERATED_BODY()

public:
    // 충돌이 발생한 월드 위치
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Hit")
    FVector HitLocation = FVector::ZeroVector;

    // 캐릭터가 밀려날 방향
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Hit")
    FVector HitDirection = FVector::ForwardVector;

    // 충돌한 차량의 현재 속도
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Hit")
    float VehicleSpeed = 0.f;

    // 차종별 피격 프로파일을 찾기 위한 ID
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Hit")
    FName VehicleType = NAME_None;

    // 이 차량이 패링 가능한 차량인지 여부
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Hit")
    bool bCanParry = false;

    // 대형 트럭처럼 납작해지는 특수 피격을 사용할지 여부
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Hit")
    bool bFlattenOnHit = false;
};