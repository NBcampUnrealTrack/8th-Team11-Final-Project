// Copyright Crosswalk Team. All Rights Reserved.

#include "CWTrafficLightComponent.h"

#include "EngineUtils.h"
#include "Components/SceneComponent.h"
#include "Traffic/CWTrafficDirector.h"
#include "Traffic/CWTrafficLog.h"

UCWTrafficLightComponent::UCWTrafficLightComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCWTrafficLightComponent::BeginPlay()
{
	Super::BeginPlay();

	ResolveDirector();

	if (!Director)
	{
		UE_LOG(LogCWTraffic, Warning, TEXT("[%s] 따라갈 ACWTrafficDirector를 찾지 못해 신호등이 꺼진 채로 남는다."),
			*GetReadableName());
		return;
	}

	if (GroupName.IsNone())
	{
		UE_LOG(LogCWTraffic, Warning, TEXT("[%s] GroupName 이 비어 있어 어느 신호를 비출지 알 수 없다. 꺼진 채로 남는다."),
			*GetReadableName());
		return;
	}

	ValidateTaggedComponents();

	// 계획 검산(정지선 배선·경로 교차)은 제어기가 자기 BeginPlay 에서 한 번 한다. 여기서 부르면 신호등 수만큼 반복된다.

	UE_LOG(LogCWTraffic, Log, TEXT("[%s] 신호등 준비. 제어기 %s / 그룹 '%s'"),
		*GetReadableName(), *Director->GetName(), *GroupName.ToString());

	Director->OnGroupStateChanged.AddUniqueDynamic(this, &UCWTrafficLightComponent::HandleGroupStateChanged);

	/**
	 * 바인딩만 하면 이미 지나간 전환은 받지 못한다.
	 *
	 * 제어기는 다음 틱에 시작하므로 보통은 첫 전환을 받는다. 그러나 늦게 접속한
	 * 클라이언트나 레벨 스트리밍으로 나중에 생긴 신호등은 이미 돌고 있는 신호에
	 * 끼어드는 것이라 첫 전환이 오지 않는다. 그래서 현재 색을 한 번 직접 읽는다.
	 */
	ApplyState(Director->GetGroupState(GroupName));
}

void UCWTrafficLightComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Director)
	{
		Director->OnGroupStateChanged.RemoveDynamic(this, &UCWTrafficLightComponent::HandleGroupStateChanged);
	}

	Super::EndPlay(EndPlayReason);
}

void UCWTrafficLightComponent::ResolveDirector()
{
	if (Director)
	{
		return;
	}

	// 신호등을 제어기 BP 안에 넣은 배치. 신호 상태가 하나뿐일 때 쓴다.
	if (ACWTrafficDirector* OwnerDirector = Cast<ACWTrafficDirector>(GetOwner()))
	{
		Director = OwnerDirector;
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 레벨에 제어기가 하나뿐이면 그것을 쓴다. 둘 이상이면 자동으로 고르지 않는다 -
	// 엉뚱한 교차로의 신호를 따라가는 것이 조용히 굴러가는 것보다 나쁘다.
	ACWTrafficDirector* FoundDirector = nullptr;
	for (TActorIterator<ACWTrafficDirector> It(World); It; ++It)
	{
		if (FoundDirector)
		{
			UE_LOG(LogCWTraffic, Warning, TEXT("[%s] 레벨에 ACWTrafficDirector가 둘 이상이라 자동으로 고르지 않는다. Director를 직접 지정해야 한다."),
				*GetReadableName());
			return;
		}

		FoundDirector = *It;
	}

	Director = FoundDirector;
}

void UCWTrafficLightComponent::HandleGroupStateChanged(ACWTrafficDirector* /*InDirector*/, FName InGroupName,
	ECWTrafficSignalState NewState, ECWTrafficSignalState /*OldState*/)
{
	// 제어기는 모든 그룹의 변화를 한 델리게이트로 쏜다. 내 그룹만 골라 받는다.
	if (InGroupName != GroupName)
	{
		return;
	}

	ApplyState(NewState);
}

void UCWTrafficLightComponent::ApplyState(ECWTrafficSignalState NewState)
{
	if (NewState == CurrentState)
	{
		return;
	}

	const ECWTrafficSignalState OldState = CurrentState;
	CurrentState = NewState;

	SetTaggedComponentsVisible(RedComponentTag, NewState == ECWTrafficSignalState::Red);
	SetTaggedComponentsVisible(YellowComponentTag, NewState == ECWTrafficSignalState::Yellow);
	SetTaggedComponentsVisible(GreenComponentTag, NewState == ECWTrafficSignalState::Green);

	UE_LOG(LogCWTraffic, Log, TEXT("[%s] %s -> %s"),
		*GetReadableName(),
		*ACWTrafficDirector::GetSignalStateText(OldState).ToString(),
		*ACWTrafficDirector::GetSignalStateText(NewState).ToString());

	OnLightStateChanged.Broadcast(this, NewState, OldState);
}

int32 UCWTrafficLightComponent::CountTaggedComponents(FName Tag) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || Tag.IsNone())
	{
		return 0;
	}

	int32 Count = 0;
	for (UActorComponent* Component : Owner->GetComponents())
	{
		const USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
		if (SceneComponent && SceneComponent->ComponentHasTag(Tag))
		{
			++Count;
		}
	}

	return Count;
}

