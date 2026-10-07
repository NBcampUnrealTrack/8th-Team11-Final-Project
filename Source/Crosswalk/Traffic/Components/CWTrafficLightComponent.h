// CWTrafficLightComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Traffic/Data/CWTrafficSignalState.h"
#include "CWTrafficLightComponent.generated.h"

class ACWTrafficDirector;

/**
 * 신호 그룹의 색을 화면에 보여주는 컴포넌트.
 *
 * 붙이면 BP 그래프를 짜지 않아도 동작한다 - 소유 액터의 컴포넌트 중 지정한 태그를 가진
 * 것들을 찾아 색에 맞게 껐다 켠다. 그래서 신호등 BP 는 큐브 3개에 태그만 달면 끝이다.
 *
 * 그룹 하나를 그대로 비춘다
 *   이 컴포넌트는 판단을 하지 않는다. 무슨 색이어야 하는지는 전부 제어기가 정하고,
 *   여기서는 지정한 그룹의 색을 그대로 켠다. 보행 신호도 마찬가지다 - 차량 신호를
 *   뒤집는 것이 아니라 **보행 그룹을 따로 만들고** 그 색을 비춘다. 뒤집기로 만들면
 *   전환 구간(양쪽 다 적색)을 표현할 수 없다.
 *
 * 어디에 붙이나
 *   ① 별도 BP_TrafficLight 액터 - 방향마다 신호등이 필요할 때
 *   ② BP_TrafficDirector 자신 - 신호 상태가 하나뿐일 때
 *   두 경우 모두 제어기를 자동으로 찾으므로 배선이 없다 (ResolveDirector).
 *
 * 네트워크 - 복제하지 않는다.
 *   제어기의 그룹 색이 복제되므로 각 머신이 알아서 같은 색을 만든다.
 */
UCLASS(ClassGroup = (Crosswalk), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWTrafficLightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWTrafficLightComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/**
	 * 쓸 제어기를 정한다. 명시 -> 소유 액터 -> 레벨에 하나뿐이면 그것 순서로 찾는다.
	 *
	 * 레벨에 제어기가 둘 이상이면 자동으로 고르지 않는다. 엉뚱한 교차로의 신호를
	 * 따라가는 것이 조용히 굴러가는 것보다 나쁘다.
	 */
	void ResolveDirector();

	UFUNCTION()
	void HandleGroupStateChanged(ACWTrafficDirector* InDirector, FName InGroupName, ECWTrafficSignalState NewState, ECWTrafficSignalState OldState);

	/** 색이 바뀌었으면 태그 메시를 갱신하고 델리게이트를 쏜다. */
	void ApplyState(ECWTrafficSignalState NewState);

	/** 소유 액터의 컴포넌트 중 이 태그를 가진 것들의 표시를 켜거나 끈다. */
	void SetTaggedComponentsVisible(FName Tag, bool bVisible) const;

	/** 이 태그가 달린 씬 컴포넌트 개수. */
	int32 CountTaggedComponents(FName Tag) const;

	/**
	 * 태그가 하나도 안 걸린 색이 있으면 경고한다.
	 *
	 * 태그를 빼먹으면 코드는 정상 동작하는데 화면에서는 아무 일도 일어나지 않는다.
	 * 가장 흔한 배치 실수이고 증상이 "신호등이 안 바뀐다" 로만 보여서 원인을 찾기 어렵다.
	 */
	void ValidateTaggedComponents() const;

protected:
	/**
	 * 이 신호등이 비출 신호 그룹의 이름. 제어기의 Groups 에 있는 이름과 같아야 한다.
	 *
	 * 차량 신호등이면 그 방향 그룹(예: 남북직진), 보행 신호등이면 보행 그룹을 적는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Light")
	FName GroupName;

	/**
	 * 따라갈 신호 제어기. 비우면 자동으로 찾는다 (ResolveDirector).
	 * 레벨에 교차로가 둘 이상이면 반드시 지정해야 한다.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Traffic|Light")
	TObjectPtr<ACWTrafficDirector> Director;

	/** 이 태그가 달린 컴포넌트를 적색일 때만 보여준다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Light|Tags")
	FName RedComponentTag = TEXT("Red");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Light|Tags")
	FName YellowComponentTag = TEXT("Yellow");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Light|Tags")
	FName GreenComponentTag = TEXT("Green");

private:
	ECWTrafficSignalState CurrentState = ECWTrafficSignalState::Off;
};
