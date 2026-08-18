// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CWTrafficLane.generated.h"

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

	UFUNCTION(BlueprintPure, Category = "Traffic|Lane")
	float GetLaneLength() const;

	/** 시작점에서 Distance(cm)만큼 진행한 지점의 트랜스폼. 회전은 진행 방향을 바라본다. */
	UFUNCTION(BlueprintPure, Category = "Traffic|Lane")
	FTransform GetTransformAtDistance(float Distance) const;

	/** 시작점에서 Distance(cm)만큼 진행한 지점의 진행 방향(정규화). */
	UFUNCTION(BlueprintPure, Category = "Traffic|Lane")
	FVector GetDirectionAtDistance(float Distance) const;

	USplineComponent* GetLaneSpline() const { return LaneSpline; }

protected:
	/** 차선 경로. 포인트를 편집해 차선 모양을 잡는다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Lane")
	TObjectPtr<USplineComponent> LaneSpline;
};
