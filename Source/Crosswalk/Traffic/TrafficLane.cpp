// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrafficLane.h"

#include "Components/SplineComponent.h"

ATrafficLane::ATrafficLane()
{
	PrimaryActorTick.bCanEverTick = false;

	LaneSpline = CreateDefaultSubobject<USplineComponent>(TEXT("LaneSpline"));
	SetRootComponent(LaneSpline);

	// 차선은 닫힌 경로가 아니다. 시작점에서 끝점까지 한 번 주행한 뒤 디스폰된다.
	LaneSpline->SetClosedLoop(false);
}

float ATrafficLane::GetLaneLength() const
{
	return LaneSpline ? LaneSpline->GetSplineLength() : 0.f;
}

FTransform ATrafficLane::GetTransformAtDistance(float Distance) const
{
	// 스플라인이 없으면 액터 위치로 대체해 스폰이 원점으로 튀는 것만 막는다.
	if (!LaneSpline)
	{
		return GetActorTransform();
	}

	return LaneSpline->GetTransformAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
}

FVector ATrafficLane::GetDirectionAtDistance(float Distance) const
{
	if (!LaneSpline)
	{
		return GetActorForwardVector();
	}

	return LaneSpline->GetDirectionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
}
