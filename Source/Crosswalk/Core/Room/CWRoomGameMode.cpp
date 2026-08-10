// CWRoomGameMode.cpp

#include "Core/Room/CWRoomGameMode.h"
#include "CoreMinimal.h"
#include "Core/CWPlayerState.h"
#include "Core/Room/CWRoomGameState.h"
#include "Engine/World.h"

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

#pragma region GameStart

bool ACWRoomGameMode::TryStartGame()
{
	// 실제 맵 이동은 서버에서만 처리
	if (!HasAuthority())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("RoomGameMode: game start was requested without authority.")
		);

		return false;
	}

	const ACWRoomGameState* RoomGameState =
		GetGameState<ACWRoomGameState>();

	if (!IsValid(RoomGameState))
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("RoomGameMode: RoomGameState is invalid.")
		);

		return false;
	}

	// 최소 인원과 전원 준비 조건을 확인
	if (!RoomGameState->CanStartGame())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("RoomGameMode: game start conditions are not satisfied.")
		);

		return false;
	}

	if (GameplayMap.IsNull())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("RoomGameMode: GameplayMap is not configured.")
		);

		return false;
	}

	UWorld* World = GetWorld();

	if (!IsValid(World))
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("RoomGameMode: World is invalid.")
		);

		return false;
	}

	// 이미 트래블이 예약됐다면 중복 시작 요청을 무시
	if (!World->NextURL.IsEmpty())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("RoomGameMode: travel is already pending.")
		);

		return false;
	}

	const FString GameplayMapPath =
		GameplayMap
		.ToSoftObjectPath()
		.GetLongPackageName();

	if (GameplayMapPath.IsEmpty())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("RoomGameMode: GameplayMap path is empty.")
		);

		return false;
	}

	// Listen Server에 연결된 모든 플레이어와 함께 인게임 맵으로 이동
	const bool bTravelStarted =
		World->ServerTravel(
			GameplayMapPath
		);

	if (!bTravelStarted)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("RoomGameMode: failed to start ServerTravel. Map=%s"),
			*GameplayMapPath
		);

		return false;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("RoomGameMode: ServerTravel started. Map=%s"),
		*GameplayMapPath
	);

	return true;
}

#pragma endregion
