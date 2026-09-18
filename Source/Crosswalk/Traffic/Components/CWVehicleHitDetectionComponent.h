// CWVehicleHitDetectionComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CWVehicleHitDetectionComponent.generated.h"

class ACWBaseVehicle;

/**
 * 차량이 플레이어를 치는 판정. 서버 전용이고, 결과는 Interface_VehicleHitReceiver 로 넘긴다.
 *
 * 왜 컴포넌트인가
 *   ACWBaseVehicle 은 주행·동기화·패링을 들고 있고, 피격은 그중 어느 것과도 상태를
 *   공유하지 않는다. 필요한 것은 차량의 위치·속도·콜리전 박스와 차종 데이터(차종 ID,
 *   피격 하한 속도, 납작 피격 여부)뿐이라 게터로 읽는다.
 *
 * 데이터는 차량에 남아 있다
 *   VehicleType / MinHitSpeed / bFlattenOnHit 은 차종 BP 가 정하는 값이라 ACWBaseVehicle 의
 *   UPROPERTY 그대로다. 프로퍼티를 옮기면 차종 BP 에 입력한 값이 사라진다.
 *
 * 왜 매 틱 다시 보는가
 *   오버랩 이벤트는 겹치기 시작할 때 한 번만 온다. 그 순간에 조건이 안 맞아 흘려보낸
 *   피격(너무 느림 / 랙 보정 보류)은 나중에 조건이 갖춰져도 새 이벤트가 없다. 그래서
 *   겹쳐 있는 대상을 들고 있다가 차량 Tick 이 RetryPendingHits 를 부른다.
 *
 * 틱이 없다. 재판정은 차량 Tick 이 맨 앞에서 부른다.
 *   "지난 프레임 속도·위치로 본다" 는 전제가 있어서(RetryPendingHits 주석) 액터 틱과의
 *   순서를 엔진 설정에 맡기지 않는다.
 *
 * 패링과 겹치는 한 곳: 쳐낸 사람이 차와 겹쳐 있으면 이번 겹침 동안 피격에서 뺀다.
 *   그건 차량의 TryParry 가 IgnoreWhileOverlapping 으로 넘긴다.
 */
