// Copyright Crosswalk Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CWTrafficStopLine.generated.h"

class ACWTrafficLane;
class UStaticMeshComponent;

/**
 * 차선 위의 정지선. 빨간불이면 차가 이 앞에 줄을 선다.
 *
 * 왜 액터인가
 *   정지선은 차선 위 거리(cm) 하나로 표현되지만, 사람에게는 "횡단보도 앞" 이라는
 *   월드의 한 지점이다. 숫자로 받으면 에디터에서 볼 수도 잴 수도 없어서 값을 맞출
 *   방법이 PIE 를 돌려보는 것밖에 없다. 액터로 두면 놓은 자리가 곧 정지선이다.
 *
 * 스플라인에 자동으로 붙는다
 *   에디터에서 옮기면 지정한 차선 위의 가장 가까운 지점으로 스냅되고, 회전도 주행
 *   방향에 맞춘다. 그래서 **보이는 자리와 차가 서는 자리가 항상 같다** - 투영 결과를
 *   상상할 필요가 없다.
 *
 * 차선 하나에 정지선 하나다
 *   신호 페이즈가 아니라 차선에 붙는다. 같은 차선이 여러 페이즈에 걸쳐 있어도
 *   차가 서는 자리는 하나이기 때문이다. 그래서 제어기의 Phases 배열을 파고들지 않고
 *   이 액터를 놓고 차선만 지정하면 배선이 끝난다.
 *
 * 신호가 없으면 아무것도 하지 않는다
 *   이 액터는 "여기가 정지선이다" 만 안다. 지금 빨간불인지는 그 차선을 통제하는
 *   ACWTrafficDirector 가 정한다. 제어기가 없는 차선에 놓으면 차가 그냥 지나간다.
 *
 * 네트워크 - 복제하지 않는다.
 *   레벨에 배치된 액터이므로 위치와 차선 참조가 모든 머신에 이미 있다.
 *   차량은 각자 이 값을 읽어 같은 감속을 계산한다.
 */
UCLASS()
class CROSSWALK_API ACWTrafficStopLine : public AActor
{
	GENERATED_BODY()

public:
	ACWTrafficStopLine();

	/** 이 정지선이 담당하는 차선들. */
	const TArray<TObjectPtr<ACWTrafficLane>>& GetLanes() const { return Lanes; }

	/** 이 차선을 담당하는지. */
	UFUNCTION(BlueprintPure, Category = "Traffic|StopLine")
	bool HasLane(const ACWTrafficLane* InLane) const;

	/**
	 * 이 정지선이 InLane 위에서 몇 cm 지점인지. 담당하지 않는 차선이면 0.
	 *
	 * 차선마다 따로 계산한다. 여러 차선을 가로지르는 선은 각 차선을 조금씩 다른
	 * 지점에서 지나가므로(특히 커브에서), 하나의 값으로 뭉치면 어긋난다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|StopLine")
	float GetDistanceAlongLane(const ACWTrafficLane* InLane) const;

	/**
	 * 차선 위로 스냅한다.
	 *
	 * 차선이 하나면 위치와 회전을 모두 그 차선에 맞춘다 - 보이는 자리가 곧 정지선이 된다.
	 * 둘 이상이면 **회전만** 맞춘다. 여러 차선을 가로지르는 선은 어느 한 차선의
	 * 중심으로 끌어당기면 안 되고, 가로지르는 위치는 사람이 정하는 값이다.
	 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Traffic|StopLine")
	void SnapToLane();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostLoad() override;

	/**
	 * 차선별 검산 표시를 다시 만든다.
	 *
	 * 정지선이 제대로 놓였는지는 "차선 위 몇 cm 인가" 로만 알 수 있는데, 그 값이
	 * 에디터 어디에도 안 보여서 PIE 를 돌려 로그를 읽는 것이 유일한 확인 방법이었다.
	 * 옮길 때마다 그러는 것은 작업 절차가 못 된다.
	 */
	void RefreshLaneReadout();

#if WITH_EDITOR
	/** 에디터에서 옮기면 차선 위로 다시 붙인다. 보이는 자리가 곧 정지선이어야 한다. */
	virtual void PostEditMove(bool bFinished) override;

	/** 차선을 새로 지정했을 때도 붙인다. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	/**
	 * 이 정지선이 담당하는 차선들. 하나 이상 지정해야 한다.
	 *
	 * 정지선은 물리적으로 여러 차선을 가로지르는 하나의 선이므로, 같은 방향 차선들을
	 * 한 액터에 묶는 것이 실제 도로와 같다. 왕복 4차선이면 방향마다 하나씩 액터 2개다.
	 *
	 * 반대 방향 차선까지 한 액터에 넣지 않는다 - 진행 방향이 반대라 같은 선이
	 * 한쪽에서는 정지선이고 다른 쪽에서는 이미 지난 지점이 된다.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Traffic|StopLine")
	TArray<TObjectPtr<ACWTrafficLane>> Lanes;

	/**
	 * 정지선 표시. 메시를 비워두면 아무것도 그리지 않는다.
	 *
	 * 도로에 그려진 흰 선을 원하면 얇게 눌린 큐브를 넣는다. 배치용 표시로만 쓰고
	 * 게임에서는 감추려면 이 컴포넌트의 Hidden in Game 을 켠다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|StopLine")
	TObjectPtr<UStaticMeshComponent> LineMesh;

	/**
	 * [검산] 담당 차선마다 이 정지선이 어디에 잡히는지. 옮기면 바로 갱신된다.
	 *
	 * `차선이름 : 1050 / 2340cm (45%)` 형태다. 40~50% 정도면 정상이고,
	 * `범위 밖` 이 붙으면 이 차선의 스플라인 밖에 있다는 뜻이다 - 그러면 어떤 차도
	 * 정지선에 도달하지 못한다. 반대 방향 차선에 배정했을 때 그렇게 된다.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Traffic|StopLine")
	TArray<FString> LaneReadout;

	/**
	 * 검산에 쓰는 최소 진입 거리(cm). 차선 시작점에서 정지선까지 이만큼은 있어야 한다.
	 *
	 * 차는 정지선 앞에서 StopLineBrakeDistance(기본 900cm)에 걸쳐 속도를 줄인다.
	 * 진입 구간이 그보다 짧으면 감속을 다 끝내기 전에 정지선에 닿아서 급제동이 된다.
	 * 게임 로직은 아니고 배치할 때 경고를 띄우기 위한 기준값이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|StopLine", meta = (ClampMin = "0.0"))
	float MinRunway = 900.f;
};
