// CWObjectiveViewModel.cpp

#include "UI/ViewModel/CWObjectiveViewModel.h"
#include "CoreMinimal.h"
#include "Core/CWGameState.h"
#include "GameFramework/GameStateBase.h"

#pragma region ObjectiveData

void UCWObjectiveViewModel::SetObjectives(
	const TArray<FCWStageObjective>& InObjectives
)
{
	// 값이 실제로 변경된 경우에만 MVVM 알림 발생
	UE_MVVM_SET_PROPERTY_VALUE(Objectives, InObjectives);
}

void UCWObjectiveViewModel::SetIsStageCleared(
	bool bInIsStageCleared
)
{
	// 클리어 여부가 실제로 변경된 경우에만 MVVM 알림 발생
	UE_MVVM_SET_PROPERTY_VALUE(
		bIsStageCleared,
		bInIsStageCleared
	);
}

#pragma endregion

#pragma region GameStateBinding

UClass* UCWObjectiveViewModel::GetGameStateClass() const
{
	return ACWGameState::StaticClass();
}

void UCWObjectiveViewModel::SubscribeToGameState(
	AGameStateBase* InGameState
)
{
	ACWGameState* GameState =
		Cast<ACWGameState>(InGameState);

	if (!IsValid(GameState))
	{
		return;
	}

	// 복제된 목표 목록이 변경될 때 알림 수신
	GameState->OnStageObjectivesChanged.AddDynamic(
		this,
		&UCWObjectiveViewModel::HandleStageObjectivesChanged
	);

	// 복제된 스테이지 상태가 변경될 때 알림 수신
	GameState->OnStageStateChanged.AddDynamic(
		this,
		&UCWObjectiveViewModel::HandleStageStateChanged
	);
}

void UCWObjectiveViewModel::UnsubscribeFromGameState(
	AGameStateBase* InGameState
)
{
	ACWGameState* GameState =
		Cast<ACWGameState>(InGameState);

	if (!IsValid(GameState))
	{
		return;
	}

	GameState->OnStageObjectivesChanged.RemoveDynamic(
		this,
		&UCWObjectiveViewModel::HandleStageObjectivesChanged
	);

	GameState->OnStageStateChanged.RemoveDynamic(
		this,
		&UCWObjectiveViewModel::HandleStageStateChanged
	);
}

void UCWObjectiveViewModel::RefreshFromGameState()
{
	ACWGameState* GameState =
		Cast<ACWGameState>(GetBoundGameState());

	if (!IsValid(GameState))
	{
		return;
	}

	// OnRep가 이미 지난 경우를 위해 현재 값을 즉시 반영
	SetObjectives(GameState->GetObjectives());
	SetIsStageCleared(GameState->IsStageCleared());
}

void UCWObjectiveViewModel::ResetViewModelState()
{
	// 이전 월드의 목표가 다음 월드에 남지 않도록 초기화
	SetObjectives(TArray<FCWStageObjective>());

	// 이전 스테이지의 클리어 상태가 다음 월드에 남지 않도록 초기화
	SetIsStageCleared(false);
}

void UCWObjectiveViewModel::HandleStageObjectivesChanged(
	ACWGameState* InGameState
)
{
	if (!IsValid(InGameState))
	{
		return;
	}

	// 서버에서 갱신·복제된 최신 목표 배열 저장
	SetObjectives(InGameState->GetObjectives());
}

void UCWObjectiveViewModel::HandleStageStateChanged(
	ACWGameState* InGameState,
	ECWStageState InStageState
)
{
	if (!IsValid(InGameState))
	{
		return;
	}

	// GameMode가 확정하고 복제한 스테이지 상태를 그대로 반영
	SetIsStageCleared(
		InStageState == ECWStageState::Cleared
	);
}

#pragma endregion
