// CWParryComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Structs/CWParryAttemptContext.h"
#include "Data/Structs/CWParrySyncData.h"
#include "CWParryComponent.generated.h"

class ACWBaseVehicle;
class UCWParryPropCatalog;

/**
 * 서버가 확정한 패링 결과를 모든 머신에서 발행. **사람에 붙는 연출**을 붙이는 자리다.
 *
 * 카메라 흔들림, 히트스톱, HUD, 쳐낸 사람 애니메이션처럼 플레이어를 따라가는 연출은 여기.
 * 차체 스파크, 트레일, 차 사운드처럼 차량을 따라가는 연출은 차량 BP 의
 * ACWBaseVehicle::OnParriedFX 다. 둘은 같은 사건에 각자 나가는 별개 RPC 이고,
 * "누구 몸에 붙는가" 로 가른다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCWOnParrySucceededSignature, const FCWParrySyncData&, ParrySyncData);

UCLASS(ClassGroup = (Crosswalk), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWParryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWParryComponent();

	/**
	 * 로컬 플레이어의 패링 입력을 받아 서버 판정 요청으로 넘김. 요청이 실제로 나갔으면 true.
	 *
	 * 반환값은 "패링에 성공했는가" 가 아니다. 판정은 서버에서 나므로 여기서는 알 수 없다.
	 * 로컬 쿨타임에 막혔는지만 답한다 - 부르는 쪽이 헛스윙 연출을 그 자리에서 재생할지
	 * 정하는 데 쓴다. 성공 여부를 기다렸다 재생하면 스윙이 왕복 지연만큼 늦게 나간다.
	 *
	 * @param OutPropIndex 이번 스윙에 쓸 소품. 여기서 뽑아 돌려주고, 같은 값을 서버로 보낸다.
	 *                     부르는 쪽은 결과를 기다리지 않고 그 소품으로 바로 휘두르면 된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Parry")
	bool RequestParry(const FVector& CursorWorldLocation, int32& OutPropIndex);

	// 패링 준비 비율을 반환
	// 0은 방금 사용한 상태, 1은 다시 사용할 수 있는 상태
	UFUNCTION(BlueprintPure, Category = "Traffic|Parry")
	float GetCooldownReadyRatio() const;

	/** 스윙 소품 카탈로그. 캐릭터가 인덱스로 메시와 사운드를 꺼낼 때 쓴다. 없으면 nullptr. */
	UFUNCTION(BlueprintPure, Category = "Traffic|Parry")
	UCWParryPropCatalog* GetPropCatalog() const { return PropCatalog; }

protected:
	/**
	 * 클라이언트에서 서버로 패링 판정을 요청. 소품 인덱스를 같이 싣는다.
	 *
	 * 인덱스를 클라이언트가 정해 보내는 것은 의도다. 순수 연출값이라 조작해도 얻을 것이
	 * 없고, 대신 누른 사람이 결과를 기다리지 않고 바로 그 소품으로 휘두를 수 있다.
	 * 서버는 범위만 확인한다. (ResolvePropIndex)
	 */
	UFUNCTION(Server, Reliable)
	void Server_RequestParry(FVector_NetQuantize CursorWorldLocation, int32 PropIndex);

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
	void HandleServerParryRequest(const FVector& CursorWorldLocation, int32 PropIndex);

	/**
	 * 클라이언트가 보낸 소품 인덱스를 검증한다. 서버에서만 부른다.
	 *
	 * 범위 밖이면 거절하지 않고 서버가 다시 뽑아 채운다. 연출값 하나 때문에 판정을
	 * 무르면 그 사람만 패링이 안 되는데, 그건 어긋난 인덱스가 만든 결과치고 너무 크다.
	 * 그 경우 누른 사람 화면의 소품만 한 번 바뀐다.
	 */
	int32 ResolvePropIndex(int32 RequestedPropIndex) const;

	// 커서 입력을 트래픽 쪽이 판정할 수 있는 시도 정보로 만든다
	FCWParryAttemptContext BuildParryAttemptContext(AActor& Parrier, const FVector& CursorWorldLocation) const;

	// 서버 판정 결과를 연출 측이 쓸 동기화 데이터로 만든다
	FCWParrySyncData BuildParrySyncData(const FCWParryAttemptContext& ParryAttemptContext, ACWBaseVehicle& ParriedVehicle, int32 PropIndex) const;

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

	/**
	 * 로그에 붙일 시전자 표시. 진단 로그 전용이다.
	 *
	 * 판정이 전부 서버에서 도는 탓에 패링 줄은 호스트 로그에만 쌓인다. 그래서 이름이 없으면
	 * 4인 세션에서 누가 눌렀는지 구분할 방법이 아예 없다. 09-17 로그에서 같은 차를 두 번
	 * 친 3건과 1초 간격 연타 4회가 한 사람인지 여러 명이 겹친 것인지 끝내 못 갈랐다.
	 *
	 * 대수 대조 로그가 쓰는 표시와 같은 모양으로 맞춘다 - 두 로그를 시각으로 붙여 읽는 것이
	 * 용도라, 같은 사람이 두 줄에서 다른 이름으로 보이면 맞춰 볼 수가 없다.
	 */
	FString GetInstigatorTag() const;

