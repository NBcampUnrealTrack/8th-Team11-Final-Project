// Copyright Crosswalk Team. All Rights Reserved.
// CWHitSyncData.h

#pragma once

#include "CoreMinimal.h"
#include "CWHitSyncData.generated.h"

USTRUCT(BlueprintType)
struct FCWHitSyncData
{
    GENERATED_BODY()

public:
    // 서버가 확정한 피격 발생 위치
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Hit")
    FVector HitLocation = FVector::ZeroVector;

    // 서버가 확정한 넉백 방향
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Hit")
    FVector LaunchDirection = FVector::ForwardVector;

    // 수평 방향으로 날아가는 힘
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Hit")
    float LaunchStrength = 0.f;

    // 위로 떠오르는 힘
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Hit")
    float UpwardStrength = 0.f;

    // 피격 후 조작 불가능한 시간
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Hit")
    float StunDuration = 0.f;

    // 래그돌 또는 피격 연출을 유지할 시간
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Hit")
    float HitReactionDuration = 0.f;

    // 차종별 연출 분기를 위한 ID
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Hit")
    FName VehicleType = NAME_None;

    // 납작해지는 특수 피격 연출 여부
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Hit")
    bool bFlattenOnHit = false;
};