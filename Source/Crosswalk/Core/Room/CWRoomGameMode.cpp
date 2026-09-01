// CWRoomGameMode.cpp

#include "Core/Room/CWRoomGameMode.h"
#include "CoreMinimal.h"
#include "Core/CWPlayerState.h"
#include "Core/Room/CWRoomGameState.h"

#pragma region GameModeLifecycle

ACWRoomGameMode::ACWRoomGameMode()
{
	// 방에서 인게임으로 이동하는 동안 클라이언트 연결을 유지
	bUseSeamlessTravel = true;

	// 방 상태는 기존 인게임 상태 클래스와 분리
	GameStateClass =
		ACWRoomGameState::StaticClass();

	PlayerStateClass =
		ACWPlayerState::StaticClass();
}

void ACWRoomGameMode::InitGameState()
{
	Super::InitGameState();

	ACWRoomGameState* RoomGameState =
		GetGameState<ACWRoomGameState>();

	if (!IsValid(RoomGameState))
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("RoomGameMode: RoomGameState is invalid during initialization.")
		);

		return;
	}

	// BP GameMode의 최소 시작 인원을 복제되는 GameState에 전달
	RoomGameState->SetMinimumPlayersToStart(
		MinimumPlayersToStart
	);
}

#pragma endregion

#pragma region StageSelectionValidation

bool ACWRoomGameMode::CanOpenStageSelection() const
{
	const ACWRoomGameState* RoomGameState =
		GetGameState<ACWRoomGameState>();

	if (!IsValid(RoomGameState))
	{
		return false;
	}

	// 최소 인원과 전원 준비 조건을 만족해야 선택창을 열 수 있음
	return RoomGameState->CanStartGame();
}

bool ACWRoomGameMode::CanStartSelectedStage() const
{
	const ACWRoomGameState* RoomGameState =
		GetGameState<ACWRoomGameState>();

	if (!IsValid(RoomGameState))
	{
		return false;
	}

	// 선택창이 열린 사이 준비 상태가 바뀔 수 있으므로 확정 시 다시 검사함
	return RoomGameState->CanStartGame();
}

#pragma endregion
