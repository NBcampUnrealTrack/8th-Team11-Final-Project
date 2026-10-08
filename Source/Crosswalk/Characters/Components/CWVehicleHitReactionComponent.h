// CWVehicleHitReactionComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Structs/CWVehicleHitContext.h"
#include "CWVehicleHitReactionComponent.generated.h"

class UCWVehicleHitReactionComponent;

/**
 * 피격 잠금 상태가 바뀔 때 서버와 클라이언트 양쪽에서 발행.
 * 작업 중단 / 도구 드롭처럼 다른 컴포넌트를 건드리는 처리는 소유 액터가 받아서 한다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnHitLockChangedSignature, UCWVehicleHitReactionComponent*, HitReactionComponent, bool, bHitLocked);

/**
 * 다운 시간이 끝나 이제 일어나야 할 때 서버에서만 발행.
 *
 * 기상 연출은 소유 액터가 멀티캐스트로 뿌린다. 여기서 클라이언트까지 발행하면
 * 머신마다 다른 시점에 다른 자리에서 일어나게 된다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCWOnGetUpRequestedSignature, UCWVehicleHitReactionComponent*, HitReactionComponent);

/**
 * 플레이어에 붙는 차량 피격 반응 컴포넌트.
 *
 * 기획서 6.2 - 차에 치이면 넉백으로 날아가고, "땅에 닿고 2초 후까지" 아무 행동도 못 한다.
 * 그래서 잠금 시간은 피격 순간이 아니라 착지 시점부터 잰다.
 * 날아가는 시간이 잠금 시간에 먹히면 넉백이 길수록 다운이 짧아지는 이상한 결과가 나온다.
 *
 * 착지 통보는 소유 액터가 NotifyLanded 로 해준다. 컴포넌트는 캐릭터 이동을 알지 못한다.
 *
 * 잠금은 세 구간으로 나뉜다. 어느 구간이든 IsHitLocked 는 true 다.
 *   1) 비행   : 피격 ~ 착지 통보(NotifyLanded)
 *   2) 다운   : 착지 ~ HitLockDuration 경과
 *   3) 기상   : OnGetUpRequested ~ 기상 완료 통보(NotifyGetUpFinished)
 *
 * 기상 모션의 길이는 애니메이션 애셋에 달려 있어 컴포넌트가 알 수 없다.
 * 그래서 3구간의 끝은 시간이 아니라 소유 액터의 통보로 정한다.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWVehicleHitReactionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWVehicleHitReactionComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 차량 피격 정보를 받아 서버 기준 피격 상태를 시작
	UFUNCTION(BlueprintCallable, Category = "Traffic|Hit")
	void HandleVehicleHit(const FCWVehicleHitContext& HitContext);

	/**
	 * 넉백으로 날아간 뒤 땅에 닿았음을 알린다. 이 시점부터 잠금 시간을 잰다.
	 * 소유 액터의 착지 처리(ACharacter::Landed 등)에서 부른다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Hit")
	void NotifyLanded();

	/**
	 * 기상 모션이 끝나 조작을 돌려줘도 된다고 알린다. 이 시점에 잠금이 풀린다.
	 * 소유 액터가 OnGetUpRequested 를 받아 연출을 끝낸 뒤 부른다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Hit")
	void NotifyGetUpFinished();

	// 현재 피격 후 행동 제한 상태인지 확인
	UFUNCTION(BlueprintPure, Category = "Traffic|Hit")
	bool IsHitLocked() const;

	// 서버가 마지막으로 확정한 피격 정보를 반환
	UFUNCTION(BlueprintPure, Category = "Traffic|Hit")
	FCWVehicleHitContext GetLastHitContext() const;

protected:
	// 피격 상태가 시작될 때 서버와 클라이언트에서 호출
	UFUNCTION(BlueprintImplementableEvent, Category = "Traffic|Hit")
	void OnHitReactionStarted(const FCWVehicleHitContext& HitContext);

	// 피격 상태가 끝날 때 서버와 클라이언트에서 호출
	UFUNCTION(BlueprintImplementableEvent, Category = "Traffic|Hit")
	void OnHitReactionFinished();

private:
	// 서버에서 피격 상태를 시작하고 복제 값을 갱신
	void StartHitReaction(const FCWVehicleHitContext& HitContext);

	// 착지 시점부터 잠금 시간을 재기 시작
	void StartHitLockCountdown();

	// 다운 시간이 끝나 기상 연출을 요청. 잠금은 아직 풀지 않는다
	void RequestGetUp();

	// 서버에서 피격 상태를 종료
	void FinishHitReaction();

	// 클라이언트에서 피격 잠금 상태 변경을 감지
	UFUNCTION()
	void OnRep_HitLocked();

private:
	/**
	 * 다 구르고 멈춘 뒤 쓰러진 채로 있는 시간(초). 이 시간이 지나면 기상 연출이 시작된다.
	 *
	 * 기획서 6.2 는 2초지만 1초로 둔다. 래그돌로 굴러가 멈추기까지가 이미 잠금 시간이고,
	 * 뒤에 기상 모션 길이까지 붙으므로 거기에 2초를 더 얹으면 조작을 못 하는 구간이 너무 길어진다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Traffic|Hit", meta = (ClampMin = "0.0"))
	float HitLockDuration = 1.f;

	/**
	 * 착지 통보를 기다리는 최대 시간(초). 넘기면 착지한 것으로 치고 잠금 시간을 시작한다.
	 * 맵 밖으로 떨어지거나 착지 처리가 오지 않는 경우에 영구 잠금이 되는 것을 막는 안전장치다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Traffic|Hit", meta = (ClampMin = "0.0"))
	float MaxLandingWait = 5.f;

	/**
	 * 기상 완료 통보를 기다리는 최대 시간(초). 넘기면 완료된 것으로 치고 잠금을 푼다.
	 * 기상 몬타주가 비어 있거나 중간에 끊긴 경우에 영구 잠금이 되는 것을 막는 안전장치다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Traffic|Hit", meta = (ClampMin = "0.0"))
	float MaxGetUpWait = 5.f;

	UPROPERTY(ReplicatedUsing = OnRep_HitLocked)
	bool bHitLocked = false;

	UPROPERTY(Replicated)
	FCWVehicleHitContext LastHitContext;

	// 아직 착지하지 않아 잠금 시간을 시작하지 못한 상태. 서버에서만 의미가 있다
	bool bWaitingForLanding = false;

	// 기상 연출을 요청해 놓고 완료 통보를 기다리는 상태. 서버에서만 의미가 있다
	bool bWaitingForGetUp = false;

	FTimerHandle HitLockTimerHandle;

	FTimerHandle LandingWaitTimerHandle;

	FTimerHandle GetUpWaitTimerHandle;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Traffic|Hit")
	FCWOnHitLockChangedSignature OnHitLockChanged;

	UPROPERTY(BlueprintAssignable, Category = "Traffic|Hit")
	FCWOnGetUpRequestedSignature OnGetUpRequested;
};