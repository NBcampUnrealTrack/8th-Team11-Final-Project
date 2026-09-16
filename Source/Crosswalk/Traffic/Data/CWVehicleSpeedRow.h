// CWVehicleSpeedRow.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CWVehicleSpeedRow.generated.h"

/**
 * 차종 하나의 주행 속도 범위. 스폰할 때 이 범위에서 한 번 뽑는다.
 *
 * **행 이름이 곧 차종 이름이다.** 차체(FCWVehicleMeshVariant)가 자기 SpeedType 에
 * 같은 이름을 적어 두면 이 행을 받는다. 차체 수만큼 행을 만들 필요가 없다 -
 * 승용차 BP 에만 차체가 15 종이지만 "세단 / SUV / 밴 / 스포츠" 처럼 묶으면 네 행이다.
 *
 * 차종을 BP 단위로 잡지 않는 이유는 한 BP 안에 거동이 다른 차체가 섞이기 때문이다.
 * BP_Vehicle_Car 에는 세단도 밴도 들어 있는데 BP 로 묶으면 둘이 같은 속도를 받는다.
 *
 * **뽑고 나면 그 차의 속도는 끝까지 안 바뀐다.** 주행 중에 흔들면 "봤을 때의 속도" 가
 * 더 이상 도착 시점의 근거가 되지 못해서, 플레이어가 틀린 게 아니라 판단 근거가 바뀐
 * 상황이 된다. 차 한 대의 거동은 예측 가능해야 회피·패링 학습이 성립한다.
 *
 * 범위를 넓힐 때 기준: 예고 1.81 초 구간에서 속도 편차 ±N% 는 도착 시점 오차
 * 약 ±(1.81 x N%) 초를 만든다. ParryWindow 가 0.3 초이므로 ±8%(±0.14 초) 를 넘기면
 * 패링이 실력이 아니라 운으로 기운다.
 */
USTRUCT(BlueprintType)
struct FCWVehicleSpeedRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 속도 범위 하한(cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0"))
	float SpeedMin = 0.f;

	/** 속도 범위 상한(cm/s). SpeedMin 이하이면 이 행을 쓰지 않고 차종 BP 의 Speed 로 달린다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0"))
	float SpeedMax = 0.f;
};
