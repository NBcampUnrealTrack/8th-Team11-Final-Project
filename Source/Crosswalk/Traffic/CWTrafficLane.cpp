// Copyright Crosswalk Team. All Rights Reserved.

#include "CWTrafficLane.h"

#include "Components/ArrowComponent.h"
#include "Components/SplineComponent.h"
#include "Traffic/CWTrafficLog.h"

ACWTrafficLane::ACWTrafficLane()
{
	PrimaryActorTick.bCanEverTick = false;

	LaneSpline = CreateDefaultSubobject<USplineComponent>(TEXT("LaneSpline"));
	SetRootComponent(LaneSpline);

	// 차선은 닫힌 경로가 아니다. 시작점에서 끝점까지 한 번 주행한 뒤 디스폰된다.
	LaneSpline->SetClosedLoop(false);

	/**
	 * 선택하지 않아도 스플라인이 보이게 한다. (뷰포트 Show > Splines 가 켜져 있을 때)
	 *
	 * 기본값이면 액터를 선택했을 때만 경로가 그려진다. 그래서 화면에는 도로 메시에
	 * 그려진 차선 도색만 보이는데, 코드가 보는 것은 스플라인이라 둘이 어긋나 있어도
	 * 알 수가 없다. 실제로 스플라인이 도로보다 훨씬 짧은 상태로 한참을 헤맸다.
	 *
	 * 정지선 % 가 이상할 때 제일 먼저 볼 것이 "스플라인이 도로 위에 제대로 깔렸는가" 인데,
	 * 그걸 보려면 매번 차선을 하나씩 선택해야 한다면 그건 확인 절차가 못 된다.
	 */
	LaneSpline->bDrawDebug = true;

#if WITH_EDITORONLY_DATA
	DirectionArrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("DirectionArrow"));
	if (DirectionArrow)
	{
		DirectionArrow->SetupAttachment(LaneSpline);
		DirectionArrow->SetArrowColor(FLinearColor(0.1f, 1.f, 0.3f));
		DirectionArrow->ArrowSize = 6.f;
		DirectionArrow->bIsScreenSizeScaled = true;
		DirectionArrow->SetHiddenInGame(true);

		// 클릭으로 잡히면 차선을 고르려다 화살표가 선택된다. 보기만 하는 표시다.
		DirectionArrow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
#endif
}

void ACWTrafficLane::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

#if WITH_EDITORONLY_DATA
	// 스플라인 포인트를 옮기든 액터를 회전하든 bReverseDirection 을 켜든 여기를 지나간다.
	UpdateDirectionArrow();
#endif

	RefreshLaneReadout();
}

void ACWTrafficLane::BeginPlay()
{
	Super::BeginPlay();

	const FVector Scale = GetActorScale3D();
	if (!Scale.Equals(FVector::OneVector, 0.01f))
	{
		UE_LOG(LogCWTraffic, Error,
			TEXT("[%s] 액터 스케일이 %s 다. 차선은 스케일하면 안 된다 - 길이(%.0fcm)는 로컬 값이라 스케일이 반영되지 않는데, 속도·차간거리·정지선은 전부 월드 cm 라 그만큼 어긋난다. 스플라인 포인트를 옮겨 길이를 잡아야 한다."),
			*GetName(), *Scale.ToString(), GetLaneLength());
	}
}

void ACWTrafficLane::RefreshLaneReadout()
{
	const FVector Scale = GetActorScale3D();

	LaneReadout = FString::Printf(TEXT("길이 %.0fcm / 스케일 %.2f, %.2f, %.2f"),
		GetLaneLength(), Scale.X, Scale.Y, Scale.Z);

	/**
	 * 스케일은 길이에 반영되지 않는다.
	 *
	 * USplineComponent::GetSplineLength 는 로컬 공간 길이를 낸다 (Scale3D 인자를 받는
	 * GetSegmentLength 와 달리 스케일을 곱하지 않는다). 그래서 액터를 2배로 늘리면
	 * 화면의 도로는 2배가 되는데 코드가 아는 길이는 그대로다.
	 *
	 * 차량의 진행 거리도 로컬 단위로 쌓이므로, 속도 750(월드 cm/s)이 실제로는
	 * 750 x 스케일 만큼 나간다. 차간 거리·정지선 간격·제동 거리가 전부 같은 배수로
	 * 어긋나고, 증상은 "정지선이 이상하다" 로만 보인다.
	 */
	if (!Scale.Equals(FVector::OneVector, 0.01f))
	{
		LaneReadout += TEXT("   <<< 스케일이 1이 아니다! 길이에 반영되지 않아 속도·정지선이 전부 어긋난다. 스케일을 1로 되돌리고 스플라인 포인트로 길이를 잡아라");
	}
}

