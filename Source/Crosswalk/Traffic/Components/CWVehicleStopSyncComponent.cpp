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
	 * 풀링으로 재사용된 차량이 지난 주행에 정지 상태로 반납됐다가, 그 상태 그대로
	 * 다시 나올 수 있다. InitializeOnLane 은 이 경우 알리지 않고 조용히 되돌리므로
	 * (ACWBaseVehicle::InitializeOnLane 주석 참고), 델리게이트만 기다리지 않고
	 * 스폰 시점의 실제 상태를 여기서 한 번 직접 확인한다.
	 */
	if (OwnerVehicle->IsStoppedAtStopLine())
	{
		HandleStopStateChanged(OwnerVehicle, true, OwnerVehicle->GetStopTargetDistance());
	}
}

void UCWVehicleStopSyncComponent::HandleStopStateChanged(ACWBaseVehicle* Vehicle, bool bNowStopped, float TargetDistance)
{
	GetWorld()->GetTimerManager().ClearTimer(CorrectionTimerHandle);

	if (bNowStopped)
	{
		GetWorld()->GetTimerManager().SetTimer(CorrectionTimerHandle, this,
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
	 * 델리게이트를 못 받았을 수 있는 상태 변화(풀링 재사용 등)를 여기서 다시 확인한다.
	 * InitializeOnLane 은 재사용 시 정지 상태를 알리지 않고 조용히 되돌리기 때문에,
	 * 델리게이트만 믿으면 이 타이머가 계속 돌면서 이제 막 새로 출발한 차를 엉뚱한
	 * 목표로 당길 수 있다.
	 */
	if (!OwnerVehicle->IsStoppedAtStopLine() || OwnerVehicle->GetStopTargetDistance() < 0.f)
	{
		GetWorld()->GetTimerManager().ClearTimer(CorrectionTimerHandle);
		return;
	}

	const float StopError = OwnerVehicle->GetStopTargetDistance() - OwnerVehicle->GetDistanceAlongLane();

	if (FMath::Abs(StopError) <= SnapTolerance)
	{
		OwnerVehicle->AdjustDistanceAlongLane(StopError);
		return;
	}

	const float MaxStep = CorrectionSpeed * CorrectionInterval;
	OwnerVehicle->AdjustDistanceAlongLane(FMath::Clamp(StopError, -MaxStep, MaxStep));
}
