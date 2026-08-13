// CWInGamePlayerViewModel.h

#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "Data/Structs/CWRoomPlayerInfo.h"
#include "CWInGamePlayerViewModel.generated.h"

class ACWGameState;
class AGameStateBase;
class UWorld;

UCLASS(BlueprintType)
class CROSSWALK_API UCWInGamePlayerViewModel
	: public UMVVMViewModelBase
{
	GENERATED_BODY()

#pragma region ViewModelLifecycle

public:
	// 현재 월드의 인게임 GameState를 찾아 플레이어 목록 구독 시작
	UFUNCTION(BlueprintCallable, Category = "CW|UI|InGamePlayer")
	void Initialize(UObject* WorldContextObject);

	// GameState와 World 이벤트 구독 정리
	UFUNCTION(BlueprintCallable, Category = "CW|UI|InGamePlayer")
	void Uninitialize();

#pragma endregion

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

#pragma endregion

#pragma region GameStateBinding

private:
	// 현재 월드에 생성된 인게임 GameState 구독
	void BindGameState(ACWGameState* InGameState);

	// 현재 구독 중인 인게임 GameState에서 구독 해제
	void UnbindGameState();

	// 플레이어 입장·퇴장·이름 변경 처리
	UFUNCTION()
	void HandlePlayerListChanged();

	// 인게임 GameState가 늦게 생성된 경우 다시 구독
	void HandleGameStateSet(AGameStateBase* InGameState);

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

	// 초기화에 사용된 현재 플레이 월드
	UPROPERTY()
	TWeakObjectPtr<UWorld> BoundWorld;

	// Initialize에 전달된 위젯
	// 해당 위젯의 Owning Player를 통해 로컬 플레이어를 식별
	UPROPERTY()
	TWeakObjectPtr<UObject> BoundContextObject;

	// GameState 생성 이벤트 구독 해제용 핸들
	FDelegateHandle GameStateSetHandle;

#pragma endregion
};
