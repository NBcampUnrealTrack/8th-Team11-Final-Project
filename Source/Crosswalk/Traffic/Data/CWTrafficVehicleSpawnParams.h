// CWTrafficVehicleSpawnParams.h

#pragma once

#include "CoreMinimal.h"
#include "CWTrafficVehicleSpawnParams.generated.h"

class UDataTable;

/**
 * 스포너가 차량 1대를 굴려보낼 때 넘기는 개체별 값.
 * 차종 고유 값(기본 속도, 패링 가능 여부)은 차량 BP가 들고, 여기 있는 건 개체마다 흔들리는 값만이다.
 */
USTRUCT(BlueprintType)
struct FCWTrafficVehicleSpawnParams
{
	GENERATED_BODY()

	/** 차선 시작점에서의 초기 진행 거리(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	float StartDistance = 0.f;

	/**
	 * 차종 기본 속도에 곱할 배율. 1.0이면 BP에 설정된 속도 그대로.
	 *
	 * 스포너가 정하지 않는다. 차량이 외형 변형을 뽑은 뒤 그 차체의 SpeedType 으로
	 * 아래 표를 찾아 스스로 채운다 - 어느 차체가 뽑힐지는 추첨 전에는 알 수 없기 때문이다.
	 */
	float SpeedMultiplier = 1.f;

	/**
	 * 차종별 속도 범위표(FCWVehicleSpeedRow). 비어 있으면 모든 차가 BP 의 Speed 로 달린다.
	 *
	 * 표를 참조만 넘기고 추첨은 차량이 한다. 스포너가 미리 뽑아 두려면 어느 차체가
	 * 나올지 먼저 알아야 하는데, 그 추첨은 복제 때문에 차량 쪽(InitializeOnLane)에 있다.
	 */
	UPROPERTY()
	TObjectPtr<UDataTable> SpeedTable;

	/** 차선 중심선에서 좌우로 밀어낼 거리(cm). 양수면 진행 방향 기준 오른쪽. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	float LateralOffset = 0.f;
};
