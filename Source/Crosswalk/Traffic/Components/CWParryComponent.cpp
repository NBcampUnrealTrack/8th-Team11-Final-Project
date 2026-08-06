// Copyright Crosswalk Team. All Rights Reserved.
// CWParryComponent.cpp

#include "Traffic/Components/CWParryComponent.h"

#include "GameFramework/Actor.h"
#include "Traffic/BaseVehicle.h"
#include "Traffic/TrafficSubsystem.h"

UCWParryComponent::UCWParryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCWParryComponent::RequestParry(const FVector& CursorWorldLocation)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || !CanUseLocalCooldown())
	{
		return;
	}

	UpdateLocalCooldown();

	if (OwnerActor->HasAuthority())
	{
		HandleServerParryRequest(CursorWorldLocation);
		return;
	}

	Server_RequestParry(CursorWorldLocation);
}

void UCWParryComponent::Server_RequestParry_Implementation(FVector_NetQuantize CursorWorldLocation)
{
	HandleServerParryRequest(CursorWorldLocation);
}

void UCWParryComponent::HandleServerParryRequest(const FVector& CursorWorldLocation)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || !OwnerActor->HasAuthority() || !CanUseServerCooldown())
	{
		return;
	}

	UpdateServerCooldown();

	UTrafficSubsystem* TrafficSubsystem = UTrafficSubsystem::Get(OwnerActor);
	if (TrafficSubsystem == nullptr)
	{
		return;
	}

	ABaseVehicle* ParriedVehicle = TrafficSubsystem->TryParryNearestThreat(OwnerActor);
	if (ParriedVehicle == nullptr)
	{
		return;
	}

	// 차량 정지와 2초 뒤 제거는 ABaseVehicle::TryParry가 이미 끝냈다.
	// 여기서는 서버가 확정한 결과만 연출용으로 뿌린다. 실패는 보내지 않는다.
	Multicast_ParrySucceeded(BuildParrySyncData(*OwnerActor, *ParriedVehicle));
}

void UCWParryComponent::Multicast_ParrySucceeded_Implementation(const FCWParrySyncData& ParrySyncData)
{
	OnParrySucceeded.Broadcast(ParrySyncData);
}

FCWParrySyncData UCWParryComponent::BuildParrySyncData(const AActor& Parrier, ABaseVehicle& ParriedVehicle) const
{
	FCWParrySyncData ParrySyncData;
	ParrySyncData.TargetVehicle = &ParriedVehicle;
	ParrySyncData.VehicleType = ParriedVehicle.GetVehicleType();
	ParrySyncData.bParrySucceeded = true;

	// 연출 기준점은 클라이언트가 보낸 커서가 아니라 서버가 확정한 차량 위치다.
	ParrySyncData.ParryLocation = ParriedVehicle.GetActorLocation();

	// 차량이 밀려나는 방향. 두 위치가 겹칠 만큼 가까우면 차량 진행 반대 방향으로 대체한다.
	const FVector ParrierToVehicle = ParrySyncData.ParryLocation - Parrier.GetActorLocation();
	ParrySyncData.ParryDirection = ParrierToVehicle.IsNearlyZero()
		? -ParriedVehicle.GetActorForwardVector()
		: ParrierToVehicle.GetSafeNormal();

	// 연출 강도는 차량이 실제로 내던 속도를 그대로 넘긴다. 세기 환산은 수신 측이 정한다.
	ParrySyncData.VehicleReactionStrength = ParriedVehicle.GetEffectiveSpeed();

	return ParrySyncData;
}

bool UCWParryComponent::CanUseLocalCooldown() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	return World->GetTimeSeconds() - LastLocalParryRequestTime >= ParryCooldown;
}

bool UCWParryComponent::CanUseServerCooldown() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	return World->GetTimeSeconds() - LastServerParryRequestTime >= ParryCooldown;
}

void UCWParryComponent::UpdateLocalCooldown()
{
	if (const UWorld* World = GetWorld())
	{
		LastLocalParryRequestTime = World->GetTimeSeconds();
	}
}

void UCWParryComponent::UpdateServerCooldown()
{
	if (const UWorld* World = GetWorld())
	{
		LastServerParryRequestTime = World->GetTimeSeconds();
	}
}