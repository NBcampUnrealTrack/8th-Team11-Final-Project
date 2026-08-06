// Copyright Crosswalk Team. All Rights Reserved.
// CWGameState.cpp

#include "Core/CWGameState.h"

#include "Net/UnrealNetwork.h"

void ACWGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACWGameState, CompletedObjectiveCount);
	DOREPLIFETIME(ACWGameState, TotalObjectiveCount);
	DOREPLIFETIME(ACWGameState, StageState);
	DOREPLIFETIME(ACWGameState, Objectives);
}

void ACWGameState::SetObjectiveCounts(int32 InCompletedCount, int32 InTotalCount)
{
	if (!HasAuthority())
	{
		return;
	}

	if (CompletedObjectiveCount == InCompletedCount && TotalObjectiveCount == InTotalCount)
	{
		return;
	}

	CompletedObjectiveCount = InCompletedCount;
	TotalObjectiveCount = InTotalCount;

	// 서버는 OnRep 이 돌지 않으므로 같은 알림을 여기서 직접 낸다.
	OnStageProgressChanged.Broadcast(this, CompletedObjectiveCount, TotalObjectiveCount);
}

void ACWGameState::SetStageState(ECWStageState NewStageState)
{
	if (!HasAuthority() || StageState == NewStageState)
	{
		return;
	}

	StageState = NewStageState;

	OnStageStateChanged.Broadcast(this, StageState);
}

bool ACWGameState::SetObjectives(const TArray<FCWStageObjective>& NewObjectives)
{
	if (!HasAuthority() || Objectives == NewObjectives)
	{
		return false;
	}

	Objectives = NewObjectives;

	OnStageObjectivesChanged.Broadcast(this);

	return true;
}

float ACWGameState::GetStageProgress() const
{
	if (TotalObjectiveCount <= 0)
	{
		return 0.f;
	}

	return static_cast<float>(CompletedObjectiveCount) / static_cast<float>(TotalObjectiveCount);
}

void ACWGameState::OnRep_ObjectiveCounts()
{
	OnStageProgressChanged.Broadcast(this, CompletedObjectiveCount, TotalObjectiveCount);
}

void ACWGameState::OnRep_StageState()
{
	OnStageStateChanged.Broadcast(this, StageState);
}

void ACWGameState::OnRep_Objectives()
{
	OnStageObjectivesChanged.Broadcast(this);
}
