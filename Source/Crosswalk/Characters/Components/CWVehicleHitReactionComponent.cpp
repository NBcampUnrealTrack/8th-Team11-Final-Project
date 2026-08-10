// Copyright Crosswalk Team. All Rights Reserved.
// CWVehicleHitReactionComponent.cpp

#include "Characters/Components/CWVehicleHitReactionComponent.h"

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

void UCWVehicleHitReactionComponent::NotifyLanded()
{
	const AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || !OwnerActor->HasAuthority())
	{
		return;
	}

	// 피격과 무관한 평소 착지는 무시한다.
	if (!bHitLocked || !bWaitingForLanding)
	{
		return;
	}

	StartHitLockCountdown();
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

	// 기획서 6.2 - 잠금 시간은 착지한 뒤부터 잰다. 여기서는 날아가는 동안 잠그기만 한다.
	bWaitingForLanding = true;

	OnHitReactionStarted(LastHitContext);
	OnHitLockChanged.Broadcast(this, true);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitLockTimerHandle);

		// 착지 통보가 끝내 오지 않아도 언젠가는 풀려야 한다.
		World->GetTimerManager().SetTimer(
			LandingWaitTimerHandle,
			this,
			&UCWVehicleHitReactionComponent::StartHitLockCountdown,
			MaxLandingWait,
			false
		);
	}
}

void UCWVehicleHitReactionComponent::StartHitLockCountdown()
{
	bWaitingForLanding = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LandingWaitTimerHandle);
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
	bWaitingForLanding = false;

	OnHitReactionFinished();
	OnHitLockChanged.Broadcast(this, false);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitLockTimerHandle);
		World->GetTimerManager().ClearTimer(LandingWaitTimerHandle);
	}
}

void UCWVehicleHitReactionComponent::OnRep_HitLocked()
{
	// 서버는 Start/Finish 에서 이미 발행했으므로 여기서는 클라이언트만 맞춰준다.
	OnHitLockChanged.Broadcast(this, bHitLocked);

	if (bHitLocked)
	{
		OnHitReactionStarted(LastHitContext);
		return;
	}

	OnHitReactionFinished();
}