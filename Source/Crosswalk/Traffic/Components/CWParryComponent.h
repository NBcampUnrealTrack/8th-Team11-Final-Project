// Copyright Crosswalk Team. All Rights Reserved.
// CWParryComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CWParryComponent.generated.h"

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
	void Server_RequestParry(FVector_NetQuantize CursorWorldLocation);

private:
	// 서버에서 쿨타임을 확인하고 주변 위협 차량에 패링 판정을 시도
	void HandleServerParryRequest(const FVector& CursorWorldLocation);

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

	float LastLocalParryRequestTime = -999.f;

	float LastServerParryRequestTime = -999.f;
};