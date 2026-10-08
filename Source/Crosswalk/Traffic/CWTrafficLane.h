// CWTrafficLane.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CWTrafficLane.generated.h"

class UArrowComponent;
class USplineComponent;

/**
 * 차량이 따라 달리는 단일 차선.
 * 스플라인의 시작점 -> 끝점 방향이 곧 주행 방향이다.
 * 왕복 2차선은 이 액터를 2개 배치해 스플라인 방향을 서로 반대로 잡는다.
 */
UCLASS()
class CROSSWALK_API ACWTrafficLane : public AActor
{
	GENERATED_BODY()

public:
	ACWTrafficLane();

	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(BlueprintPure, Category = "Traffic|Lane")
	float GetLaneLength() const;

	/** 시작점에서 Distance(cm)만큼 진행한 지점의 트랜스폼. 회전은 진행 방향을 바라본다. */
	UFUNCTION(BlueprintPure, Category = "Traffic|Lane")
	FTransform GetTransformAtDistance(float Distance) const;

	/**
	 * 월드 위치에서 가장 가까운 차선 위 지점의 진행 거리(cm).
	 *
	 * "이 자리는 차선 위 몇 cm 인가" 를 묻는 창구다. 정지선처럼 사람이 월드에서
	 * 위치로 잡는 값을 차선 거리로 바꿀 때 쓴다 - 스플라인 누적거리는 에디터에서
	 * 볼 수도 잴 수도 없어서 사람이 직접 넣을 수 있는 값이 아니다.
	 *
	 * 차선에서 멀리 떨어진 위치를 넣어도 가장 가까운 지점으로 잡히므로,
	 * 대충 근처에 놓기만 하면 된다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Lane")
	float GetDistanceAtWorldLocation(const FVector& WorldLocation) const;

protected:
	virtual void BeginPlay() override;

private:
	/** 진행 거리를 스플라인 누적거리로 바꾼다. 역방향이면 반대쪽 끝에서 잰다. */
	float ToSplineDistance(float Distance) const;

#if WITH_EDITORONLY_DATA
	/**
	 * 화살표를 주행 시작점으로 옮기고 진행 방향을 바라보게 한다.
	 *
	 * "어느 끝이 시작점인가" 가 이 클래스의 가장 오래된 함정이다. 그 값이 화면에
	 * 안 보이면 정지선을 놓을 때마다 상상으로 맞춰야 하고, 틀리면 증상이
	 * "차가 안 선다" 로만 보여서 원인에 도달하지 못한다. 실제로 그렇게 이틀을 썼다.
	 */
	void UpdateDirectionArrow();
#endif

	/** 검산 표시를 다시 만든다. 스케일이 1이 아니면 경고를 붙인다. */
	void RefreshLaneReadout();

protected:
	/** 차선 경로. 포인트를 편집해 차선 모양을 잡는다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Lane")
	TObjectPtr<USplineComponent> LaneSpline;

	/**
	 * 스플라인을 끝점 -> 시작점 방향으로 달릴지.
	 *
	 * 왜 필요한가
	 *   왕복 차선은 방향이 반대인 스플라인 두 개인데, 어느 끝이 시작점인지가 에디터
	 *   화면에 드러나지 않는다. 그래서 "차가 어디서 나오는가" 를 상상으로 맞춰야 하고,
	 *   틀리면 정지선이 반대편에 잡혀서 차가 안 서거나 엉뚱한 자리에 선다.
	 *   증상이 원인을 안 알려주는 종류라 찾는 데 오래 걸린다.
	 *
	 * 액터를 180도 돌려서 해결하면 안 된다
	 *   스플라인 컴포넌트가 루트라 액터 회전은 **경로 자체를 물리적으로 회전시킨다.**
	 *   시작점은 여전히 포인트 0 이므로 방향은 그대로이고, 피벗이 경로 중앙이 아니면
	 *   차선이 도로 밖으로 날아간다. 우연히 맞아떨어지는 배치도 있어서 더 위험하다.
	 *
	 * 이 값을 켜면 경로는 그대로 두고 **읽는 방향만** 뒤집는다. 스폰 위치, 정지선 거리,
	 * 차량 회전이 한꺼번에 따라오므로 배치를 다시 만질 필요가 없다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Lane")
	bool bReverseDirection = false;

#if WITH_EDITORONLY_DATA
	/**
	 * 주행 시작점에 서서 진행 방향을 가리키는 화살표. 에디터에서만 보인다.
	 *
	 * 화살표 꼬리가 차가 나오는 자리이고, 머리가 차가 가는 쪽이다.
	 * bReverseDirection 을 켜면 반대쪽 끝으로 옮겨간다.
	 */
	UPROPERTY()
	TObjectPtr<UArrowComponent> DirectionArrow;
#endif

	/**
	 * [검산] 이 차선의 길이와 스케일. 값을 고치면 바로 갱신된다.
	 *
	 * 스케일 경고가 여기 뜬다. 차선을 늘릴 때 스플라인 포인트를 옮기는 대신 액터를
	 * 스케일하면 화면에서는 똑같아 보이는데 코드가 보는 길이는 안 늘어난다.
	 * 그 상태로는 정지선을 아무리 옮겨도 맞출 수 없다.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Traffic|Lane")
	FString LaneReadout;
};
