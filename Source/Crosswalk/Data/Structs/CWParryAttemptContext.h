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

    /**
     * 서버가 패링 대상을 찾을 때 사용할 기본 반경. 0 이면 거리 제한 없음.
     *
     * 고정 반경으로는 판정을 맞출 수 없어 0 으로 둔다.
     * 윈도우 안에 들어온 차량의 거리는 (속도 x 윈도우)라서 차마다 다르고,
     * 스포너가 개체별 속도 배율을 굴리므로 같은 차종끼리도 다르다.
     * 750cm/s 기준으로 225 를 잡으면 그보다 빠른 차량이 다시 잘린다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Parry")
    float ParryRadius = 0.f;

    // 패링 판정이 허용되는 부채꼴 전체 각(도). 360 이면 방향 제한 없음
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Parry")
    float ParryAngle = 360.f;

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