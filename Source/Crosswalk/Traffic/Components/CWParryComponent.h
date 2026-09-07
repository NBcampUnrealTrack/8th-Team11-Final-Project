// Copyright Crosswalk Team. All Rights Reserved.
// CWParryComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Structs/CWParryAttemptContext.h"
#include "Data/Structs/CWParrySyncData.h"
#include "CWParryComponent.generated.h"

class ACWBaseVehicle;

/**
 * 서버가 확정한 패링 결과를 모든 머신에서 발행. **사람에 붙는 연출**을 붙이는 자리다.
 *
 * 카메라 흔들림, 히트스톱, HUD, 쳐낸 사람 애니메이션처럼 플레이어를 따라가는 연출은 여기.
 * 차체 스파크, 트레일, 차 사운드처럼 차량을 따라가는 연출은 차량 BP 의
 * ACWBaseVehicle::OnParriedFX 다. 둘은 같은 사건에 각자 나가는 별개 RPC 이고,
 * "누구 몸에 붙는가" 로 가른다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCWOnParrySucceededSignature, const FCWParrySyncData&, ParrySyncData);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWParryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWParryComponent();

	// 로컬 플레이어의 패링 입력을 받아 서버 판정 요청으로 넘김
	UFUNCTION(BlueprintCallable, Category = "Traffic|Parry")
	void RequestParry(const FVector& CursorWorldLocation);

	// 패링 준비 비율을 반환
	// 0은 방금 사용한 상태, 1은 다시 사용할 수 있는 상태
	UFUNCTION(BlueprintPure, Category = "Traffic|Parry")
	float GetCooldownReadyRatio() const;

protected:
	// 클라이언트에서 서버로 패링 판정을 요청
	UFUNCTION(Server, Reliable)
	void Server_RequestParry(FVector_NetQuantize CursorWorldLocation, float ClientInputTime);

	/**
	 * 서버가 확정한 패링 결과를 연출용으로 모든 클라이언트에 전달. 사람에 붙는 연출 전용이다.
	 *
	 * 차에 붙는 연출은 ACWBaseVehicle::Multicast_PlayParriedFX 가 따로 나간다.
	 * 같은 사건에 RPC 가 둘인 것은 의도다 - 역할 구분은 FCWOnParrySucceededSignature 주석 참고.
	 *
	 * 연출 전용이라 Unreliable 이다. 차량이 실제로 튕겨나가는 것은 ACWBaseVehicle 의
	 * 복제 프로퍼티(bParried / ParriedLaunch*)가 담당하므로, 이게 유실돼도
	 * 이번 연출 한 번이 재생되지 않을 뿐 게임플레이는 갈리지 않는다.
	 * Reliable 로 두면 그 유실분이 재전송 큐에 쌓여 신뢰 채널만 먹는다.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_ParrySucceeded(const FCWParrySyncData& ParrySyncData);

	/**
	 * 서버가 쿨타임을 쓰지 않았음을 소유 클라이언트에 알린다. 로컬 예측을 되돌리는 신호다.
	 *
	 * 유실되면 쓰지도 않은 쿨타임을 그대로 기다리게 되므로 Reliable 이어야 한다.
	 * 소유 연결에만 가므로 다른 사람의 쿨타임에는 영향이 없다.
	 */
	UFUNCTION(Client, Reliable)
	void Client_ParryRejected();

