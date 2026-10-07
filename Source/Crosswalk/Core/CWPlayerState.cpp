// CWPlayerState.cpp

#include "Core/CWPlayerState.h"

#include "CoreMinimal.h"
#include "Core/CWGameState.h"
#include "Core/Room/CWRoomGameState.h"
#include "Net/UnrealNetwork.h"
#include "Characters/Components/CWWearableComponent.h"

#pragma region PlayerStateLifecycle

ACWPlayerState::ACWPlayerState()
{
	WearableComponent = CreateDefaultSubobject<UCWWearableComponent>(TEXT("WearableComponent"));
}

void ACWPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);
	if (ACWPlayerState* Destination = Cast<ACWPlayerState>(PlayerState))
	{
		WearableComponent->CopySelectionTo(Destination->WearableComponent);
		Destination->SetFilmObserver(bIsFilmObserver);
	}
}

void ACWPlayerState::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps
) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 서버의 준비 상태를 모든 클라이언트에 복제
	DOREPLIFETIME(ACWPlayerState, bIsReady);
	DOREPLIFETIME(ACWPlayerState, bIsFilmObserver);
}

void ACWPlayerState::OnRep_PlayerName()
{
	Super::OnRep_PlayerName();

	// 이름 복제가 끝난 시점의 최신 플레이어 목록을 UI에 알림
	NotifyPlayersChanged();
}

#pragma endregion

#pragma region ReadyState

bool ACWPlayerState::IsReady() const
{
	return bIsReady;
}

void ACWPlayerState::SetFilmObserver(bool bObserver)
{
	if (!HasAuthority()) { return; }
	bIsFilmObserver = bObserver;
	SetIsOnlyASpectator(bObserver);
	SetIsSpectator(bObserver);
	NotifyPlayersChanged();
	ForceNetUpdate();
}

void ACWPlayerState::ServerSetReady_Implementation(bool bNewReady)
{
	if (bIsFilmObserver) { return; }
	if (bIsReady == bNewReady)
	{
		return;
	}

	// 준비 상태는 서버에서만 확정하고 이후 클라이언트에 복제
	bIsReady = bNewReady;

	// Listen Server에서는 RepNotify가 자동 호출되지 않으므로 직접 알림
	NotifyPlayersChanged();

	// 준비 상태 변경을 가능한 빨리 클라이언트에 복제
	ForceNetUpdate();
}

void ACWPlayerState::OnRep_IsReady()
{
	// 복제된 준비 상태를 사용하는 플레이어 목록 UI가 목록을 다시 읽도록 알림
	NotifyPlayersChanged();
}

void ACWPlayerState::NotifyPlayersChanged() const
{
	UWorld* World = GetWorld();

	if (!IsValid(World))
	{
		return;
	}

	// 방 대기실에서는 준비 상태와 이름 변경을 RoomGameState에 전달
	ACWRoomGameState* RoomGameState =
		World->GetGameState<ACWRoomGameState>();

	if (IsValid(RoomGameState))
	{
		RoomGameState->NotifyRoomPlayersChanged();
		return;
	}

	// 인게임에서는 이름 변경을 인게임 플레이어 목록에 전달
	ACWGameState* GameState =
		World->GetGameState<ACWGameState>();

	if (IsValid(GameState))
	{
		GameState->NotifyPlayerListChanged();
	}
}

#pragma endregion
