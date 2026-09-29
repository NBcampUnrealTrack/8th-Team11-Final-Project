// CWTrafficStopLine.cpp

#include "CWTrafficStopLine.h"

#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "CWTrafficLane.h"
#include "CWTrafficSubsystem.h"
#include "Traffic/CWTrafficLog.h"

ACWTrafficStopLine::ACWTrafficStopLine()
{
	PrimaryActorTick.bCanEverTick = false;

	// 레벨 배치 액터이고 위치와 차선 참조가 모든 머신에 이미 있다. 복제할 것이 없다.
	bReplicates = false;

	LineMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LineMesh"));
	SetRootComponent(LineMesh);

	// 표시용이므로 차량이나 플레이어와 부딪히면 안 된다.
	LineMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LineMesh->SetGenerateOverlapEvents(false);
}

bool ACWTrafficStopLine::HasLane(const ACWTrafficLane* InLane) const
{
	return InLane && Lanes.Contains(InLane);
}

float ACWTrafficStopLine::GetDistanceAlongLane(const ACWTrafficLane* InLane) const
{
	if (!HasLane(InLane))
	{
		return 0.f;
	}

	return InLane->GetDistanceAtWorldLocation(GetActorLocation());
}

void ACWTrafficStopLine::SnapToLane()
{
	// 기준은 첫 번째 유효한 차선이다. 회전은 어느 차선을 봐도 거의 같다.
	const ACWTrafficLane* ReferenceLane = nullptr;
	int32 ValidLaneCount = 0;

	for (const TObjectPtr<ACWTrafficLane>& Entry : Lanes)
	{
		if (!Entry)
		{
			continue;
		}

		++ValidLaneCount;
		if (!ReferenceLane)
		{
			ReferenceLane = Entry;
		}
	}

	if (!ReferenceLane)
	{
		return;
	}

	const float Distance = ReferenceLane->GetDistanceAtWorldLocation(GetActorLocation());
	const FTransform LaneTransform = ReferenceLane->GetTransformAtDistance(Distance);

	/**
	 * 차선이 하나면 위치까지 끌어당긴다.
	 *
	 * 스냅하지 않으면 액터가 도로 옆에 떠 있어도 정지선은 투영된 지점에 생긴다.
	 * 그러면 보이는 자리와 차가 서는 자리가 달라져서, 값을 맞출 방법이 다시
	 * PIE 를 돌려보는 것밖에 없어진다. 이 액터의 존재 이유가 그것이다.
	 *
	 * 차선이 둘 이상이면 회전만 맞춘다. 여러 차선을 가로지르는 선을 어느 한 차선의
	 * 중심으로 끌어당기면 나머지 차선에서 벗어나고, 가로지르는 위치는 사람이 정하는 값이다.
	 * 차선별 거리는 각자 투영해서 구하므로(GetDistanceAlongLane) 위치를 안 붙여도 정확하다.
	 */
	if (ValidLaneCount == 1)
	{
		// 스케일은 건드리지 않는다. 표시 메시의 크기는 사람이 정하는 값이다.
		SetActorLocationAndRotation(LaneTransform.GetLocation(), LaneTransform.GetRotation());
	}
	else
	{
		SetActorRotation(LaneTransform.GetRotation());
	}
}

void ACWTrafficStopLine::BeginPlay()
{
	Super::BeginPlay();

	if (!Lanes.ContainsByPredicate([](const TObjectPtr<ACWTrafficLane>& Entry) { return Entry != nullptr; }))
	{
		UE_LOG(LogCWTraffic, Warning, TEXT("[%s] Lanes 가 비어 있어 정지선이 동작하지 않는다. 어느 차선의 정지선인지 지정해야 한다."), *GetName());
		return;
	}

	// 등록은 클라이언트에서도 한다. 정지선 판정을 양쪽 차량이 각자 하기 때문이다.
	if (UCWTrafficSubsystem* TrafficSubsystem = UCWTrafficSubsystem::Get(this))
	{
		TrafficSubsystem->RegisterStopLine(this);
	}
}

void ACWTrafficStopLine::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UCWTrafficSubsystem* TrafficSubsystem = UCWTrafficSubsystem::Get(this))
	{
		TrafficSubsystem->UnregisterStopLine(this);
	}

	Super::EndPlay(EndPlayReason);
}

