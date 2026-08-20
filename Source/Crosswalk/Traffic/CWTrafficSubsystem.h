// Copyright Crosswalk Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/Structs/CWParryAttemptContext.h"
#include "CWTrafficSubsystem.generated.h"

class ACWBaseVehicle;

/**
 * 활성 차량 목록을 들고 있는 조회 창구.
 *
 * 패링은 플레이어가 "지금 나를 칠 차가 있냐"를 물어야 성립한다.
 * 플레이어 파트가 차량을 직접 순회하지 않도록 트래픽 쪽에서 이 진입점 하나만 노출한다.
 */
UCLASS()
class CROSSWALK_API UCWTrafficSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** WorldContext로부터 서브시스템을 얻는다. 월드가 없으면 nullptr. */
	static UCWTrafficSubsystem* Get(const UObject* WorldContextObject);

	void RegisterVehicle(ACWBaseVehicle* Vehicle);
	void UnregisterVehicle(ACWBaseVehicle* Vehicle);

	/**
	 * 같은 차선에서 Vehicle 바로 앞에 있는 차량을 찾는다. 없으면 nullptr.
	 * 차간 거리 유지에 쓰이므로 정지한 차량도 후보에 포함한다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic")
	ACWBaseVehicle* FindVehicleAhead(const ACWBaseVehicle* Vehicle) const;

	/**
	 * 패링 입력 한 번을 처리한다. 성공 시 패링당한 차량을 반환.
	 *
	 * 대상 선별(반경·각도·임박도)까지만 여기서 하고, 최종 판정은 고른 차량의
	 * ReceiveParryAttempt 가 내린다. 차종별 예외는 차량이 스스로 결정해야 하기 때문이다.
	 * 쿨타임(1초)과 입력 바인딩은 플레이어 파트 담당이므로 여기서 관리하지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Parry")
	ACWBaseVehicle* TryParryWithAttempt(const FCWParryAttemptContext& ParryAttemptContext);

	/**
	 * 지정 지점에서 Radius 안에 있는 차량을 모은다. 차체 표면 기준으로 잰다.
	 *
	 * 도로 위에서 차량과 상호작용하는 다른 파트를 위한 조회 창구다.
	 * (젖은 페인트를 밟고 지나가는 차량을 찾는 데 쓴다)
	 */
	UFUNCTION(BlueprintCallable, Category = "Traffic")
	void GetVehiclesNearPoint(const FVector& Point, float Radius, TArray<ACWBaseVehicle*>& OutVehicles) const;

	UFUNCTION(BlueprintPure, Category = "Traffic")
	int32 GetActiveVehicleCount() const;

private:
	/**
	 * 시도 정보 기준으로 이 차량이 패링 후보인지 본다.
	 * 통과하면 OutTimeToImpact 에 충돌까지 남은 시간을 낸다.
	 */
	bool PassesAttemptFilter(ACWBaseVehicle& Vehicle, const FCWParryAttemptContext& ParryAttemptContext, float& OutTimeToImpact) const;

private:
	TArray<TWeakObjectPtr<ACWBaseVehicle>> ActiveVehicles;
};
