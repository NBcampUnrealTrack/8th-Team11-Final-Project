// CWRoomViewModel.cpp

#include "UI/ViewModel/CWRoomViewModel.h"
#include "CoreMinimal.h"
#include "Core/CWPlayerState.h"
#include "Core/Room/CWRoomGameMode.h"
#include "Core/Room/CWRoomGameState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"

#pragma region RoomRequests

void UCWRoomViewModel::RequestToggleReady()
{
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

bool UCWRoomViewModel::RequestOpenStageSelection()
{
	UWorld* World = GetBoundWorld();

	if (!IsValid(World))
	{
		return false;
	}

	// RoomGameMode 는 서버에만 존재 → 호스트에서만 성공
	ACWRoomGameMode* RoomGameMode =
		World->GetAuthGameMode<ACWRoomGameMode>();

	if (!IsValid(RoomGameMode))
	{
		return false;
	}

	// 전원에게 스테이지 선택창을 여는 요청
	return RoomGameMode->BeginStageSelection();
}

bool UCWRoomViewModel::RequestStartSelectedStage(FName StageId)
{
	UWorld* World = GetBoundWorld();

	if (!IsValid(World))
	{
		return false;
	}

	// RoomGameMode 는 서버에만 존재 → 호스트에서만 성공
	ACWRoomGameMode* RoomGameMode =
		World->GetAuthGameMode<ACWRoomGameMode>();

	if (!IsValid(RoomGameMode))
	{
		return false;
	}

	// 호스트가 선택한 스테이지로 서버에 최종 시작 요청
	return RoomGameMode->TryStartGame(StageId);
}

bool UCWRoomViewModel::RequestSelectStage(FName StageId)
{
	UWorld* World = GetBoundWorld();

	if (!IsValid(World))
	{
		return false;
	}

	ACWRoomGameMode* RoomGameMode =
		World->GetAuthGameMode<ACWRoomGameMode>();

	if (!IsValid(RoomGameMode))
	{
		return false;
	}

	// 서버에 현재 선택 변경 요청 (클라는 GameMode 없어 무효 = 호스트 전용)
	return RoomGameMode->SelectStage(StageId);
}

bool UCWRoomViewModel::RequestCloseStageSelection()
{
	UWorld* World = GetBoundWorld();

	if (!IsValid(World))
	{
		return false;
	}

	ACWRoomGameMode* RoomGameMode =
		World->GetAuthGameMode<ACWRoomGameMode>();

	if (!IsValid(RoomGameMode))
	{
		return false;
	}

	// 서버에 선택창 닫기 요청
	return RoomGameMode->CancelStageSelection();
}

#pragma endregion

#pragma region RoomData

void UCWRoomViewModel::RefreshRoomState()
{
	ACWRoomGameState* RoomGameState =
		Cast<ACWRoomGameState>(GetBoundGameState());

	if (!IsValid(RoomGameState))
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

		// 이름 복제 전이면 목록 내 유효 순번으로 임시 이름 생성 (공용 헬퍼)
		PlayerInfo.PlayerName = MakePlayerDisplayName(
			PlayerState,
			NewRoomPlayers.Num() + 1
		);

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

	// PlayerArray 복제 순서는 화면마다 다르므로 서버가 부여한 번호로 정렬 (공용 헬퍼)
	SortPlayerInfosByPlayerId(NewRoomPlayers);

	const int32 NewCurrentPlayerCount =
		NewRoomPlayers.Num();

	const int32 NewMinimumPlayers =
		RoomGameState->GetMinimumPlayersToStart();

	const bool bNewCanStartGame =
		RoomGameState->CanStartGame();

	// Listen Server의 로컬 컨트롤러만 호스트로 판정 (베이스 공용 헬퍼)
	const bool bNewIsHost = IsLocalHost();

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

UClass* UCWRoomViewModel::GetGameStateClass() const
{
	return ACWRoomGameState::StaticClass();
}

void UCWRoomViewModel::SubscribeToGameState(
	AGameStateBase* InGameState
)
{
	ACWRoomGameState* RoomGameState =
		Cast<ACWRoomGameState>(InGameState);

	if (!IsValid(RoomGameState))
	{
		return;
	}

	// 플레이어 입장·퇴장·이름·준비 상태 변경 알림 구독
	RoomGameState->OnRoomPlayersChanged.AddDynamic(
		this,
		&UCWRoomViewModel::HandleRoomPlayersChanged
	);

	// 스테이지 선택창 열림·확정 상태 변경 알림 구독
	RoomGameState->OnStageSelectionStateChanged.AddUObject(
		this,
		&UCWRoomViewModel::HandleStageSelectionStateChanged
	);
}

void UCWRoomViewModel::UnsubscribeFromGameState(
	AGameStateBase* InGameState
)
{
	// 바인딩 시 등록한 방 플레이어 변경 delegate 를 해제
	ACWRoomGameState* RoomGameState =
		Cast<ACWRoomGameState>(InGameState);

	if (!IsValid(RoomGameState))
	{
		return;
	}

	RoomGameState->OnRoomPlayersChanged.RemoveDynamic(
		this,
		&UCWRoomViewModel::HandleRoomPlayersChanged
	);

	RoomGameState->OnStageSelectionStateChanged.RemoveAll(this);
}

void UCWRoomViewModel::RefreshFromGameState()
{
	// OnRep 또는 AddPlayerState 알림이 지난 경우를 위해 즉시 한 번 읽음
	RefreshRoomState();

	// 바인딩·재접속 시점의 선택창 상태도 즉시 반영 (OnRep 이 구독 전 지난 경우 방어)
	RefreshStageSelectionState();
}

void UCWRoomViewModel::ResetViewModelState()
{
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

	// 선택창 상태도 초기화 (이전 방 상태가 다음 화면에 남지 않도록)
	SetIsStageSelectionOpen(false);
	SetSelectedStageId(NAME_None);
}

void UCWRoomViewModel::HandleRoomPlayersChanged()
{
	// 준비 상태나 플레이어 목록이 바뀌면 화면 데이터를 다시 계산
	RefreshRoomState();
}

void UCWRoomViewModel::HandleStageSelectionStateChanged()
{
	// 열림·확정 중 무엇이 바뀌든 두 값을 통째로 다시 읽음 (알림 2회에도 일관)
	RefreshStageSelectionState();
}

void UCWRoomViewModel::RefreshStageSelectionState()
{
	const ACWRoomGameState* RoomGameState =
		Cast<ACWRoomGameState>(GetBoundGameState());

	if (IsValid(RoomGameState))
	{
		// 두 값을 항상 함께 재조회
		SetIsStageSelectionOpen(RoomGameState->IsStageSelectionOpen());
		SetSelectedStageId(RoomGameState->GetSelectedStageId());
	}
	else
	{
		// GameState 없으면 닫힘·선택없음으로
		SetIsStageSelectionOpen(false);
		SetSelectedStageId(NAME_None);
	}

	// 두 값 반영 후, 위젯이 팝업 생성·닫기를 처리하도록 한 번 알림
	OnStageSelectionChanged.Broadcast();
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

void UCWRoomViewModel::SetIsStageSelectionOpen(bool bInOpen)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		bIsStageSelectionOpen,
		bInOpen
	);
}

void UCWRoomViewModel::SetSelectedStageId(FName InStageId)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		SelectedStageId,
		InStageId
	);
}

#pragma endregion