void ACWTrafficStopLine::RefreshLaneReadout()
{
	LaneReadout.Reset();

	for (const TObjectPtr<ACWTrafficLane>& Entry : Lanes)
	{
		/**
		 * 빈 칸은 조용히 넘어가면 안 된다.
		 *
		 * 차선 액터를 지웠다 다시 만들면 여기 참조가 통째로 끊기는데, 배열 길이는 그대로라
		 * 패널만 봐서는 멀쩡해 보인다. 그 차선의 차들은 정지선을 무시하고 지나가지만
		 * 화면에서는 "그냥 지나가는 차" 라 이상해 보이지도 않는다.
		 */
		if (!Entry)
		{
			LaneReadout.Add(TEXT("(비어 있음)   <<< 참조가 끊겼다! 차선을 다시 지정해라"));
			continue;
		}

		const float LaneLength = Entry->GetLaneLength();
		const float Distance = Entry->GetDistanceAtWorldLocation(GetActorLocation());
		const float Percent = (LaneLength > 0.f) ? (Distance / LaneLength) * 100.f : 0.f;

		/**
		 * 0 이나 차선 길이에 붙었으면 스플라인 범위 밖이다.
		 *
		 * 범위 밖 위치는 차선 양 끝으로 잘리는데(GetDistanceAtWorldLocation), 그 지점은
		 * 차가 스폰되는 자리이거나 소멸하는 자리라 실제로는 아무 차도 정지선에 걸리지 않는다.
		 * 마주보는 차선에 배정하면 반드시 이렇게 된다 - 가장 흔한 실수다.
		 */
		FString Line = FString::Printf(TEXT("%s : %.0f / %.0fcm (%.0f%%)"),
			*Entry->GetName(), Distance, LaneLength, Percent);

		/**
		 * 0% 와 100% 는 둘 다 범위 밖이지만 고치는 방향이 정반대다.
		 *
		 * 투영은 스플라인 양 끝으로 잘리므로(GetDistanceAtWorldLocation), 어느 쪽으로
		 * 잘렸는지가 곧 정지선이 차선의 어느 바깥에 있는지를 말해준다.
		 *
		 * 문구는 차선 액터의 화살표를 기준으로 쓴다. 그것이 에디터 화면에서 실제로
		 * 보이는 유일한 방향 표시이고, 화살표와 이 값은 같은 함수에서 나오므로
		 * 어긋날 수가 없다 (ACWTrafficLane::UpdateDirectionArrow).
		 */
		const bool bOutOfRange = (Distance <= KINDA_SMALL_NUMBER)
			|| (LaneLength > 0.f && Distance >= LaneLength - KINDA_SMALL_NUMBER);

		if (bOutOfRange)
		{
			// 얼마나 벗어났는지. 살짝 넘친 것과 아예 딴 데 있는 것을 구분해 준다.
			const FVector PointOnLane = Entry->GetTransformAtDistance(Distance).GetLocation();
			const float OffLane = FVector::Dist(GetActorLocation(), PointOnLane);

			Line += (Distance <= KINDA_SMALL_NUMBER)
				? FString::Printf(TEXT("   <<< 차선 밖 (화살표 꼬리 뒤). %.0fcm 벗어났다. 화살표가 가리키는 쪽으로 옮겨라"), OffLane)
				: FString::Printf(TEXT("   <<< 차선 밖 (화살표 머리 너머). %.0fcm 벗어났다. 차가 이미 사라진 자리다"), OffLane);
		}
		else if (Distance < MinRunway)
		{
			// 정지선까지의 거리가 제동 거리보다 짧으면 차가 감속을 끝내지 못하고 급제동한다.
			Line += FString::Printf(TEXT("   <<< 화살표 꼬리에서 %.0fcm 뿐이라 급제동이 된다 (차량 BP 의 StopLineBrakeDistance 참고)"), Distance);
		}

		/**
		 * 이 차선을 다른 정지선도 물고 있는지.
		 *
		 * 차량은 자기 앞에 있는 정지선 중 **가장 가까운 것**에 선다(FindActiveStopLine).
		 * 그래서 반대 방향 정지선이 실수로 이 차선까지 담고 있으면, 마주보는 차선은
		 * 같은 월드 위치가 훨씬 작은 거리로 잡혀서 그쪽이 이긴다 - 차가 스폰되자마자
		 * 서고 횡단보도까지 한참을 남겨둔다.
		 *
		 * 자기 %만 봐서는 절대 안 보이는 실수다. 이 줄이 없으면 두 정지선 다 40~80%
		 * 로 멀쩡해 보이는데 한쪽만 엉뚱하게 동작한다.
		 */
		if (const UWorld* World = GetWorld())
		{
			FString Others;
			for (TActorIterator<ACWTrafficStopLine> It(World); It; ++It)
			{
				const ACWTrafficStopLine* Other = *It;
				if (Other == this || !Other->HasLane(Entry))
				{
					continue;
				}

				Others += Others.IsEmpty() ? TEXT("") : TEXT(", ");
				Others += Other->GetName();
			}

			if (!Others.IsEmpty())
			{
				Line += FString::Printf(TEXT("   <<< 이 차선을 %s 도 담당한다! 가까운 쪽이 이겨서 엉뚱한 자리에 선다"), *Others);
			}
		}

		LaneReadout.Add(Line);
	}
}

