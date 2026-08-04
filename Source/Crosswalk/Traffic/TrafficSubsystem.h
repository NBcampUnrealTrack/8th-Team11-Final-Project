// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TrafficSubsystem.generated.h"

class ABaseVehicle;

/**
 * 활성 차량 목록을 들고 있는 조회 창구.
 *
 * 패링은 플레이어가 "지금 나를 칠 차가 있냐"를 물어야 성립한다.
 * 플레이어 파트가 차량을 직접 순회하지 않도록 트래픽 쪽에서 이 진입점 하나만 노출한다.
 */
UCLASS()
class CROSSWALK_API UTrafficSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** WorldContext로부터 서브시스템을 얻는다. 월드가 없으면 nullptr. */
	static UTrafficSubsystem* Get(const UObject* WorldContextObject);

	void RegisterVehicle(ABaseVehicle* Vehicle);
	void UnregisterVehicle(ABaseVehicle* Vehicle);

	/**
	 * Target을 Window(초) 안에 칠 차량 중 가장 임박한 것을 찾는다.
	 * Window에 0 이하를 넘기면 각 차량의 자체 패링 윈도우를 기준으로 판정한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Parry")
	ABaseVehicle* FindImminentThreat(const AActor* Target, float Window, float& OutTimeToImpact) const;

	/**
	 * 패링 입력 한 번을 처리한다. 성공 시 패링당한 차량을 반환.
	 * 쿨타임(3초)과 입력 바인딩은 플레이어 파트 담당이므로 여기서 관리하지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Parry")
	ABaseVehicle* TryParryNearestThreat(AActor* Parrier);

	UFUNCTION(BlueprintPure, Category = "Traffic")
	int32 GetActiveVehicleCount() const;

private:
	TArray<TWeakObjectPtr<ABaseVehicle>> ActiveVehicles;
};
