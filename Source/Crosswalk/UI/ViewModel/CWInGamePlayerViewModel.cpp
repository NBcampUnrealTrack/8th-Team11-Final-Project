// CWInGamePlayerViewModel.cpp

#include "UI/ViewModel/CWInGamePlayerViewModel.h"
#include "CoreMinimal.h"
#include "Core/CWGameState.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

#pragma region PlayerData

void UCWInGamePlayerViewModel::RefreshPlayers()
{
	// 이번 실행이 시작됐으니 기존 예약 정리 (필요 시 끝에서 다시 예약)
	ClearRefreshPlayersRetry();

	ACWGameState* GameState =
		Cast<ACWGameState>(GetBoundGameState());

	UWorld* World = GetBoundWorld();

	if (!IsValid(GameState) || !IsValid(World))
	{
		return;
	}

	// 위젯의 Owning Player로 로컬 플레이어를 식별한다.
	// Listen Server에는 여러 PlayerController가 있어
	// 월드에서 찾으면 원격 컨트롤러를 고를 수 있다
	APlayerController* LocalPlayerController =
		GetOwningPlayerController();

	// Owning Player 연결이 아직 끝나지 않았다면 다음 Tick에 재시도
	if (!IsValid(LocalPlayerController))
	{
		ScheduleRefreshPlayersRetry();

		return;
	}

	// Listen Server의 로컬 컨트롤러만 호스트로 판정 (베이스 공용 헬퍼)
	SetIsHost(IsLocalHost());

	APlayerState* LocalPlayerState =
		LocalPlayerController->PlayerState;

	// PC의 PlayerState 복제가 아직 도착하지 않았다면
	// 잘못된 원격 목록을 확정하지 않고 다음 Tick에 재시도
	if (!IsValid(LocalPlayerState))
	{
		ScheduleRefreshPlayersRetry();

		return;
	}

	// Seamless Travel 중 PlayerState 객체가 교체돼도
	// 동일한 로컬 플레이어를 식별할 수 있도록 PlayerId를 함께 저장
	const int32 LocalPlayerId =
		LocalPlayerState->GetPlayerId();

	FText NewLocalPlayerName =
		FText::GetEmpty();

	TArray<FCWRoomPlayerInfo> NewRemotePlayers;

	NewRemotePlayers.Reserve(
		FMath::Max(
			0,
			GameState->PlayerArray.Num() - 1
		)
	);

	int32 ValidPlayerIndex = 0;

	// PlayerId 복제를 기다리는 플레이어가 있었는지 기록
	bool bHasPendingPlayerId = false;

	// 복제된 PlayerArray를 로컬 플레이어와 다른 플레이어로 분리
	for (
		const TObjectPtr<APlayerState>& PlayerStateObject :
		GameState->PlayerArray
		)
	{
		APlayerState* PlayerState =
			PlayerStateObject.Get();

		if (!IsValid(PlayerState))
		{
			continue;
		}

		const int32 CandidatePlayerId =
			PlayerState->GetPlayerId();

		// Seamless Travel 중 아직 초기화되지 않은 임시 PlayerState는 표시하지 않음
		// PlayerId는 RepNotify가 없어 뒤늦게 도착해도 갱신 이벤트가 오지 않음
		// 이번 프레임에는 건너뛰고 다음 Tick에 다시 확인
		if (CandidatePlayerId <= 0)
		{
			bHasPendingPlayerId = true;
			continue;
		}

		++ValidPlayerIndex;

		// 이름 복제 전이면 목록 내 유효 순번으로 임시 이름 생성 (공용 헬퍼)
		const FText DisplayName = MakePlayerDisplayName(
			PlayerState,
			ValidPlayerIndex
		);

		const APlayerController* PlayerController =
			PlayerState->GetPlayerController();

		// PlayerState가 가진 컨트롤러와 위젯의 Owning Player를 직접 비교해서
		// 현재 화면을 소유한 로컬 플레이어인지 보조 판정
		const bool bOwnedByLocalController =
			IsValid(PlayerController) &&
			PlayerController == LocalPlayerController;

		// Seamless Travel 중 포인터 교체와 연결 순서 차이를 모두 처리
		const bool bIsLocalPlayer =
			bOwnedByLocalController ||
			PlayerState == LocalPlayerState ||
			CandidatePlayerId == LocalPlayerId;

		// 현재 화면을 소유한 플레이어는 원격 목록에서 제외
		if (bIsLocalPlayer)
		{
			NewLocalPlayerName = DisplayName;
			continue;
		}

		FCWRoomPlayerInfo PlayerInfo;

		// 서버가 접속 순서대로 부여한 번호를 화면 정렬에 사용
		PlayerInfo.PlayerId =
			CandidatePlayerId;

		// Entry 위젯이 이 플레이어의 폰에서 체온을 직접 읽도록 참조 전달
		PlayerInfo.PlayerState = PlayerState;

		PlayerInfo.PlayerName =
			DisplayName;

		// 인게임 목록에서는 방 준비 상태를 사용하지 않음
		PlayerInfo.bIsReady = false;
		PlayerInfo.bIsLocalPlayer = false;

		NewRemotePlayers.Add(
			MoveTemp(PlayerInfo)
		);
	}

	// PlayerArray 복제 도착 순서는 화면마다 다르므로 PlayerId로 정렬 (공용 헬퍼)
	SortPlayerInfosByPlayerId(NewRemotePlayers);

	SetLocalPlayerName(NewLocalPlayerName);
	SetRemotePlayers(NewRemotePlayers);

	// 목록을 먼저 반영한 뒤, 빠진 플레이어가 있으면 다음 Tick에 다시 채움
	if (bHasPendingPlayerId)
	{
		ScheduleRefreshPlayersRetry();
	}
}

