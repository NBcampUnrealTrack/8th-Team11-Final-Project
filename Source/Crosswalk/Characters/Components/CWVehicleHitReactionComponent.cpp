// CWVehicleHitReactionComponent.cpp

#include "Characters/Components/CWVehicleHitReactionComponent.h"

#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace
{
	/**
	 * SetTimer 는 0 이하를 받으면 타이머를 걸지 않고 조용히 지운다.
	 *
	 * 잠금 시간 세 개는 ClampMin 0 이라 BP 에서 0 을 넣을 수 있다. 그러면 다음 단계가 영영
	 * 안 불려서 플레이어가 잠긴 채로 남는다. 0 은 "기다리지 않는다" 로 읽고 다음 틱에 넘긴다.
	 */
	float HitReactionTimerRate(float Seconds)
	{
		return FMath::Max(Seconds, KINDA_SMALL_NUMBER);
	}
}

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

void UCWVehicleHitReactionComponent::NotifyGetUpFinished()
{
	const AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || !OwnerActor->HasAuthority())
	{
		return;
	}

	// 기상을 요청한 적이 없는데 들어온 통보는 무시한다. 안전장치가 이미 잠금을 푼 뒤일 수도 있다.
	if (!bHitLocked || !bWaitingForGetUp)
	{
		return;
	}

	FinishHitReaction();
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
	bWaitingForGetUp = false;

	OnHitReactionStarted(LastHitContext);
	OnHitLockChanged.Broadcast(this, true);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitLockTimerHandle);

		// 앞선 피격의 기상 대기 타이머가 남아 있으면 이번 잠금을 중간에 풀어 버린다.
		World->GetTimerManager().ClearTimer(GetUpWaitTimerHandle);

		// 착지 통보가 끝내 오지 않아도 언젠가는 풀려야 한다.
		World->GetTimerManager().SetTimer(
			LandingWaitTimerHandle,
			this,
			&UCWVehicleHitReactionComponent::StartHitLockCountdown,
			HitReactionTimerRate(MaxLandingWait),
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
			&UCWVehicleHitReactionComponent::RequestGetUp,
			HitReactionTimerRate(HitLockDuration),
			false
		);
	}
}

void UCWVehicleHitReactionComponent::RequestGetUp()
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || !OwnerActor->HasAuthority())
	{
		return;
	}

	bWaitingForGetUp = true;

	// 잠금은 아직 그대로다. 일어나는 동안 조작이 들어가면 몸이 애니메이션과 따로 논다.
	OnGetUpRequested.Broadcast(this);

	/**
	 * 기상이 Broadcast 안에서 바로 끝났으면 안전장치를 걸 필요가 없다.
	 * (래그돌이 꺼져 있거나 기상 몬타주가 없을 때 NotifyGetUpFinished 가 그 자리에서 불린다)
	 * 여기서 걸면 잠금이 이미 풀린 뒤에 타이머가 남아, 다음 피격의 잠금을 끊는다.
	 */
	if (!bHitLocked || !bWaitingForGetUp)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitLockTimerHandle);

		// 기상 완료 통보가 끝내 오지 않아도 언젠가는 풀려야 한다.
		World->GetTimerManager().SetTimer(
			GetUpWaitTimerHandle,
			this,
			&UCWVehicleHitReactionComponent::FinishHitReaction,
			HitReactionTimerRate(MaxGetUpWait),
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
	bWaitingForGetUp = false;

	OnHitReactionFinished();
	OnHitLockChanged.Broadcast(this, false);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitLockTimerHandle);
		World->GetTimerManager().ClearTimer(LandingWaitTimerHandle);
		World->GetTimerManager().ClearTimer(GetUpWaitTimerHandle);
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