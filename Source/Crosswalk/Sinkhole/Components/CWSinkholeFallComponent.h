// CWSinkholeFallComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CWSinkholeFallComponent.generated.h"

class ACWSinkhole;
class UCWSinkholeFallComponent;

/**
 * 낙하 잠금 상태가 바뀔 때 서버와 클라이언트 양쪽에서 발행.
 * 입력 차단과 작업 중단처럼 다른 컴포넌트를 건드리는 처리는 소유 액터가 받아서 한다.
 * (차량 피격의 FCWOnHitLockChangedSignature 와 같은 모델이다)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnFallLockChangedSignature, UCWSinkholeFallComponent*, FallComponent, bool, bFallLocked);

/**
 * 플레이어에 붙는 싱크홀 낙하 반응 컴포넌트. (싱크홀 기획서 5~6절)
 *
 * 실제로 Z 로 추락시키지 않는다. 캡슐은 그 자리에 두고 메쉬 컴포넌트만 조작한다.
 *   - 스케일 축소 + 메쉬 로컬 Z 오프셋을 같은 알파로 함께 적용
 *   - 두 값을 같이 쓰는 이유: 메쉬 피벗이 발바닥이라 스케일만 줄이면 발은 제자리에 남고
 *     머리만 내려온다. Z 를 함께 내려야 전체가 가라앉는 그림이 된다.
 *   - 캡슐을 건드리지 않으므로 이동/물리/낙하 데미지 판정이 전혀 얽히지 않는다.
 *
 * 진행 순서
 *   1) 흡입   : 구멍 중앙으로 짧게 끌어당긴다. 즉시 좌표를 옮기면 순간이동으로 보인다.
 *   2) 가라앉음: 스케일과 Z 를 알파로 밀어 넣는다. 도로 메쉬가 알아서 잘라 준다.
 *   3) 대기   : 사라진 상태로 리스폰 시간을 센다.
 *   4) 리스폰 : 플레이어 스타트로 옮기고 무적을 건다.
 *
 * 도구는 흡입이 시작되기 "전" 위치에 떨어뜨린다. 중앙으로 끌려간 뒤에 떨어뜨리면
 * 구멍 한가운데 놓여 회수할 수 없다.
 *
 * 네트워크
 *   잠금 상태와 단계만 리플리케이트하고, 연출(스케일/Z/그림자)은 각 머신이 자기 시계로 돈다.
 *   프레임 단위로 스케일을 동기화할 이유가 없다. 판정에 영향이 없는 순수 외형이다.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWSinkholeFallComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWSinkholeFallComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void BeginPlay() override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * 싱크홀에 빠진다. 서버에서만 유효하다. ACWSinkhole 의 오버랩에서 들어온다.
	 * 이미 빠져 있거나 리스폰 대기 중이면 무시된다.
	 */
	void StartFall(ACWSinkhole* Sinkhole);

	/** 지금 빠져 있어서 조작을 받으면 안 되는 상태인가. */
	UFUNCTION(BlueprintPure, Category = "Sinkhole|Fall")
	bool IsFallLocked() const { return bFallLocked; }

	/** 리스폰 직후 무적 구간인가. 깜빡임 연출의 on/off 기준이다. */
	UFUNCTION(BlueprintPure, Category = "Sinkhole|Fall")
	bool IsInvincible() const { return bInvincible; }

