// Copyright Crosswalk Team. All Rights Reserved.
// CWParryAttemptContext.h

#pragma once

#include "CoreMinimal.h"
#include "CWParryAttemptContext.generated.h"

class AActor;

USTRUCT(BlueprintType)
struct FCWParryAttemptContext
{
    GENERATED_BODY()

public:
    // 패링을 시도한 액터. 수신 측이 충돌 예측 기준점으로 쓴다
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Parry")
    TObjectPtr<AActor> Parrier = nullptr;

    // 패링을 시도한 월드 위치
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Parry")
    FVector AttemptLocation = FVector::ZeroVector;

    // 플레이어가 바라보거나 방어하려는 방향
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Parry")
    FVector AttemptDirection = FVector::ForwardVector;

    // 서버가 패링 대상을 찾을 때 사용할 기본 반경
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Parry")
    float ParryRadius = 225.f;

    // 패링 판정이 허용되는 각도
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Parry")
    float ParryAngle = 90.f;

    // 이미 특정 차량을 알고 있을 때 서버 검증에 사용할 대상
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Parry")
    TObjectPtr<AActor> TargetVehicle = nullptr;

    // 차종별 예외 처리를 위한 ID
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Parry")
    FName VehicleType = NAME_None;

    // 클라이언트 입력 시점을 기록하기 위한 값이며, 최종 판정은 서버가 결정.
    // 클라이언트 월드 시간이라 서버 시간과 직접 비교할 수 없다. 기록·디버깅용이다
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Parry")
    float ClientInputTime = 0.f;
};