// CWRoomViewModel.h

#pragma once

#include "CoreMinimal.h"
#include "UI/ViewModel/CWBaseGameStateViewModel.h"
#include "Data/Structs/CWRoomPlayerInfo.h"
#include "CWRoomViewModel.generated.h"

class ACWRoomGameState;
class AGameStateBase;

UCLASS(BlueprintType)
class CROSSWALK_API UCWRoomViewModel : public UCWBaseGameStateViewModel
{
	GENERATED_BODY()

#pragma region RoomRequests

public:
	// 로컬 플레이어의 준비 상태를 반대로 변경
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Room")
	void RequestToggleReady();

	// 호스트가 게임 시작 조건을 확인하고 인게임 이동 요청
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Room")
	bool RequestStartGame();

#pragma endregion

#pragma region RoomData

public:
	// 방 화면에 표시할 현재 플레이어 목록 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Room")
	const TArray<FCWRoomPlayerInfo>& GetRoomPlayers() const
	{
		return RoomPlayers;
	}

	// 현재 방에 참가한 플레이어 수 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Room")
	int32 GetCurrentPlayerCount() const
	{
		return CurrentPlayerCount;
	}

	// 현재 준비를 완료한 플레이어 수 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Room")
	int32 GetReadyPlayerCount() const
	{
		return ReadyPlayerCount;
	}

	// 게임 시작에 필요한 최소 플레이어 수 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Room")
	int32 GetMinimumPlayersToStart() const
	{
		return MinimumPlayersToStart;
	}

	// 현재 모든 게임 시작 조건을 만족하는지 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Room")
	bool GetCanStartGame() const
	{
		return bCanStartGame;
	}

	// 현재 로컬 플레이어가 Listen Server 호스트인지 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Room")
	bool GetIsHost() const
	{
		return bIsHost;
	}

	// 현재 로컬 플레이어의 준비 상태 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Room")
	bool GetIsLocalPlayerReady() const
	{
		return bIsLocalPlayerReady;
	}

	// 준비 완료 인원 표시 문구 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Room")
	FText GetReadyCountText() const
	{
		return ReadyCountText;
	}

	// 최소 인원 미달 상태 표시 문구 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Room")
	FText GetStartConditionText() const
	{
		return StartConditionText;
	}

	// 최소 시작 인원 미달 문구를 표시해야 하는지 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Room")
	bool GetShouldShowStartCondition() const
	{
		return bShouldShowStartCondition;
	}

private:
	// 현재 GameState의 플레이어 상태를 MVVM 프로퍼티로 변환
	void RefreshRoomState();

#pragma endregion

#pragma region GameStateBinding

protected:
	// 방 전용 GameState일 때만 방 데이터를 구독
	virtual void BindGameState(AGameStateBase* InGameState) override;

	// 현재 구독 중인 RoomGameState에서 구독 해제
	virtual void UnbindGameState() override;

	// 표시 상태를 기본값으로 되돌림
	virtual void ResetViewModelState() override;

private:
	// 방 플레이어 목록 또는 준비 상태 변경 처리
	UFUNCTION()
	void HandleRoomPlayersChanged();

#pragma endregion

#pragma region ViewModelSetters

private:
	// 플레이어 표시 목록 저장 후 FieldNotify 발생
	void SetRoomPlayers(
		const TArray<FCWRoomPlayerInfo>& InRoomPlayers
	);

	// 현재 방 플레이어 수 저장 후 FieldNotify 발생
	void SetCurrentPlayerCount(int32 InPlayerCount);

	// 준비 완료 플레이어 수 저장 후 FieldNotify 발생
	void SetReadyPlayerCount(int32 InReadyCount);

	// 최소 시작 인원 저장 후 FieldNotify 발생
	void SetMinimumPlayersToStart(int32 InMinimumPlayers);

	// 게임 시작 가능 상태 저장 후 FieldNotify 발생
	void SetCanStartGame(bool bInCanStartGame);

	// 로컬 호스트 여부 저장 후 FieldNotify 발생
	void SetIsHost(bool bInIsHost);

	// 로컬 플레이어 준비 상태 저장 후 FieldNotify 발생
	void SetIsLocalPlayerReady(bool bInIsReady);

	// 준비 완료 인원 문구 저장 후 FieldNotify 발생
	void SetReadyCountText(const FText& InText);

	// 최소 시작 인원 안내 문구 저장 후 FieldNotify 발생
	void SetStartConditionText(const FText& InText);

	// 최소 인원 안내 문구 표시 여부 저장 후 FieldNotify 발생
	void SetShouldShowStartCondition(bool bInShouldShow);

#pragma endregion

#pragma region ViewModelState

private:
	// 방 화면에 표시할 플레이어 목록
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Room",
		meta = (AllowPrivateAccess = "true")
	)
	TArray<FCWRoomPlayerInfo> RoomPlayers;

	// 현재 방에 참가한 플레이어 수
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Room",
		meta = (AllowPrivateAccess = "true")
	)
	int32 CurrentPlayerCount = 0;

	// 현재 준비를 완료한 플레이어 수
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Room",
		meta = (AllowPrivateAccess = "true")
	)
	int32 ReadyPlayerCount = 0;

	// 게임 시작에 필요한 최소 플레이어 수
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Room",
		meta = (AllowPrivateAccess = "true")
	)
	int32 MinimumPlayersToStart = 1;

	// 최소 인원과 전원 준비 조건 만족 여부
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Room",
		meta = (AllowPrivateAccess = "true")
	)
	bool bCanStartGame = false;

	// 현재 로컬 플레이어가 Listen Server 호스트인지 나타냄
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Room",
		meta = (AllowPrivateAccess = "true")
	)
	bool bIsHost = false;

	// 현재 로컬 플레이어의 준비 상태
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Room",
		meta = (AllowPrivateAccess = "true")
	)
	bool bIsLocalPlayerReady = false;

	// 준비 완료 인원 표시 문구
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Room",
		meta = (AllowPrivateAccess = "true")
	)
	FText ReadyCountText;

	// 최소 시작 인원 미달 안내 문구
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Room",
		meta = (AllowPrivateAccess = "true")
	)
	FText StartConditionText;

	// 최소 시작 인원 미달 안내 표시 여부
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Room",
		meta = (AllowPrivateAccess = "true")
	)
	bool bShouldShowStartCondition = false;

	// 현재 구독 중인 RoomGameState
	UPROPERTY()
	TWeakObjectPtr<ACWRoomGameState> BoundGameState;

#pragma endregion
};