private:
	// 서버에서 쿨타임을 확인하고 주변 위협 차량에 패링 판정을 시도
	void HandleServerParryRequest(const FVector& CursorWorldLocation, float ClientInputTime);

	// 커서 입력을 트래픽 쪽이 판정할 수 있는 시도 정보로 만든다
	FCWParryAttemptContext BuildParryAttemptContext(AActor& Parrier, const FVector& CursorWorldLocation, float ClientInputTime) const;

	// 서버 판정 결과를 연출 측이 쓸 동기화 데이터로 만든다
	FCWParrySyncData BuildParrySyncData(const FCWParryAttemptContext& ParryAttemptContext, ACWBaseVehicle& ParriedVehicle) const;

	// 로컬 입력 남발을 막기 위한 클라이언트 측 쿨타임 검사
	bool CanUseLocalCooldown() const;

	// 실제 판정 남발을 막기 위한 서버 측 쿨타임 검사
	bool CanUseServerCooldown() const;

	// 로컬 입력 쿨타임 시간을 갱신
	void UpdateLocalCooldown();

	// 서버 판정 쿨타임 시간을 갱신
	void UpdateServerCooldown();

	/**
	 * 서버가 이 시도를 쿨타임 없이 흘려보냈음을 알린다. 서버에서만 부른다.
	 *
	 * 여기서 되돌리는 것은 클라이언트의 로컬 예측뿐이다. 서버 쿨타임은 애초에
	 * 소모가 확정된 뒤에만 갱신하므로 되돌릴 것이 없다.
	 */
	void RejectParry();

	// 쿨타임을 아직 쓰지 않은 상태로 되돌린다. 소유 클라이언트에서만 의미가 있다.
	void ResetLocalCooldown();

private:
	UPROPERTY(EditDefaultsOnly, Category = "Traffic|Parry", meta = (ClampMin = "0.0"))
	float ParryCooldown = 1.f;

	/**
	 * 조건을 통과한 차량이 없어 판정이 실패했을 때 쿨타임을 되돌려줄지.
	 *
	 * 켜면 헛친 것은 공짜가 되고, 끄면 헛치는 것 자체가 1초의 대가를 치른다.
	 * 어느 쪽이 맞는지는 게임 감각의 문제라 플레이로 정하면 된다.
	 *
	 * 기본값은 켬이다. 랙 보정을 넣었어도 상한(UCWTrafficSubsystem::ResolveViewRewindSeconds)을
	 * 넘게 늦으면 여전히 실패하는데, 그건 그 사람의 실수가 아니라 회선 때문이라 대가를 물리기 어렵다.
	 *
	 * 끄면 패링 키를 연타하는 것을 막는 효과가 있다. 켠 상태에서 그 문제가 보이면
	 * 이 값을 끄기 전에 윈도우나 쿨타임 쪽을 먼저 보는 게 낫다.
	 *
	 * 서버 쿨타임에 걸린 요청은 이 값과 무관하게 절대 환불하지 않는다.
	 * 되돌려주면 곧바로 다시 보내고 다시 걸리기를 반복한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Traffic|Parry")
	bool bRefundCooldownOnMiss = true;

	/**
	 * 패링이 닿는 거리(cm). 차체 표면 기준으로 잰다. 0 이면 거리 제한 없음.
	 *
	 * 기본값 0. 거리 조건을 따로 두면 판정 윈도우와 이중으로 걸린다.
	 * 윈도우 안에 들어온 차량의 표면 거리는 이미 (속도 x 윈도우)로 정해진다.
	 * 750cm/s 차량에 윈도우 0.3초면 225cm 이므로, 반경을 그보다 좁게 주면 성공 자체가 불가능하다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Traffic|Parry", meta = (ClampMin = "0.0"))
	float ParryRadius = 0.f;

	/**
	 * 커서 방향을 중심으로 한 부채꼴 전체 각(도). 360 이면 방향 제한 없음.
	 *
	 * 기본값 360. 커서는 조준이 아니라 차를 튕겨 보낼 방향이다.
	 * 각도를 좁히면 "차를 저쪽으로 날리려고 반대편을 찍는" 조작이 전부 실패한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Traffic|Parry", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float ParryAngle = 360.f;

	// 랙 보정의 되돌림 양과 상한은 여기 없다. 피격 판정과 같은 값이어야 해서
	// UCWTrafficSubsystem::ResolveViewRewindSeconds 하나가 두 판정에 답한다.

	float LastLocalParryRequestTime = -999.f;

	float LastServerParryRequestTime = -999.f;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Traffic|Parry")
	FCWOnParrySucceededSignature OnParrySucceeded;
};
