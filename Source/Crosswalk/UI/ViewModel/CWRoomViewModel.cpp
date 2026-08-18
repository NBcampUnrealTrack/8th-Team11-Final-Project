// CWRoomViewModel.cpp

#include "UI/ViewModel/CWRoomViewModel.h"

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/CWPlayerState.h"
#include "Core/Room/CWRoomGameMode.h"
#include "Core/Room/CWRoomGameState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"

#pragma region ViewModelLifecycle

void UCWRoomViewModel::Initialize(UObject* WorldContextObject)
{
	// 재초기화 시 기존 월드와 GameState 구독을 먼저 정리
	Uninitialize();

	if (!IsValid(WorldContextObject))
	{
		return;
	}

	// 전달받은 화면에서 현재 플레이 월드 확인
	UWorld* World = WorldContextObject->GetWorld();

	if (!IsValid(World))
	{
		return;
	}

	BoundWorld = World;
	BoundContextObject = WorldContextObject;

	// RoomGameState가 늦게 만들어지는 상황을 처리하기 위해 월드 이벤트 구독
	GameStateSetHandle = World->GameStateSetEvent.AddUObject(
		this,
		&UCWRoomViewModel::HandleGameStateSet
	);

	// 이미 RoomGameState가 존재하면 즉시 구독하고 현재 상태까지 읽음
	BindGameState(
		World->GetGameState<ACWRoomGameState>()
	);
}

void UCWRoomViewModel::Uninitialize()
{
	// 현재 RoomGameState의 플레이어 변경 이벤트 구독 해제
	UnbindGameState();

	// 월드의 GameState 생성 이벤트 구독 해제
	if (UWorld* World = BoundWorld.Get())
	{
		if (GameStateSetHandle.IsValid())
		{
			World->GameStateSetEvent.Remove(
				GameStateSetHandle
			);
		}
	}

	GameStateSetHandle.Reset();
	BoundContextObject.Reset();
	BoundWorld.Reset();

	// 이전 방 데이터가 다음 화면에 남지 않도록 상태 초기화
	SetRoomPlayers(TArray<FCWRoomPlayerInfo>());
	SetCurrentPlayerCount(0);
	SetReadyPlayerCount(0);
	SetMinimumPlayersToStart(1);
	SetCanStartGame(false);
	SetIsHost(false);
	SetIsLocalPlayerReady(false);
	SetReadyCountText(FText::GetEmpty());
	SetStartConditionText(FText::GetEmpty());
	SetShouldShowStartCondition(false);
}

#pragma endregion

#pragma region RoomRequests

void UCWRoomViewModel::RequestToggleReady()
{
	UWorld* World = BoundWorld.Get();

	if (!IsValid(World))
	{
		return;
	}

	// 현재 화면을 소유한 로컬 플레이어 컨트롤러 확인
	APlayerController* LocalPlayerController =
		GetOwningPlayerController();

	if (!IsValid(LocalPlayerController))
	{
		return;
	}

	ACWPlayerState* CWPlayerState =
		LocalPlayerController->GetPlayerState<ACWPlayerState>();

	if (!IsValid(CWPlayerState))
	{
		return;
	}

	// 준비 상태는 PlayerState의 Server RPC를 통해 서버에서 확정
	CWPlayerState->ServerSetReady(
		!CWPlayerState->IsReady()
	);
}

bool UCWRoomViewModel::RequestStartGame()
{
	UWorld* World = BoundWorld.Get();

	if (!IsValid(World))
	{
		return false;
	}

	// RoomGameMode는 서버에만 존재하므로 클라이언트에서는 null 반환
	ACWRoomGameMode* RoomGameMode =
		World->GetAuthGameMode<ACWRoomGameMode>();

	if (!IsValid(RoomGameMode))
	{
		return false;
	}

	// 최종 시작 조건과 중복 트래블 검사는 GameMode에서 수행
	return RoomGameMode->TryStartGame();
}

#pragma region RoomData

APlayerController* UCWRoomViewModel::GetOwningPlayerController() const
{
	// 위젯이 소유한 로컬 플레이어를 우선 사용
	// Seamless Travel 이후에는 월드의 첫 번째 컨트롤러 순서가 보장되지 않음
	if (
		const UUserWidget* OwningWidget =
		Cast<UUserWidget>(BoundContextObject.Get())
		)
	{
		if (
			APlayerController* OwningPlayer =
			OwningWidget->GetOwningPlayer()
			)
		{
			return OwningPlayer;
		}
	}

	// 위젯이 아닌 객체로 초기화된 경우를 위한 예비 경로
	if (const UWorld* World = BoundWorld.Get())
	{
		return World->GetFirstPlayerController();
	}

	return nullptr;
}