#if WITH_EDITORONLY_DATA
void ACWTrafficLane::UpdateDirectionArrow()
{
	if (!DirectionArrow || !LaneSpline)
	{
		return;
	}

	/**
	 * 거리 0 의 트랜스폼이 곧 "차가 나오는 자리와 바라보는 방향" 이다.
	 *
	 * GetTransformAtDistance 가 bReverseDirection 까지 처리하므로 여기서 방향을 다시
	 * 따질 필요가 없다. 차량이 스폰될 때 쓰는 것과 **같은 함수**라서, 화살표가 가리키는
	 * 것과 실제 차가 나오는 것이 어긋날 수가 없다.
	 */
	const FTransform StartTransform = GetTransformAtDistance(0.f);

	DirectionArrow->SetWorldLocationAndRotation(StartTransform.GetLocation(), StartTransform.GetRotation());
}
#endif

float ACWTrafficLane::GetLaneLength() const
{
	return LaneSpline ? LaneSpline->GetSplineLength() : 0.f;
}

float ACWTrafficLane::ToSplineDistance(float Distance) const
{
	// 역방향이면 반대쪽 끝에서부터 잰다. 경로는 그대로 두고 읽는 방향만 뒤집는 것이다.
	return bReverseDirection ? FMath::Max(0.f, GetLaneLength() - Distance) : Distance;
}

FTransform ACWTrafficLane::GetTransformAtDistance(float Distance) const
{
	// 스플라인이 없으면 액터 위치로 대체해 스폰이 원점으로 튀는 것만 막는다.
	if (!LaneSpline)
	{
		return GetActorTransform();
	}

	FTransform Result = LaneSpline->GetTransformAtDistanceAlongSpline(ToSplineDistance(Distance), ESplineCoordinateSpace::World);

	if (bReverseDirection)
	{
		/**
		 * 위치는 그대로 두고 바라보는 쪽만 뒤집는다.
		 *
		 * 월드 Z 축으로 180도 돌린다. 로테이터의 Yaw 에 180 을 더하는 것과 달리
		 * 경사로처럼 피치가 섞인 구간에서도 어긋나지 않는다.
		 */
		Result.SetRotation(FQuat(FVector::UpVector, PI) * Result.GetRotation());
	}

	return Result;
}

FVector ACWTrafficLane::GetDirectionAtDistance(float Distance) const
{
	if (!LaneSpline)
	{
		return GetActorForwardVector();
	}

	const FVector Direction = LaneSpline->GetDirectionAtDistanceAlongSpline(ToSplineDistance(Distance), ESplineCoordinateSpace::World);

	return bReverseDirection ? -Direction : Direction;
}

float ACWTrafficLane::GetDistanceAtWorldLocation(const FVector& WorldLocation) const
{
	if (!LaneSpline)
	{
		return 0.f;
	}

	// 스플라인 파라미터(입력 키)로 먼저 잡은 뒤 누적 거리로 바꾼다.
	// 곡선 구간에서도 실제 주행 거리 기준이 되어야 정지선이 눈에 보이는 자리와 맞는다.
	const float InputKey = LaneSpline->FindInputKeyClosestToWorldLocation(WorldLocation);
	const float Distance = LaneSpline->GetDistanceAlongSplineAtSplineInputKey(InputKey);

	/**
	 * 차선 범위로 잘라낸다.
	 *
	 * 차선 밖의 위치를 넣으면 스플라인 끝을 넘는 값이 나올 수 있고, 그 값은 어떤 차도
	 * 도달할 수 없는 지점이 된다. 정지선이 그렇게 잡히면 감속이 영원히 걸리지 않아서
	 * "정지선이 없는 것" 과 같아진다 - 화면만 봐서는 원인을 알 수 없다.
	 *
	 * 잘라 두면 최소한 차선 끝으로 잡히고, 정지선 액터의 검산 표시(ACWTrafficStopLine::
	 * RefreshLaneReadout)가 0% / 100% 를 "차선 밖" 으로 잡아낸다.
	 */
	const float LaneLength = GetLaneLength();
	const float Clamped = FMath::Clamp(Distance, 0.f, LaneLength);

	// 돌려주는 값은 항상 "주행 시작점에서 얼마나 왔는가" 다. 역방향이면 뒤집어서 답한다.
	return bReverseDirection ? (LaneLength - Clamped) : Clamped;
}
