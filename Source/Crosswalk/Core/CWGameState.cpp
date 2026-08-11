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

void ACWGameState::AddPlayerState(
	APlayerState* PlayerState
)
{
	// 엔진이 PlayerArray에 플레이어를 먼저 추가
	Super::AddPlayerState(PlayerState);

	// 최신 PlayerArray를 UI 계층에 알림
	NotifyPlayerListChanged();
}

void ACWGameState::RemovePlayerState(
	APlayerState* PlayerState
)
{
	// 엔진이 PlayerArray에서 플레이어를 먼저 제거
	Super::RemovePlayerState(PlayerState);

	// 제거된 최신 PlayerArray를 UI 계층에 알림
	NotifyPlayerListChanged();
}

void ACWGameState::NotifyPlayerListChanged()
{
	// 서버와 클라이언트가 각자 보유한 최신 PlayerArray를 읽도록 알림
	OnPlayerListChanged.Broadcast();
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
