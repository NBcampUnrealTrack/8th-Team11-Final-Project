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

    /**
     * 플레이어 기준 커서 방향. 대상 선별(부채꼴 각도)에 쓴다.
     *
     * 차량이 날아갈 방향은 이 값이 아니라 CursorWorldLocation 으로 차량이 직접 낸다.
     * 여기는 "플레이어가 어느 쪽을 가리켰나"이고, 그쪽은 "차가 어디로 가야 하나"라서 기준점이 다르다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Parry")
    FVector AttemptDirection = FVector::ForwardVector;

    /**
     * 플레이어가 찍은 커서의 월드 지점. 패링당한 차량이 이 지점을 향해 날아간다.
     *
     * 방향이 아니라 지점을 넘기는 이유: 방향만 넘기면 차량은 커서 쪽이 아니라
     * "커서 방향과 평행하게" 날아간다. 차량과 플레이어의 위치가 다르기 때문이다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Parry")
    FVector CursorWorldLocation = FVector::ZeroVector;

    /**
     * CursorWorldLocation 을 쓸 수 있는가.
     *
     * 커서 트레이스가 빗나가면 플레이어 위치가 그대로 돌아온다. 그 값을 목표로 삼으면
     * 차가 플레이어를 향해 날아오므로, 그 경우를 구분해 AttemptDirection 으로 되돌린다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosswalk|Parry")
    bool bHasCursorTarget = false;

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