#pragma endregion

#pragma region RoomData

void UCWRoomViewModel::RefreshRoomState()
{
	ACWRoomGameState* RoomGameState =
		BoundGameState.Get();

	UWorld* World = BoundWorld.Get();

	if (!IsValid(RoomGameState) || !IsValid(World))
	{
		return;
	}

	APlayerController* LocalPlayerController =
		GetOwningPlayerController();

	APlayerState* LocalPlayerState = nullptr;

	if (IsValid(LocalPlayerController))
	{
		LocalPlayerState =
			LocalPlayerController->PlayerState;
	}

	TArray<FCWRoomPlayerInfo> NewRoomPlayers;
	NewRoomPlayers.Reserve(
		RoomGameState->PlayerArray.Num()
	);

	int32 NewReadyPlayerCount = 0;
	bool bNewLocalPlayerReady = false;

	// 복제된 PlayerArray를 UI 전용 표시 구조체 배열로 변환
	for (const TObjectPtr<APlayerState>& PlayerStateObject :
		RoomGameState->PlayerArray)
	{
		APlayerState* PlayerState =
			PlayerStateObject.Get();

		if (!IsValid(PlayerState))
		{
			continue;
		}

		ACWPlayerState* CWPlayerState =
			Cast<ACWPlayerState>(PlayerState);

		if (!IsValid(CWPlayerState))
		{
			continue;
		}

		FCWRoomPlayerInfo PlayerInfo;

		// 서버가 접속 순서대로 부여한 번호를 UI 표시 순서에 사용
		PlayerInfo.PlayerId =
			PlayerState->GetPlayerId();

		const FString PlayerName =
			PlayerState->GetPlayerName();

		// 이름 복제가 아직 끝나지 않았으면 임시 플레이어 이름 사용
		if (PlayerName.IsEmpty())
		{
			PlayerInfo.PlayerName = FText::Format(
				NSLOCTEXT(
					"CWRoomUI",
					"DefaultPlayerName",
					"Player {0}"
				),
				FText::AsNumber(
					NewRoomPlayers.Num() + 1
				)
			);
		}
		else
		{
			PlayerInfo.PlayerName =
				FText::FromString(PlayerName);
		}

		PlayerInfo.bIsReady =
			CWPlayerState->IsReady();

		PlayerInfo.bIsLocalPlayer =
			PlayerState == LocalPlayerState;

		if (PlayerInfo.bIsReady)
		{
			++NewReadyPlayerCount;
		}

		if (PlayerInfo.bIsLocalPlayer)
		{
			bNewLocalPlayerReady =
				PlayerInfo.bIsReady;
		}

		NewRoomPlayers.Add(
			MoveTemp(PlayerInfo)
		);
	}

	// PlayerArray 복제 순서는 화면마다 다르므로 서버가 부여한 번호로 정렬
	NewRoomPlayers.Sort(
		[](const FCWRoomPlayerInfo& Left,
			const FCWRoomPlayerInfo& Right)
		{
			return Left.PlayerId < Right.PlayerId;
		}
	);

	const int32 NewCurrentPlayerCount =
		NewRoomPlayers.Num();

	const int32 NewMinimumPlayers =
		RoomGameState->GetMinimumPlayersToStart();

	const bool bNewCanStartGame =
		RoomGameState->CanStartGame();

	// Listen Server의 로컬 컨트롤러만 호스트로 판정
	const bool bNewIsHost =
		IsValid(LocalPlayerController) &&
		LocalPlayerController->IsLocalController() &&
		LocalPlayerController->HasAuthority();

	const bool bNeedsMorePlayers =
		NewCurrentPlayerCount < NewMinimumPlayers;

	const FText NewReadyCountText = FText::Format(
		NSLOCTEXT(
			"CWRoomUI",
			"ReadyPlayerCount",
			"준비 완료 {0} / {1}"
		),
		FText::AsNumber(NewReadyPlayerCount),
		FText::AsNumber(NewCurrentPlayerCount)
	);

	FText NewStartConditionText =
		FText::GetEmpty();

	if (bNeedsMorePlayers)
	{
		NewStartConditionText = FText::Format(
			NSLOCTEXT(
				"CWRoomUI",
				"MinimumPlayersRequired",
				"게임 시작에 최소 {0}명이 필요합니다."
			),
			FText::AsNumber(NewMinimumPlayers)
		);
	}

	// 계산한 최신 방 상태를 MVVM 프로퍼티에 반영
	SetRoomPlayers(NewRoomPlayers);
	SetCurrentPlayerCount(NewCurrentPlayerCount);
	SetReadyPlayerCount(NewReadyPlayerCount);
	SetMinimumPlayersToStart(NewMinimumPlayers);
	SetCanStartGame(bNewCanStartGame);
	SetIsHost(bNewIsHost);
	SetIsLocalPlayerReady(bNewLocalPlayerReady);
	SetReadyCountText(NewReadyCountText);
	SetStartConditionText(NewStartConditionText);
	SetShouldShowStartCondition(bNeedsMorePlayers);
}