void UCWTrafficLightComponent::ValidateTaggedComponents() const
{
	const int32 RedCount = CountTaggedComponents(RedComponentTag);
	const int32 YellowCount = CountTaggedComponents(YellowComponentTag);
	const int32 GreenCount = CountTaggedComponents(GreenComponentTag);

	if (RedCount + YellowCount + GreenCount == 0)
	{
		UE_LOG(LogCWTraffic, Warning,
			TEXT("[%s] 태그가 걸린 컴포넌트가 하나도 없다. 신호가 바뀌어도 화면에서 아무 일도 일어나지 않는다. 메시 컴포넌트의 Component Tags 에 %s / %s / %s 를 넣어야 한다."),
			*GetReadableName(), *RedComponentTag.ToString(), *YellowComponentTag.ToString(), *GreenComponentTag.ToString());
		return;
	}

	if (RedCount == 0)
	{
		UE_LOG(LogCWTraffic, Warning, TEXT("[%s] 태그 %s 가 달린 컴포넌트가 없어 적색이 안 보인다."), *GetReadableName(), *RedComponentTag.ToString());
	}

	if (GreenCount == 0)
	{
		UE_LOG(LogCWTraffic, Warning, TEXT("[%s] 태그 %s 가 달린 컴포넌트가 없어 초록이 안 보인다."), *GetReadableName(), *GreenComponentTag.ToString());
	}

	// 황색은 전환 구간에 자동으로 켜지지 않는다. 단계에서 직접 지정할 때만 쓰이므로 경고가 아니다.
	if (YellowCount == 0)
	{
		UE_LOG(LogCWTraffic, Log, TEXT("[%s] 태그 %s 가 없다. 단계에서 황색을 지정하지 않는다면 정상이다."),
			*GetReadableName(), *YellowComponentTag.ToString());
	}
}

void UCWTrafficLightComponent::SetTaggedComponentsVisible(FName Tag, bool bVisible) const
{
	if (Tag.IsNone())
	{
		return;
	}

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// 태그로 찾는 이유: 디테일 패널에서는 형제 컴포넌트를 참조로 지정할 수 없다.
	// 태그를 쓰면 큐브에 태그만 달면 되고 BP 그래프가 필요 없다.
	for (UActorComponent* Component : Owner->GetComponents())
	{
		USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
		if (!SceneComponent || !SceneComponent->ComponentHasTag(Tag))
		{
			continue;
		}

		SceneComponent->SetVisibility(bVisible, /*bPropagateToChildren*/ true);
	}
}
