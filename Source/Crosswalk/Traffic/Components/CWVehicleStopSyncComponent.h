// Copyright Crosswalk Team. All Rights Reserved.
// CWVehicleStopSyncComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CWVehicleStopSyncComponent.generated.h"

class ACWBaseVehicle;

/**
 * 정지선 앞에 선 차량의 서버-클라 위치 오차를 좁힌다. (복제·판정 담당)
 *
 * ACWBaseVehicle 은 신호 지연으로 생긴 오차를 스스로 못 지운다 - 정지 중에는
 * DistanceAlongLane 이 안 바뀌어 리플리케이션이 다시 안 오기 때문이다(OnStopStateChanged
 * 주석 참고). 그 값을 실제로 쓰는 이 컴포넌트를 트래픽 쪽 파일과 분리해 두면, 차량
 * 자체의 이동/판정 로직과 "서버-클라를 맞추는" 로직이 서로 다른 파일에 남는다.
 *
 * ACWBaseVehicle 은 OnStopStateChanged/GetStopTargetDistance/AdjustDistanceAlongLane
 * 만 열어 주고, 오차를 스냅할지 글라이드할지 같은 판단은 전부 여기서 한다.
 *
 * 매 틱 대신 타이머로 돈다. 어차피 완만하게 좁히는 보정이라 초당 수십 번 계산할 필요가
 * 없고, 정지 중인 차량만 도니 부하도 작다.
 *
 * **HasAuthority 로 막지 않는다. 서버·클라이언트 양쪽에서 도는 것이 의도다.**
 *
 * 트랜스폼을 복제하지 않는 구조라, 클라이언트 화면에 보이는 차량 위치는 서버가 보낸 값이
 * 아니라 클라이언트 자신이 계산한 DistanceAlongLane 이 만든다. 서버에서만 돌리면 정작
 * 고쳐야 할 화면이 안 고쳐져서 이 컴포넌트를 만든 의미가 없어진다.
 *
 * 서버에서 도는 것도 해롭지 않다. 감속이 목표 지점에 수렴하는 식이라 서버가 설 때의
 * 잔차는 0 에 가깝고([TrafficSync] 정지 로그의 오차 항목), 밀 것이 없으면 아무 일도 안 한다.
 *
 * 치팅 통로도 아니다. 충돌·패링 판정은 서버가 서버 자신의 DistanceAlongLane 으로 따로
 * 계산하므로 클라이언트가 이 값을 흔들어도 판정이 바뀌지 않고, 차가 다시 움직이기
 * 시작하면 복제가 다시 흘러 클라이언트 값을 서버 값으로 덮는다.
 */
UCLASS(ClassGroup = (Crosswalk), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWVehicleStopSyncComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWVehicleStopSyncComponent();

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleStopStateChanged(ACWBaseVehicle* Vehicle, bool bNowStopped, float TargetDistance);

	/** 타이머 주기마다 실행. 오차를 확인하고 스냅하거나 글라이드한다. */
	void RunCorrection();

private:
	UPROPERTY(Transient)
	TObjectPtr<ACWBaseVehicle> OwnerVehicle;

	FTimerHandle CorrectionTimerHandle;

public:
	/** 이 주기(초)마다 보정을 시도한다. 매 틱 대신 낮은 주기로 돌려 부하를 줄인다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle", meta = (ClampMin = "0.01"))
	float CorrectionInterval = 0.1f;

	/**
	 * 오차가 이 값(cm) 이하면 그대로 확정(스냅)한다. 0 이면 스냅을 아예 안 쓰고 항상
	 * 글라이드만 한다.
	 *
	 * 기본값을 0 으로 둔 이유: 차량은 정지선 앞에서 이미 부드럽게 감속해서 선다.
	 * 그 직후에 순간이동이 섞이면, 방금까지 매끄럽던 정지 동작과 대비돼 오히려
	 * 더 눈에 띌 수 있다. 테스트해서 작은 오차의 스냅이 안 보이면 이 값을 올려도 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0"))
	float SnapTolerance = 0.f;

	/** 허용 오차를 넘는(또는 스냅을 안 쓰는) 나머지를 목표 지점 쪽으로 좁히는 속도(cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0"))
	float CorrectionSpeed = 100.f;
};
