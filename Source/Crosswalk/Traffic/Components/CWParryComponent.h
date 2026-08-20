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
 * 서버가 확정한 패링 결과를 모든 머신에서 발행.
 * 이펙트 / 사운드 / 카메라 흔들림 같은 연출은 수신 측이 붙인다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCWOnParrySucceededSignature, const FCWParrySyncData&, ParrySyncData);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWParryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWParryComponent();

	// 로컬 플레이어의 패링 입력을 받아 서버 판정 요청으로 넘김
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Parry")
	void RequestParry(const FVector& CursorWorldLocation);

	// 패링 준비 비율을 반환
	// 0은 방금 사용한 상태, 1은 다시 사용할 수 있는 상태
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Parry")
	float GetCooldownReadyRatio() const;

protected:
	// 클라이언트에서 서버로 패링 판정을 요청
	UFUNCTION(Server, Reliable)
	void Server_RequestParry(FVector_NetQuantize CursorWorldLocation, float ClientInputTime);

	// 서버가 확정한 패링 결과를 연출용으로 모든 클라이언트에 전달
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ParrySucceeded(const FCWParrySyncData& ParrySyncData);

private:
	// 서버에서 쿨타임을 확인하고 주변 위협 차량에 패링 판정을 시도
	void HandleServerParryRequest(const FVector& CursorWorldLocation, float ClientInputTime);

	// 커서 입력을 트래픽 쪽이 판정할 수 있는 시도 정보로 만든다
	FCWParryAttemptContext BuildParryAttemptContext(AActor& Parrier, const FVector& CursorWorldLocation, float ClientInputTime) const;

	// 서버 판정 결과를 연출 측이 쓸 동기화 데이터로 만든다
	FCWParrySyncData BuildParrySyncData(const FCWParryAttemptContext& ParryAttemptContext, ACWBaseVehicle& ParriedVehicle) const;

	/**
	 * 이 사람의 화면이 서버보다 얼마나 뒤처져 있는가(초). 판정 시 차량을 되돌릴 양이다.
	 *
	 * 서버에서만 의미가 있고, 호스트가 조종하는 캐릭터는 0 이다.
	 */
	float ResolveViewRewindSeconds() const;

	// 로컬 입력 남발을 막기 위한 클라이언트 측 쿨타임 검사
	bool CanUseLocalCooldown() const;

	// 실제 판정 남발을 막기 위한 서버 측 쿨타임 검사
	bool CanUseServerCooldown() const;

	// 로컬 입력 쿨타임 시간을 갱신
	void UpdateLocalCooldown();

	// 서버 판정 쿨타임 시간을 갱신
	void UpdateServerCooldown();

private:
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Parry", meta = (ClampMin = "0.0"))
	float ParryCooldown = 1.f;

	/**
	 * 패링이 닿는 거리(cm). 차체 표면 기준으로 잰다. 0 이면 거리 제한 없음.
	 *
	 * 기본값 0. 거리 조건을 따로 두면 판정 윈도우와 이중으로 걸린다.
	 * 윈도우 안에 들어온 차량의 표면 거리는 이미 (속도 x 윈도우)로 정해진다.
	 * 750cm/s 차량에 윈도우 0.3초면 225cm 이므로, 반경을 그보다 좁게 주면 성공 자체가 불가능하다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Parry", meta = (ClampMin = "0.0"))
	float ParryRadius = 0.f;

	/**
	 * 커서 방향을 중심으로 한 부채꼴 전체 각(도). 360 이면 방향 제한 없음.
	 *
	 * 기본값 360. 커서는 조준이 아니라 차를 튕겨 보낼 방향이다.
	 * 각도를 좁히면 "차를 저쪽으로 날리려고 반대편을 찍는" 조작이 전부 실패한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Parry", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float ParryAngle = 360.f;

	/**
	 * 랙 보정으로 차량을 되돌릴 수 있는 상한(초). 0 이면 보정을 아예 끈다.
	 *
	 * 보정을 하지 않으면 핑이 있는 사람은 윈도우(0.3초)의 뒷부분을 통째로 잃는다.
	 * 서버가 그 사람이 본 것보다 왕복 시간만큼 뒤의 상황으로 판정하기 때문에,
	 * 화면에서 "지금이다" 싶은 마지막 순간에 누르면 서버에서는 이미 지나친 차가 된다.
	 *
	 * 그렇다고 무제한으로 되돌리면 다른 사람 화면에서 한참 전에 지나간 차가 갑자기
	 * 튕겨나가는 것으로 보인다. 되돌린 만큼은 남들이 보기에 과거를 고치는 것이기 때문이다.
	 *
	 * 0.2 인 이유: 기본 속도 750cm/s 로 150cm, 차 길이(440cm)의 3분의 1 정도다.
	 * 그 정도면 쳐낸 사람과 차가 아직 겹쳐 보이는 범위라 남들 화면에서도 납득이 된다.
	 * 랜 환경 핑은 대부분 이 밑이라 실제로는 거의 잘리지 않는다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Parry", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxViewRewindSeconds = 0.2f;

	float LastLocalParryRequestTime = -999.f;

	float LastServerParryRequestTime = -999.f;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Crosswalk|Parry")
	FCWOnParrySucceededSignature OnParrySucceeded;
};
