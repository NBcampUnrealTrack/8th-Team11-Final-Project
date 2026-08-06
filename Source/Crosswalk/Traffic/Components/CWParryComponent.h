// Copyright Crosswalk Team. All Rights Reserved.
// CWParryComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Structs/CWParryAttemptContext.h"
#include "Data/Structs/CWParrySyncData.h"
#include "CWParryComponent.generated.h"

class ABaseVehicle;

/**
 * 서버가 확정한 패링 결과를 모든 머신에서 발행.
 * 이펙트 / 사운드 / 카메라 흔들림 같은 연출은 수신 측이 붙인다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnParrySucceededSignature, const FCWParrySyncData&, ParrySyncData);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWParryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWParryComponent();

	// 로컬 플레이어의 패링 입력을 받아 서버 판정 요청으로 넘김
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Parry")
	void RequestParry(const FVector& CursorWorldLocation);

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
	FCWParrySyncData BuildParrySyncData(const AActor& Parrier, ABaseVehicle& ParriedVehicle) const;

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

	// 패링이 닿는 거리(cm). 차체 표면 기준으로 잰다. 0 이면 거리 제한 없음
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Parry", meta = (ClampMin = "0.0"))
	float ParryRadius = 150.f;

	// 커서 방향을 중심으로 한 부채꼴 전체 각(도). 360 이면 방향 제한 없음
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Parry", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float ParryAngle = 90.f;

	float LastLocalParryRequestTime = -999.f;

	float LastServerParryRequestTime = -999.f;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Crosswalk|Parry")
	FOnParrySucceededSignature OnParrySucceeded;
};