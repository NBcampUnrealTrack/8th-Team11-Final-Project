// CWStageSelectionNetworkViewModel.cpp

#include "UI/ViewModel/CWStageSelectionNetworkViewModel.h"
#include "Core/Stage/CWStageSelectionGameModeBase.h"
#include "Core/Stage/CWStageSelectionGameStateBase.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "UI/Progress/CWLocalStageProgress.h"

#pragma region StageSelectionRequests

bool UCWStageSelectionNetworkViewModel::RequestOpenStageSelection()
{
	UWorld* World = GetBoundWorld();

	if (!IsValid(World))
	{
		return false;
	}

	// GameMode는 서버에만 존재하므로 Listen Server 호스트에서만 유효함
	ACWStageSelectionGameModeBase* StageSelectionGameMode =
		World->GetAuthGameMode<ACWStageSelectionGameModeBase>();

	if (!IsValid(StageSelectionGameMode))
	{
		return false;
	}

	return StageSelectionGameMode->BeginStageSelection();
}

bool UCWStageSelectionNetworkViewModel::RequestSelectStage(FName StageId)
{
	if (!FCWLocalStageProgress::CanSelect(StageId)) return false;
	UWorld* World = GetBoundWorld();

	if (!IsValid(World))
	{
		return false;
	}

	ACWStageSelectionGameModeBase* StageSelectionGameMode =
		World->GetAuthGameMode<ACWStageSelectionGameModeBase>();

	if (!IsValid(StageSelectionGameMode))
	{
		return false;
	}

	return StageSelectionGameMode->SelectStage(StageId);
}

bool UCWStageSelectionNetworkViewModel::RequestCloseStageSelection()
{
	UWorld* World = GetBoundWorld();

	if (!IsValid(World))
	{
		return false;
	}

	ACWStageSelectionGameModeBase* StageSelectionGameMode =
		World->GetAuthGameMode<ACWStageSelectionGameModeBase>();

	if (!IsValid(StageSelectionGameMode))
	{
		return false;
	}

	return StageSelectionGameMode->CancelStageSelection();
}

bool UCWStageSelectionNetworkViewModel::RequestStartSelectedStage(FName StageId)
{
	if (!FCWLocalStageProgress::CanSelect(StageId)) return false;
	UWorld* World = GetBoundWorld();

	if (!IsValid(World))
	{
		return false;
	}

	ACWStageSelectionGameModeBase* StageSelectionGameMode =
		World->GetAuthGameMode<ACWStageSelectionGameModeBase>();

	if (!IsValid(StageSelectionGameMode))
	{
		return false;
	}

	return StageSelectionGameMode->TryStartGame(StageId);
}

#pragma endregion

#pragma region StageSelectionData

void UCWStageSelectionNetworkViewModel::RefreshStageSelectionState()
{
	const ACWStageSelectionGameStateBase* StageSelectionGameState =
		Cast<ACWStageSelectionGameStateBase>(GetBoundGameState());

	if (IsValid(StageSelectionGameState))
	{
		// 두 복제값과 로컬 호스트 여부를 항상 함께 다시 읽음
		SetIsStageSelectionOpen(
			StageSelectionGameState
			->IsStageSelectionOpen()
		);

		SetSelectedStageId(
			StageSelectionGameState
			->GetSelectedStageId()
		);

		SetIsHost(IsLocalHost());
	}
	else
	{
		// GameState가 없으면 이전 월드 상태를 남기지 않음
		SetIsStageSelectionOpen(false);
		SetSelectedStageId(NAME_None);
		SetIsHost(false);
	}

	// 모든 프로퍼티 반영 후 위젯 로직을 한 번만 실행하도록 알림
	OnStageSelectionChanged.Broadcast();
}

#pragma endregion

#pragma region GameStateBinding

UClass* UCWStageSelectionNetworkViewModel::GetGameStateClass() const
{
	// RoomGameState와 CWGameState가 모두 이 공용 부모를 상속함
	return ACWStageSelectionGameStateBase::StaticClass();
}

void UCWStageSelectionNetworkViewModel::SubscribeToGameState(AGameStateBase* InGameState)
{
	ACWStageSelectionGameStateBase* StageSelectionGameState =
		Cast<ACWStageSelectionGameStateBase>(InGameState);

	if (!IsValid(StageSelectionGameState))
	{
		return;
	}

	// GameState의 네이티브 이벤트를 구독함
	StageSelectionGameState->OnStageSelectionStateChanged.AddUObject(
		this,
		&UCWStageSelectionNetworkViewModel::HandleStageSelectionStateChanged
	);
}

void UCWStageSelectionNetworkViewModel::UnsubscribeFromGameState(AGameStateBase* InGameState)
{
	ACWStageSelectionGameStateBase* StageSelectionGameState =
		Cast<ACWStageSelectionGameStateBase>(InGameState);

	if (!IsValid(StageSelectionGameState))
	{
		return;
	}

	StageSelectionGameState->OnStageSelectionStateChanged.RemoveAll(this);
}

void UCWStageSelectionNetworkViewModel::RefreshFromGameState()
{
	// 구독 전 OnRep가 지나간 경우에도 현재 상태를 즉시 반영함
	RefreshStageSelectionState();
}

void UCWStageSelectionNetworkViewModel::ResetViewModelState()
{
	SetIsStageSelectionOpen(false);
	SetSelectedStageId(NAME_None);
	SetIsHost(false);
}

void UCWStageSelectionNetworkViewModel::HandleStageSelectionStateChanged()
{
	// 두 복제 프로퍼티가 별도로 알림을 발생시킬 수 있으므로
	// 이벤트마다 전체 선택 상태를 함께 다시 읽음
	RefreshStageSelectionState();
}

#pragma endregion

#pragma region ViewModelSetters

void UCWStageSelectionNetworkViewModel::SetIsStageSelectionOpen(bool bInOpen)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		bIsStageSelectionOpen,
		bInOpen
	);
}

void UCWStageSelectionNetworkViewModel::SetSelectedStageId(FName InStageId)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		SelectedStageId,
		InStageId
	);
}

void UCWStageSelectionNetworkViewModel::SetIsHost(bool bInIsHost)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		bIsHost,
		bInIsHost
	);
}

#pragma endregion