void UCWInGamePlayerViewModel::ScheduleRefreshPlayersRetry()
{
	// 이미 예약된 핸들이 유효하면 중복 예약하지 않음
	if (RefreshPlayersRetryHandle.IsValid())
	{
		return;
	}

	UWorld* World = GetBoundWorld();

	if (!IsValid(World))
	{
		return;
	}

	RefreshPlayersRetryHandle =
		World->GetTimerManager().SetTimerForNextTick(
			this,
			&UCWInGamePlayerViewModel::RefreshPlayers
		);
}

void UCWInGamePlayerViewModel::ClearRefreshPlayersRetry()
{
	// 남은 예약을 실제로 취소 (재-Initialize 시 이전 콜백 발동 방지)
	if (UWorld* World = GetBoundWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshPlayersRetryHandle);
	}

	RefreshPlayersRetryHandle.Invalidate();
}

#pragma endregion

#pragma region GameStateBinding

UClass* UCWInGamePlayerViewModel::GetGameStateClass() const
{
	return ACWGameState::StaticClass();
}

void UCWInGamePlayerViewModel::SubscribeToGameState(
	AGameStateBase* InGameState
)
{
	ACWGameState* GameState =
		Cast<ACWGameState>(InGameState);

	if (!IsValid(GameState))
	{
		return;
	}

	// 플레이어 입장·퇴장·이름 변경 이벤트 구독
	GameState->OnPlayerListChanged.AddDynamic(
		this,
		&UCWInGamePlayerViewModel::HandlePlayerListChanged
	);
}

void UCWInGamePlayerViewModel::UnsubscribeFromGameState(
	AGameStateBase* InGameState
)
{
	// GameState 유효성과 무관하게 재시도 예약부터 정리
	ClearRefreshPlayersRetry();

	ACWGameState* GameState =
		Cast<ACWGameState>(InGameState);

	if (!IsValid(GameState))
	{
		return;
	}

	GameState->OnPlayerListChanged.RemoveDynamic(
		this,
		&UCWInGamePlayerViewModel::HandlePlayerListChanged
	);
}

void UCWInGamePlayerViewModel::RefreshFromGameState()
{
	// 이벤트가 ViewModel 생성 전에 발생했을 수 있으므로 즉시 한 번 읽음
	RefreshPlayers();
}

void UCWInGamePlayerViewModel::ResetViewModelState()
{
	// 이전 맵의 플레이어 데이터가 다음 화면에 남지 않도록 초기화
	SetIsHost(false);
	SetLocalPlayerName(FText::GetEmpty());
	SetRemotePlayers(TArray<FCWRoomPlayerInfo>());
}

void UCWInGamePlayerViewModel::HandlePlayerListChanged()
{
	// GameState가 가진 최신 PlayerArray를 다시 분리하고 정렬
	RefreshPlayers();
}

#pragma endregion

#pragma region ViewModelSetters

void UCWInGamePlayerViewModel::SetIsHost(
	bool bInIsHost
)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		bIsHost,
		bInIsHost
	);
}

void UCWInGamePlayerViewModel::SetLocalPlayerName(
	const FText& InPlayerName
)
{
	// FText의 내부 히스토리가 아니라 실제 표시 문자열로 비교
	if (LocalPlayerName.EqualTo(InPlayerName))
	{
		return;
	}

	LocalPlayerName = InPlayerName;

	// 표시 이름이 실제로 달라진 경우에만 UI 갱신
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(
		LocalPlayerName
	);
}

void UCWInGamePlayerViewModel::SetRemotePlayers(
	const TArray<FCWRoomPlayerInfo>& InRemotePlayers
)
{
	// 구조체 비교 연산자로 실제 배열 변경 시에만 UI 갱신
	UE_MVVM_SET_PROPERTY_VALUE(
		RemotePlayers,
		InRemotePlayers
	);
}

#pragma endregion
