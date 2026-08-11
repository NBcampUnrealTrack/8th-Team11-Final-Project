// CWInGamePlayerViewModel.cpp

#include "UI/ViewModel/CWInGamePlayerViewModel.h"

#include "CoreMinimal.h"
#include "Core/CWGameState.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

#pragma region ViewModelLifecycle

void UCWInGamePlayerViewModel::Initialize(
	UObject* WorldContextObject
)
{
	// 재초기화 시 이전 월드와 GameState 구독을 먼저 정리
	Uninitialize();

	if (!IsValid(WorldContextObject))
	{
		return;
	}

	UWorld* World = WorldContextObject->GetWorld();

	if (!IsValid(World))
	{
		return;
	}

	BoundWorld = World;

	// 인게임 GameState가 ViewModel보다 늦게 생성되는 상황 처리
	GameStateSetHandle = World->GameStateSetEvent.AddUObject(
		this,
		&UCWInGamePlayerViewModel::HandleGameStateSet
	);

	// GameState가 이미 생성돼 있으면 즉시 구독하고 현재 목록 반영
	BindGameState(
		World->GetGameState<ACWGameState>()
	);
}

void UCWInGamePlayerViewModel::Uninitialize()
{
	// 현재 GameState의 플레이어 목록 이벤트 구독 해제
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
	BoundWorld.Reset();

	// 이전 맵의 플레이어 데이터가 다음 화면에 남지 않도록 초기화
	SetLocalPlayerName(FText::GetEmpty());
	SetRemotePlayers(TArray<FCWRoomPlayerInfo>());
}

#pragma endregion

#pragma region PlayerData

void UCWInGamePlayerViewModel::RefreshPlayers()
{
	ACWGameState* GameState =
		BoundGameState.Get();

	UWorld* World =
		BoundWorld.Get();

	if (!IsValid(GameState) || !IsValid(World))
	{
		return;
	}

	APlayerController* LocalPlayerController =
		World->GetFirstPlayerController();

	APlayerState* LocalPlayerState = nullptr;

	if (IsValid(LocalPlayerController))
	{
		LocalPlayerState =
			LocalPlayerController->PlayerState;
	}

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

		++ValidPlayerIndex;

		const FString PlayerName =
			PlayerState->GetPlayerName();

		FText DisplayName;

		// 이름 복제가 아직 끝나지 않았으면 임시 이름 표시
		if (PlayerName.IsEmpty())
		{
			DisplayName = FText::Format(
				NSLOCTEXT(
					"CWInGamePlayerUI",
					"DefaultPlayerName",
					"Player {0}"
				),
				FText::AsNumber(ValidPlayerIndex)
			);
		}
		else
		{
			DisplayName =
				FText::FromString(PlayerName);
		}

		// 현재 화면을 소유한 플레이어는 원격 목록에서 제외
		if (PlayerState == LocalPlayerState)
		{
			NewLocalPlayerName = DisplayName;
			continue;
		}

		FCWRoomPlayerInfo PlayerInfo;

		// 서버가 접속 순서대로 부여한 번호를 화면 정렬에 사용
		PlayerInfo.PlayerId =
			PlayerState->GetPlayerId();

		PlayerInfo.PlayerName =
			DisplayName;

		// 인게임 목록에서는 방 준비 상태를 사용하지 않음
		PlayerInfo.bIsReady = false;
		PlayerInfo.bIsLocalPlayer = false;

		NewRemotePlayers.Add(
			MoveTemp(PlayerInfo)
		);
	}

	// PlayerArray 복제 도착 순서는 화면마다 다르므로 PlayerId로 정렬
	NewRemotePlayers.Sort(
		[](const FCWRoomPlayerInfo& Left,
			const FCWRoomPlayerInfo& Right)
		{
			return Left.PlayerId < Right.PlayerId;
		}
	);

	SetLocalPlayerName(NewLocalPlayerName);
	SetRemotePlayers(NewRemotePlayers);
}

#pragma endregion

#pragma region GameStateBinding

void UCWInGamePlayerViewModel::BindGameState(
	ACWGameState* InGameState
)
{
	if (!IsValid(InGameState))
	{
		return;
	}

	// 같은 GameState에 중복 바인딩하지 않음
	if (BoundGameState.Get() == InGameState)
	{
		return;
	}

	UnbindGameState();

	BoundGameState = InGameState;

	// 플레이어 입장·퇴장·이름 변경 이벤트 구독
	InGameState->OnPlayerListChanged.AddDynamic(
		this,
		&UCWInGamePlayerViewModel::HandlePlayerListChanged
	);

	// 이벤트가 ViewModel 생성 전에 발생했을 수 있으므로 즉시 한 번 읽음
	RefreshPlayers();
}

void UCWInGamePlayerViewModel::UnbindGameState()
{
	if (
		ACWGameState* GameState =
		BoundGameState.Get()
		)
	{
		GameState->OnPlayerListChanged.RemoveDynamic(
			this,
			&UCWInGamePlayerViewModel::HandlePlayerListChanged
		);
	}

	BoundGameState.Reset();
}

void UCWInGamePlayerViewModel::HandlePlayerListChanged()
{
	// GameState가 가진 최신 PlayerArray를 다시 분리하고 정렬
	RefreshPlayers();
}

void UCWInGamePlayerViewModel::HandleGameStateSet(
	AGameStateBase* InGameState
)
{
	// 인게임 전용 GameState가 만들어진 경우에만 구독
	BindGameState(
		Cast<ACWGameState>(InGameState)
	);
}

#pragma endregion

#pragma region ViewModelSetters

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
