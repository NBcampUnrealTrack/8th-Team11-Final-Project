// Copyright Crosswalk Team. All Rights Reserved.
// CWVehicleStopSyncComponent.cpp

#include "Traffic/Components/CWVehicleStopSyncComponent.h"
#include "Traffic/CWBaseVehicle.h"

UCWVehicleStopSyncComponent::UCWVehicleStopSyncComponent()
{
	// 틱이 아니라 타이머로만 돈다 (RunCorrection 참고).
	PrimaryComponentTick.bCanEverTick = false;
}

void UCWVehicleStopSyncComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerVehicle = Cast<ACWBaseVehicle>(GetOwner());
	if (!OwnerVehicle)
	{
		return;
	}

	OwnerVehicle->OnStopStateChanged.AddDynamic(this, &UCWVehicleStopSyncComponent::HandleStopStateChanged);

	/**
	 * 바인딩 전에 이미 정지 상태가 됐을 수 있다. 델리게이트만 기다리지 않고
	 * 지금 상태를 한 번 직접 확인한다. (초기화 순서에 기대지 않기 위한 것이다)
	 */
	if (OwnerVehicle->IsStoppedAtStopLine())
	{
		HandleStopStateChanged(OwnerVehicle, true, OwnerVehicle->GetStopTargetDistance());
	}
}

void UCWVehicleStopSyncComponent::HandleStopStateChanged(ACWBaseVehicle* Vehicle, bool bNowStopped, float TargetDistance)
{
	// 월드가 내려가는 중에 마지막 정지 알림이 올 수 있다. 그때는 걸 타이머도 없다.
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(CorrectionTimerHandle);

	if (bNowStopped)
	{
		World->GetTimerManager().SetTimer(CorrectionTimerHandle, this,
			&UCWVehicleStopSyncComponent::RunCorrection, CorrectionInterval, true);
	}
}

void UCWVehicleStopSyncComponent::RunCorrection()
{
	if (!OwnerVehicle)
	{
		return;
	}

	/**
	 * 델리게이트 없이 바뀐 상태를 여기서 다시 확인한다. InitializeOnLane 은 정지 상태를
	 * 알리지 않고 조용히 되돌리므로, 델리게이트만 믿으면 이 타이머가 계속 돌면서
	 * 이제 막 출발한 차를 엉뚱한 목표로 당길 수 있다.
	 */
	if (!OwnerVehicle->IsStoppedAtStopLine() || OwnerVehicle->GetStopTargetDistance() < 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(CorrectionTimerHandle);
		}
		return;
	}

	const float StopError = OwnerVehicle->GetStopTargetDistance() - OwnerVehicle->GetDistanceAlongLane();

	// SnapTolerance 가 0 이면 이 분기는 오차가 정확히 0 일 때만 걸린다. 그때는 밀 것도 없다.
	if (FMath::Abs(StopError) <= SnapTolerance)
	{
		OwnerVehicle->AdjustDistanceAlongLane(StopError);
		return;
	}

	const float MaxStep = CorrectionSpeed * CorrectionInterval;
	OwnerVehicle->AdjustDistanceAlongLane(FMath::Clamp(StopError, -MaxStep, MaxStep));
}
