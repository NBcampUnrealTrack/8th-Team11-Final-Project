// CWPlayerState.cpp

#include "Core/CWPlayerState.h"

#include "CoreMinimal.h"
#include "Core/Room/CWRoomGameState.h"
#include "Net/UnrealNetwork.h"

#pragma region PlayerStateLifecycle

void ACWPlayerState::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps
) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 서버의 준비 상태를 모든 클라이언트에 복제
	DOREPLIFETIME(ACWPlayerState, bIsReady);
}

void ACWPlayerState::OnRep_PlayerName()
{
	Super::OnRep_PlayerName();

	// 이름 복제가 끝난 시점의 최신 플레이어 목록을 UI에 알림
	NotifyRoomPlayersChanged();
}

#pragma endregion

#pragma region ReadyState

bool ACWPlayerState::IsReady() const
{
	return bIsReady;
}

void ACWPlayerState::ServerSetReady_Implementation(bool bNewReady)
{
	if (bIsReady == bNewReady)
	{
		return;
	}

	// 준비 상태는 서버에서만 확정하고 이후 클라이언트에 복제
	bIsReady = bNewReady;

	// Listen Server에서는 RepNotify가 자동 호출되지 않으므로 직접 알림
	NotifyRoomPlayersChanged();

	// 준비 상태 변경을 가능한 빨리 클라이언트에 복제
	ForceNetUpdate();
}

void ACWPlayerState::OnRep_IsReady()
{
	// 복제된 준비 상태를 사용하는 방 UI가 목록을 다시 읽도록 알림
	NotifyRoomPlayersChanged();
}

void ACWPlayerState::NotifyRoomPlayersChanged() const
{
	UWorld* World = GetWorld();

	if (!IsValid(World))
	{
		return;
	}

	ACWRoomGameState* RoomGameState =
		World->GetGameState<ACWRoomGameState>();

	if (!IsValid(RoomGameState))
	{
		return;
	}

	// GameState가 소유한 단일 변경 이벤트를 통해 UI 계층에 전달
	RoomGameState->NotifyRoomPlayersChanged();
}

#pragma endregion