#if WITH_EDITOR
void ACWTrafficStopLine::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	// PIE 와 게임은 이 표시를 안 쓴다. 에디터 월드에서만 건다.
	const UWorld* World = GetWorld();
	if (!World || World->IsGameWorld())
	{
		return;
	}

	/**
	 * 검산 표시는 Transient 라 저장되지 않는다. 레벨을 열 때 다시 만들어야 하는데,
	 * PostLoad 에서는 트랜스폼이 아직 틀린 값이라 엉뚱한 % 와 "차선 밖" 경고가 떴다.
	 * 그 경고를 믿고 옮기면 멀쩡한 정지선이 망가진다.
	 *
	 * 내 컴포넌트가 등록된 이 자리도 이르다. 레벨은 액터를 하나씩 등록하므로 차선이
	 * 나보다 뒤에 있으면 차선 스플라인이 아직 등록 전이다. 순서에 기대지 않으려고
	 * 레벨 전체의 등록이 끝나는 이벤트(ULevel::UpdateLevelComponents 의 끝)를 기다린다.
	 *
	 * 재등록으로 여기가 여러 번 불려도 핸들이 하나라 중복으로 걸리지 않는다.
	 */
	if (!LevelComponentsUpdatedHandle.IsValid())
	{
		LevelComponentsUpdatedHandle = FWorldDelegates::LevelComponentsUpdated.AddUObject(
			this, &ACWTrafficStopLine::HandleLevelComponentsUpdated);
	}

	if (!LevelAddedToWorldHandle.IsValid())
	{
		LevelAddedToWorldHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(
			this, &ACWTrafficStopLine::HandleLevelAddedToWorld);
	}
}

void ACWTrafficStopLine::PostUnregisterAllComponents()
{
	FWorldDelegates::LevelComponentsUpdated.Remove(LevelComponentsUpdatedHandle);
	FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedToWorldHandle);
	LevelComponentsUpdatedHandle.Reset();
	LevelAddedToWorldHandle.Reset();

	Super::PostUnregisterAllComponents();
}

void ACWTrafficStopLine::HandleLevelComponentsUpdated(UWorld* InWorld, ULevel* InLevel)
{
	// 다른 레벨의 등록 이벤트에는 반응하지 않는다. 내 차선은 같은 레벨에 있다.
	if (InLevel == GetLevel())
	{
		RefreshLaneReadout();
	}
}

void ACWTrafficStopLine::HandleLevelAddedToWorld(ULevel* InLevel, UWorld* InWorld)
{
	if (InLevel == GetLevel())
	{
		RefreshLaneReadout();
	}
}

void ACWTrafficStopLine::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// 드래그 중에는 붙이지 않는다. 매 프레임 끌어당기면 옮기기가 힘들다.
	if (bFinished)
	{
		SnapToLane();
	}

	// 표시는 드래그 중에도 갱신한다. 옮기면서 % 가 변하는 것을 보는 것이 목적이다.
	RefreshLaneReadout();
}

void ACWTrafficStopLine::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// 차선을 새로 지정한 순간 바로 그 차선 위로 붙는다.
	// 배열 원소를 바꿀 때는 PropertyName 이 원소 이름으로 오므로 MemberProperty 로 본다.
	const FName MemberName = (PropertyChangedEvent.MemberProperty != nullptr)
		? PropertyChangedEvent.MemberProperty->GetFName()
		: PropertyChangedEvent.GetPropertyName();

	if (MemberName == GET_MEMBER_NAME_CHECKED(ACWTrafficStopLine, Lanes))
	{
		SnapToLane();
	}

	RefreshLaneReadout();
}
#endif