#pragma endregion

#pragma region GameStateBinding

void UCWRoomViewModel::BindGameState(
	ACWRoomGameState* InGameState
)
{
	if (!IsValid(InGameState))
	{
		return;
	}

	// 동일한 RoomGameState에 중복 바인딩하지 않음
	if (BoundGameState.Get() == InGameState)
	{
		return;
	}

	UnbindGameState();

	BoundGameState = InGameState;

	// 플레이어 입장·퇴장·이름·준비 상태 변경 알림 구독
	InGameState->OnRoomPlayersChanged.AddDynamic(
		this,
		&UCWRoomViewModel::HandleRoomPlayersChanged
	);

	// OnRep 또는 AddPlayerState 알림이 지난 경우를 위해 즉시 한 번 읽음
	RefreshRoomState();
}

void UCWRoomViewModel::UnbindGameState()
{
	if (ACWRoomGameState* RoomGameState =
		BoundGameState.Get())
	{
		RoomGameState->OnRoomPlayersChanged.RemoveDynamic(
			this,
			&UCWRoomViewModel::HandleRoomPlayersChanged
		);
	}

	BoundGameState.Reset();
}

void UCWRoomViewModel::HandleRoomPlayersChanged()
{
	// GameState가 발행한 최신 PlayerArray와 준비 상태를 다시 계산
	RefreshRoomState();
}

void UCWRoomViewModel::HandleGameStateSet(
	AGameStateBase* InGameState
)
{
	// 방 전용 GameState가 만들어진 경우에만 구독
	BindGameState(
		Cast<ACWRoomGameState>(InGameState)
	);
}

#pragma endregion

#pragma region ViewModelSetters

void UCWRoomViewModel::SetRoomPlayers(
	const TArray<FCWRoomPlayerInfo>& InRoomPlayers
)
{
	// 구조체 비교 연산자를 사용해 실제 배열 변경 시에만 알림
	UE_MVVM_SET_PROPERTY_VALUE(
		RoomPlayers,
		InRoomPlayers
	);
}

void UCWRoomViewModel::SetCurrentPlayerCount(
	int32 InPlayerCount
)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		CurrentPlayerCount,
		InPlayerCount
	);
}

void UCWRoomViewModel::SetReadyPlayerCount(
	int32 InReadyCount
)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		ReadyPlayerCount,
		InReadyCount
	);
}

void UCWRoomViewModel::SetMinimumPlayersToStart(
	int32 InMinimumPlayers
)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		MinimumPlayersToStart,
		InMinimumPlayers
	);
}

void UCWRoomViewModel::SetCanStartGame(
	bool bInCanStartGame
)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		bCanStartGame,
		bInCanStartGame
	);
}

void UCWRoomViewModel::SetIsHost(
	bool bInIsHost
)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		bIsHost,
		bInIsHost
	);
}

void UCWRoomViewModel::SetIsLocalPlayerReady(
	bool bInIsReady
)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		bIsLocalPlayerReady,
		bInIsReady
	);
}

void UCWRoomViewModel::SetReadyCountText(
	const FText& InText
)
{
	// FText는 내부 히스토리가 아니라 실제 표시 문자열로 비교
	if (ReadyCountText.EqualTo(InText))
	{
		return;
	}

	ReadyCountText = InText;

	// 표시 문자열이 실제로 달라졌을 때만 MVVM 갱신 알림 발생
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(
		ReadyCountText
	);
}

void UCWRoomViewModel::SetStartConditionText(
	const FText& InText
)
{
	// FText::Format이 만든 새 히스토리 때문에 중복 알림이 발생하지 않도록 표시 문자열 비교
	if (StartConditionText.EqualTo(InText))
	{
		return;
	}

	StartConditionText = InText;

	// 표시 문자열이 실제로 달라졌을 때만 MVVM 갱신 알림 발생
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(
		StartConditionText
	);
}

void UCWRoomViewModel::SetShouldShowStartCondition(
	bool bInShouldShow
)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		bShouldShowStartCondition,
		bInShouldShow
	);
}

#pragma endregion