UCLASS(ClassGroup = (Crosswalk), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWVehicleHitDetectionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWVehicleHitDetectionComponent();

	/**
	 * 겹쳐 있지만 아직 못 친 대상을 서버 틱마다 다시 판정한다. 차량 Tick 이 부른다.
	 *
	 * 흘려보내는 경로가 둘이라 둘 다 여기서 다시 본다.
	 *   1. 속도가 MinHitSpeed 아래 - 멈춰 있는 차에 걸어 들어간 사람이 그 차가 다시
	 *      출발해도 맞지 않는다. "차 안에 서 있으면 안전" 이 되어 버린다.
	 *   2. 랙 보정으로 보류 - 그 사람 화면에서 아직 차가 안 닿았다. 몸을 빼면 그대로
	 *      피한 것이고, 가만히 있으면 화면에서 차가 닿는 순간 여기서 성립해야 한다.
	 *
	 * 매 틱 도는 비용은 겹쳐 있는 대상 수만큼이다. 대부분의 차량은 이 집합이 비어 있어
	 * 곧바로 빠져나간다.
	 */
	void RetryPendingHits();

	/**
	 * 피격 기록을 비운다. 차량이 새 주행을 시작할 때(InitializeOnLane) 부른다.
	 * 지금은 스폰마다 새 액터라 이미 비어 있다. 풀링이 생겼을 때를 위한 방어다.
	 */
	void ResetHitRecords();

	/**
	 * 이 액터가 지금 차와 겹쳐 있으면 이번 겹침 동안만 피격에서 뺀다.
	 *
	 * 패링 직후에 쓴다. 충돌 직전에 쳐낸 경우라 차와 몸이 이미 닿아 있고, 그대로 두면
	 * 패링에 성공하자마자 맞는다. 겹치지 않은 상태면 아무것도 하지 않는다 - 커서를 자기
	 * 쪽으로 찍어 차를 자신에게 날려보냈다면 그건 맞아야 하는 상황이다.
	 *
	 * 어느 쪽이든 차에서 떨어지는 순간 HandleEndOverlap 이 기록을 지우므로,
	 * 다시 닿으면 쳐낸 사람도 똑같이 피격된다.
	 */
	void IgnoreWhileOverlapping(AActor* Actor);

protected:
	virtual void BeginPlay() override;

private:
	/** 소유 차량. 차량이 아닌 액터에 붙어 있으면 nullptr 이고 모든 동작이 비켜간다. */
	ACWBaseVehicle* GetVehicle() const;

	/**
	 * 이 대상을 지금 칠 수 있으면 치고 기록한다. 서버에서만 유효하다.
	 *
	 * 오버랩이 시작될 때와, 느렸다가 다시 빨라진 순간 두 곳에서 들어온다.
	 * 두 경로가 같은 판정을 써야 하므로 조건을 여기 한 곳에 모은다.
	 */
	void TryApplyHit(AActor* OtherActor, bool bFromSweep, const FHitResult& SweepResult);

	/**
	 * 이 피격이 대상의 화면에서도 실제로 닿았는지 본다. 통과하면 지금 쳐도 되는 피격이다.
	 *
	 * 서버는 "지금 차 위치"로 겹침을 잡지만, 대상의 화면 속 차는 그 사람 핑만큼 뒤에 있다.
	 * 차를 그만큼 되돌렸을 때 아직 대상에 닿지 않았다면, 화면에서는 피한 것이므로 흘려보낸다.
	 *
	 * 판정식: (차가 대상을 파고든 깊이) < (핑 동안 차가 이동한 거리) 이면 보류한다.
	 *   되돌린 차 앞면이 아직 대상 뒤에 있다는 뜻이라, 그 사람 화면에선 차가 안 왔다.
	 *
	 * 가만히 선 대상은 차가 계속 파고들어 깊이가 이동 거리를 넘는 순간 통과한다. 그래서
	 * 매 틱 재검사가 필요하다. (RetryPendingHits) 보류를 한 번만 하고 끝내면 그 사람은
	 * 핑이 높을수록 안 맞는 무적이 되어 버린다.
	 *
	 * 되돌릴 양이 0 인 대상(호스트 조종/랜)은 깊이가 음수 미만이 될 수 없어 항상 통과한다.
	 * 즉 보정 이전과 동일하게 동작한다.
	 */
	bool PassesHitLagCompensation(const AActor* Target) const;

	/** 오버랩 정보에서 피격 지점을 뽑는다. 스윕이 아니면 최근접점으로 대체한다. */
	FVector ResolveHitLocation(const AActor* HitActor, bool bFromSweep, const FHitResult& SweepResult) const;

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/**
	 * 차에서 떨어진 대상을 피격 기록에서 지운다. 다시 닿으면 또 맞아야 하기 때문이다.
	 * 기록을 영구히 들고 있으면 한 번 스친 사람은 그 차에 두 번 다시 맞지 않는다.
	 */
	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

private:
	/** 같은 액터를 중복으로 치지 않도록 이미 판정한 대상을 기록. */
	TSet<TWeakObjectPtr<AActor>> HitActors;

	/**
	 * 지금 몸에 겹쳐 있는 피격 대상. 랙 보정으로 보류됐거나 아직 속도가 하한을 못 넘어
	 * 못 친 대상을 매 틱 다시 보기 위해 들고 있는다. (RetryPendingHits)
	 *
	 * 오버랩 이벤트는 겹치기 시작할 때 한 번뿐이라, 이 집합이 없으면 나중에 조건이
	 * 갖춰진 피격을 다시 판정할 길이 없다. 떨어지면 HandleEndOverlap 이 지운다.
	 */
	TSet<TWeakObjectPtr<AActor>> OverlappingReceivers;
};