protected:
	/** 빠지기 시작한 순간. BP 에서 비명과 낙하 몬타주를 붙이는 자리. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Sinkhole|Fall")
	void OnFallStarted();

	/** 리스폰 직후. BP 에서 깜빡임을 켜는 자리. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Sinkhole|Fall")
	void OnRespawned();

	/** 무적이 끝난 순간. BP 에서 깜빡임을 끄는 자리. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Sinkhole|Fall")
	void OnInvincibilityFinished();

private:
	UFUNCTION()
	void OnRep_FallLocked();

	/** 흡입과 가라앉음을 진행시킨다. 모든 머신이 각자 자기 시계로 돈다. */
	void TickFallSequence(float DeltaTime);

	/** 메쉬 스케일과 Z 오프셋을 알파에 맞춰 넣는다. 0 = 평소, 1 = 완전히 잠김. */
	void ApplySinkAlpha(float Alpha);

	/** 연출 값을 원래대로 되돌린다. 리스폰 시점에 부른다. */
	void ResetSinkVisual();

	/** 플레이어 스타트로 옮기고 무적을 건다. 서버에서만 돈다. */
	void Respawn();

	/** 무적 시간이 끝났을 때. 서버에서만 돈다. */
	void ClearInvincibility();

	/** 들고 있던 도구를 저장해 둔 진입 지점에 떨어뜨린다. 서버에서만 돈다. */
	void DropHeldToolAtEntry();

protected:
	// 구멍 중앙으로 끌려가는 시간(초). 짧아야 답답하지 않고, 0 이면 순간이동으로 보인다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole|Fall", meta = (ClampMin = "0.0"))
	float SuctionDuration = 0.2f;

	// 흡입이 끝나고 완전히 잠기기까지의 시간(초).
	//
	// 너무 느리면 도로 메쉬에 잘리는 단면이 눈에 띄어 트릭이 드러난다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole|Fall", meta = (ClampMin = "0.05"))
	float SinkDuration = 0.35f;

	// 완전히 잠겼을 때의 메쉬 스케일 배율.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole|Fall", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SunkMeshScale = 0.85f;

	// 완전히 잠겼을 때 메쉬를 내릴 거리(cm). 캡슐은 그대로 있고 메쉬만 내려간다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole|Fall", meta = (ClampMin = "0.0"))
	float SunkMeshDepth = 180.f;

	// 스케일과 Z 를 함께 미는 커브. 비워 두면 선형이다.
	// 두 값을 하나의 커브로 묶어 두면 튜닝할 때 커브만 만지면 된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole|Fall")
	TObjectPtr<UCurveFloat> SinkCurve;

	// 완전히 잠긴 뒤 리스폰까지의 대기 시간(초). 싱크홀의 핵심 리스크인 "시간 손해" 다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole|Fall", meta = (ClampMin = "0.0"))
	float RespawnDelay = 2.f;

	// 리스폰 직후 무적 시간(초).
	//
	// 0 으로 두면 안 된다. 스타트 지점에 차가 지나가는 순간 리스폰하면 연속으로 당해
	// 플레이어가 손쓸 수 없는 상황이 된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole|Fall", meta = (ClampMin = "0.0"))
	float InvincibleDuration = 2.f;

private:
	// 빠진 상태. 조작 차단의 기준이라 리플리케이트한다.
	UPROPERTY(ReplicatedUsing = OnRep_FallLocked)
	bool bFallLocked = false;

	// 무적 여부. 깜빡임을 남의 화면에서도 보여야 해서 리플리케이트한다.
	UPROPERTY(Replicated)
	bool bInvincible = false;

	// 흡입 목표 지점. 빠진 싱크홀의 중앙이다.
	UPROPERTY(Replicated)
	FVector FallTargetLocation = FVector::ZeroVector;

	// 흡입이 시작되기 직전의 위치. 도구를 여기에 떨어뜨린다.
	FVector EntryLocation = FVector::ZeroVector;

	// 연출 시작 이후 흐른 시간. 각 머신이 자기 시계로 센다.
	float ElapsedFallTime = 0.f;

	// 연출 전 메쉬의 원래 상대 위치와 스케일. 리스폰 때 이 값으로 되돌린다.
	FVector DefaultMeshRelativeLocation = FVector::ZeroVector;
	FVector DefaultMeshRelativeScale = FVector::OneVector;

	FTimerHandle RespawnTimerHandle;
	FTimerHandle InvincibleTimerHandle;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Sinkhole|Fall")
	FCWOnFallLockChangedSignature OnFallLockChanged;
};
