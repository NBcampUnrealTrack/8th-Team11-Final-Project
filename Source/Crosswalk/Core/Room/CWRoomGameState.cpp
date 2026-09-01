// CWRoomGameState.cpp

#include "Core/Room/CWRoomGameState.h"
#include "CoreMinimal.h"
#include "Core/CWPlayerState.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

#pragma region GameStateLifecycle

void ACWRoomGameState::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps
) const
{
	Super::GetLifetimeReplicatedProps(
		OutLifetimeProps
	);

	// 서버의 최소 시작 인원을 모든 클라이언트에 공유
	DOREPLIFETIME(
		ACWRoomGameState,
		MinimumPlayersToStart
	);
}

void ACWRoomGameState::AddPlayerState(
	APlayerState* PlayerState
)
{
	Super::AddPlayerState(
		PlayerState
	);

	// PlayerArray 반영 후 최신 방 목록을 알림
	NotifyRoomPlayersChanged();
}

void ACWRoomGameState::RemovePlayerState(
	APlayerState* PlayerState
)
{
	Super::RemovePlayerState(
		PlayerState
	);

	// PlayerArray 제거 후 최신 방 목록을 알림
	NotifyRoomPlayersChanged();
}

#pragma endregion

#pragma region RoomPlayers

bool ACWRoomGameState::CanStartGame() const
{
	// 설정된 최소 인원보다 적으면 시작할 수 없음
	if (PlayerArray.Num() < MinimumPlayersToStart)
	{
		return false;
	}

	// 모든 방 플레이어의 준비 상태를 확인
	for (const TObjectPtr<APlayerState>& PlayerState : PlayerArray)
	{
		const ACWPlayerState* CWPlayerState =
			Cast<ACWPlayerState>(
				PlayerState.Get()
			);

		if (!IsValid(CWPlayerState))
		{
			return false;
		}

		// 준비하지 않은 플레이어가 있으면 시작할 수 없음
		if (!CWPlayerState->IsReady())
		{
			return false;
		}
	}

	return true;
}

void ACWRoomGameState::NotifyRoomPlayersChanged()
{
	// 방 UI가 현재 PlayerArray와 준비 상태를 다시 읽도록 알림
	OnRoomPlayersChanged.Broadcast();
}

#pragma endregion

#pragma region RoomConfiguration

void ACWRoomGameState::SetMinimumPlayersToStart(
	int32 InMinimumPlayers
)
{
	// 방 설정값은 서버에서만 변경
	if (!HasAuthority())
	{
		return;
	}

	// 최소 시작 인원은 반드시 한 명 이상으로 제한
	MinimumPlayersToStart =
		FMath::Max(
			1,
			InMinimumPlayers
		);

	// Listen Server의 로컬 UI도 즉시 갱신
	NotifyRoomPlayersChanged();

	// 변경된 값을 다음 복제 갱신에서 바로 전송
	ForceNetUpdate();
}

void ACWRoomGameState::OnRep_MinimumPlayersToStart()
{
	// 클라이언트가 새 시작 조건을 받으면 UI를 다시 계산
	NotifyRoomPlayersChanged();
}

#pragma endregion