private:
	/**
	 * 패링 입력 간격(초).
	 *
	 * **0.5 는 임시값이다.** 2026-09-21 기획자와 정했고, 환불을 끄는 것과 한 묶음이다 -
	 * 헛친 것도 대가를 치르게 되므로 1초를 그대로 두면 실패 한 번의 벌이 너무 무거워진다.
	 * 둘 중 하나만 보고 조절하면 안 된다.
	 *
	 * ⚠️ **BP_PlayerCharacter 의 ParryComponent 가 이 값을 덮고 있다.** 여기를 고쳐도
	 * 그 BP 로 스폰되는 캐릭터에는 반영되지 않는다. 실제로 게임에 적용하려면 BP 쪽을
	 * 같이 바꿔야 하고, 이 기본값은 새로 붙는 BP 가 다른 값을 갖지 않게 맞춰 두는 용도다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Traffic|Parry", meta = (ClampMin = "0.0"))
	float ParryCooldown = 0.5f;

	/**
	 * 조건을 통과한 차량이 없어 판정이 실패했을 때 쿨타임을 되돌려줄지.
	 *
	 * 켜면 헛친 것은 공짜가 되고, 끄면 헛치는 것 자체가 ParryCooldown 만큼의 대가를 치른다.
	 *
	 * **2026-09-21 기획자와 끄기로 정했다.** 환불이 켜져 있으면 쿨타임이 실패에는 사실상
	 * 걸리지 않아서 연타로 긁는 것이 최적해가 된다. 09-21 로그에서 1.4초가 아니라
	 * 5초 동안 0.2초 간격으로 22번이 찍혔다 - 쿨타임 1초가 무력화된 상태였다.
	 *
	 * 대가는 알고 받는다. 랙 보정을 넣었어도 상한
	 * (UCWTrafficSubsystem::ResolveViewRewindSeconds)을 넘게 늦으면 여전히 실패하는데,
	 * 그건 그 사람의 실수가 아니라 회선 때문이다. 회선이 나쁜 사람이 더 자주 벌을 받는
	 * 구조라, 그게 실제로 보이면 이 값이 아니라 보정 상한을 먼저 봐야 한다.
	 *
	 * 서버 쿨타임에 걸린 요청은 이 값과 무관하게 절대 환불하지 않는다.
	 * 되돌려주면 곧바로 다시 보내고 다시 걸리기를 반복한다.
	 *
	 * ⚠️ ParryCooldown 과 같이 BP_PlayerCharacter 가 덮고 있다. 위 주석 참고.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Traffic|Parry")
	bool bRefundCooldownOnMiss = false;

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
	 * 기본값 180 - 커서와 같은 반구에 있는 차량만 후보로 본다. 등 뒤에서 오는 차량을
	 * 커서는 앞에 둔 채로 쳐내는 것을 막으려고 켰다. 뒤차를 치려면 커서를 그쪽으로
	 * 돌려야 하고, 그러면 캐릭터도 같이 돌아서므로(UpdateCursorFacing) 화면과 맞는다.
	 * 2026-09-18 플레이로 확인했다.
	 *
	 * 이 각은 후보 선별만 좁힌다. 발사 방향은 차량에서 커서 지점을 향해 따로 잡으므로
	 * (ACWBaseVehicle::ResolveLaunchDirection) 여기를 좁혀도 "어디로 날릴지" 는 그대로다.
	 * 2026-09-21 에 발사 기준을 커서 지점으로 되돌리면서 둘이 분리됐다.
	 *
	 * 180 에서 잃는 것은 등 뒤 차량을 치는 것뿐이다. 커서를 차 반대편에 찍어 차를 자기
	 * 쪽으로 지나가게 날리는 조작은 이제 가능하다 - 그 커서는 플레이어 기준으로도 앞쪽이라
	 * 선별을 통과하고, 발사는 차량 기준이라 원하는 쪽으로 나간다.
	 * (단 시전자 쪽 60도는 ACWBaseVehicle::SelfLaunchDeadZoneAngle 이 밀어낸다)
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Traffic|Parry", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float ParryAngle = 180.f;

	/**
	 * 스윙에 쓸 소품 목록. 비워두면 소품 없이 맨손 스윙이 나간다.
	 *
	 * 누르는 사람이 여기서 인덱스 하나를 뽑아 서버로 보내고, 서버가 그 값을
	 * FCWParrySyncData 에 실어 뿌린다. 각 클라이언트는 같은 인덱스로 같은 메시와 사운드를
	 * 꺼낸다. 실제로 손에 붙이고 소리를 내는 것은 ACWPlayerCharacter 쪽이고,
	 * 캐릭터는 GetPropCatalog() 로 같은 애셋을 본다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Traffic|Parry")
	TObjectPtr<UCWParryPropCatalog> PropCatalog;

	// 랙 보정의 되돌림 양과 상한은 여기 없다. 피격 판정과 같은 값이어야 해서
	// UCWTrafficSubsystem::ResolveViewRewindSeconds 하나가 두 판정에 답한다.

	/**
	 * "아직 한 번도 안 썼다" 를 뜻하는 시각. 월드 시간은 항상 이보다 커서 곧바로 사용 가능으로 읽힌다.
	 * 환불(ResetLocalCooldown)도 이 값으로 되돌린다.
	 */
	static constexpr float CooldownNeverUsedTime = -999.f;

	float LastLocalParryRequestTime = CooldownNeverUsedTime;

	float LastServerParryRequestTime = CooldownNeverUsedTime;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Traffic|Parry")
	FCWOnParrySucceededSignature OnParrySucceeded;
};
