// Copyright Crosswalk Team. All Rights Reserved.
// CWVehicleHitReactionComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Structs/CWVehicleHitContext.h"
#include "CWVehicleHitReactionComponent.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWVehicleHitReactionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWVehicleHitReactionComponent();

	// 차량 피격 정보를 받아 서버 기준 피격 상태를 시작
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Hit")
	void HandleVehicleHit(const FCWVehicleHitContext& HitContext);

	// 현재 피격 후 행동 제한 상태인지 확인
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Hit")
	bool IsHitLocked() const;

	// 서버가 마지막으로 확정한 피격 정보를 반환
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Hit")
	FCWVehicleHitContext GetLastHitContext() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	// 피격 상태가 시작될 때 서버와 클라이언트에서 호출
	UFUNCTION(BlueprintImplementableEvent, Category = "Crosswalk|Hit")
	void OnHitReactionStarted(const FCWVehicleHitContext& HitContext);

	// 피격 상태가 끝날 때 서버와 클라이언트에서 호출
	UFUNCTION(BlueprintImplementableEvent, Category = "Crosswalk|Hit")
	void OnHitReactionFinished();

private:
	// 서버에서 피격 상태를 시작하고 복제 값을 갱신
	void StartHitReaction(const FCWVehicleHitContext& HitContext);

	// 서버에서 피격 상태를 종료
	void FinishHitReaction();

	// 클라이언트에서 피격 잠금 상태 변경을 감지
	UFUNCTION()
	void OnRep_HitLocked();

private:
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Hit", meta = (ClampMin = "0.0"))
	float HitLockDuration = 2.f;

	UPROPERTY(ReplicatedUsing = OnRep_HitLocked)
	bool bHitLocked = false;

	UPROPERTY(Replicated)
	FCWVehicleHitContext LastHitContext;

	FTimerHandle HitLockTimerHandle;
};