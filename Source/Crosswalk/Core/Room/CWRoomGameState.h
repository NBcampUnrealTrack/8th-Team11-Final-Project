// CWRoomGameState.h

#pragma once

#include "CoreMinimal.h"
#include "Core/Stage/CWStageSelectionGameStateBase.h"
#include "CWRoomGameState.generated.h"

// 방 플레이어 목록 또는 표시 정보가 변경됐음을 알리는 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE(
	FCWOnRoomPlayersChangedSignature
);

UCLASS()
class CROSSWALK_API ACWRoomGameState : public ACWStageSelectionGameStateBase
{
	GENERATED_BODY()

#pragma region GameStateLifecycle

public:
	// 방 설정값을 클라이언트에도 복제하도록 등록
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps
	) const override;

	// PlayerState가 방 목록에 추가된 후 변경 이벤트를 발행
	virtual void AddPlayerState(
		APlayerState* PlayerState
	) override;

	// PlayerState가 방 목록에서 제거된 후 변경 이벤트를 발행
	virtual void RemovePlayerState(
		APlayerState* PlayerState
	) override;

#pragma endregion

#pragma region RoomPlayers

public:
	// 최소 인원과 전원 준비 조건을 만족하는지 확인
	UFUNCTION(BlueprintPure, Category = "CW|Room")
	bool CanStartGame() const;

	// 플레이어 정보가 바뀌었을 때 목록 갱신 이벤트를 발행
	UFUNCTION(BlueprintCallable, Category = "CW|Room")
	void NotifyRoomPlayersChanged();

#pragma endregion

#pragma region RoomConfiguration

public:
	// 게임 시작에 필요한 최소 인원을 반환
	UFUNCTION(BlueprintPure, Category = "CW|Room")
	int32 GetMinimumPlayersToStart() const
	{
		return MinimumPlayersToStart;
	}

	// GameMode가 게임 시작에 필요한 최소 인원을 주입
	void SetMinimumPlayersToStart(
		int32 InMinimumPlayers
	);

private:
	// 최소 시작 인원이 클라이언트에 복제되면 UI 갱신 이벤트를 발행
	UFUNCTION()
	void OnRep_MinimumPlayersToStart();

	// 게임 시작에 필요한 최소 방 인원
	UPROPERTY(
		ReplicatedUsing = OnRep_MinimumPlayersToStart
	)
	int32 MinimumPlayersToStart = 1;

#pragma endregion

#pragma region RoomEvents

public:
	// 입장·퇴장·이름·준비 상태 변경을 UI 계층에 알림
	UPROPERTY(BlueprintAssignable, Category = "CW|Room")
	FCWOnRoomPlayersChangedSignature OnRoomPlayersChanged;

#pragma endregion
};
