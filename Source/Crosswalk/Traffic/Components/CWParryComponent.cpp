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

	// 패링 성공 차량 방향 전환/2초 후 제거 처리를 연결

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