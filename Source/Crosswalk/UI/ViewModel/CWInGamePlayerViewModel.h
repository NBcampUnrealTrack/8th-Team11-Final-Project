// CWInGamePlayerViewModel.h

#pragma once

#include "CoreMinimal.h"
#include "UI/ViewModel/CWBaseGameStateViewModel.h"
#include "Data/Structs/CWRoomPlayerInfo.h"
#include "CWInGamePlayerViewModel.generated.h"

class ACWGameState;
class AGameStateBase;

UCLASS(BlueprintType)
class CROSSWALK_API UCWInGamePlayerViewModel
	: public UCWBaseGameStateViewModel
{
	GENERATED_BODY()

#pragma region PlayerData

public:
	// 현재 화면의 로컬 플레이어가 Listen Server 호스트인지 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|InGamePlayer")
	bool GetIsHost() const
	{
		return bIsHost;
	}

	// 현재 로컬 플레이어의 표시 이름 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|InGamePlayer")
	FText GetLocalPlayerName() const
	{
		return LocalPlayerName;
	}

	// 현재 로컬 플레이어를 제외한 다른 플레이어 목록 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|InGamePlayer")
	const TArray<FCWRoomPlayerInfo>& GetRemotePlayers() const
	{
		return RemotePlayers;
	}

private:
	// GameState의 PlayerArray를 로컬·원격 플레이어 데이터로 변환
	void RefreshPlayers();

	// 다음 Tick 재시도 예약 핸들 (중복 예약 방지·취소용)
	FTimerHandle RefreshPlayersRetryHandle;

	// RefreshPlayers 재시도를 다음 Tick에 한 번만 예약
	void ScheduleRefreshPlayersRetry();

	// 재시도 예약을 취소하고 핸들 무효화
	void ClearRefreshPlayersRetry();

#pragma endregion

#pragma region GameStateBinding

protected:
	// 인게임 전용 GameState일 때만 플레이어 목록을 구독
	virtual void BindGameState(AGameStateBase* InGameState) override;

	// 현재 구독 중인 인게임 GameState에서 구독 해제
	virtual void UnbindGameState() override;

	// 표시 상태를 기본값으로 되돌림
	virtual void ResetViewModelState() override;

private:
	// 플레이어 입장·퇴장·이름 변경 처리
	UFUNCTION()
	void HandlePlayerListChanged();

#pragma endregion

#pragma region ViewModelSetters

private:
	// 로컬 플레이어의 호스트 여부 저장 후 FieldNotify 발생
	void SetIsHost(bool bInIsHost);

	// 로컬 플레이어 표시 이름 저장 후 FieldNotify 발생
	void SetLocalPlayerName(const FText& InPlayerName);

	// 원격 플레이어 표시 목록 저장 후 FieldNotify 발생
	void SetRemotePlayers(
		const TArray<FCWRoomPlayerInfo>& InRemotePlayers
	);

#pragma endregion

#pragma region ViewModelState

private:
	// Result 화면에서 호스트 전용 버튼 표시 여부
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|InGamePlayer",
		meta = (AllowPrivateAccess = "true")
	)
	bool bIsHost = false;

	// 좌측 하단 본인 상태 영역에 표시할 로컬 플레이어 이름
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|InGamePlayer",
		meta = (AllowPrivateAccess = "true")
	)
	FText LocalPlayerName;

	// 좌측 중단에 표시할 본인을 제외한 플레이어 목록
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|InGamePlayer",
		meta = (AllowPrivateAccess = "true")
	)
	TArray<FCWRoomPlayerInfo> RemotePlayers;

	// 현재 구독 중인 인게임 GameState
	UPROPERTY()
	TWeakObjectPtr<ACWGameState> BoundGameState;

#pragma endregion
};
