// Copyright Crosswalk Team. All Rights Reserved.
// CWVehicleHitReactionComponent.cpp

#include "Traffic/Components/CWVehicleHitReactionComponent.h"

#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UCWVehicleHitReactionComponent::UCWVehicleHitReactionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCWVehicleHitReactionComponent::HandleVehicleHit(const FCWVehicleHitContext& HitContext)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || !OwnerActor->HasAuthority())
	{
		return;
	}

	if (bHitLocked)
	{
		return;
	}

	StartHitReaction(HitContext);
}

bool UCWVehicleHitReactionComponent::IsHitLocked() const
{
	return bHitLocked;
}

FCWVehicleHitContext UCWVehicleHitReactionComponent::GetLastHitContext() const
{
	return LastHitContext;
}

void UCWVehicleHitReactionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCWVehicleHitReactionComponent, bHitLocked);
	DOREPLIFETIME(UCWVehicleHitReactionComponent, LastHitContext);
}

void UCWVehicleHitReactionComponent::StartHitReaction(const FCWVehicleHitContext& HitContext)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || !OwnerActor->HasAuthority())
	{
		return;
	}

	LastHitContext = HitContext;
	bHitLocked = true;

	OnHitReactionStarted(LastHitContext);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitLockTimerHandle);
		World->GetTimerManager().SetTimer(
			HitLockTimerHandle,
			this,
			&UCWVehicleHitReactionComponent::FinishHitReaction,
			HitLockDuration,
			false
		);
	}
}

void UCWVehicleHitReactionComponent::FinishHitReaction()
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || !OwnerActor->HasAuthority())
	{
		return;
	}

	bHitLocked = false;
	OnHitReactionFinished();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitLockTimerHandle);
	}
}

void UCWVehicleHitReactionComponent::OnRep_HitLocked()
{
	if (bHitLocked)
	{
		OnHitReactionStarted(LastHitContext);
		return;
	}

	OnHitReactionFinished();
}