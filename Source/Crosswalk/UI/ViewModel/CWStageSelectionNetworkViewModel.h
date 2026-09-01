// CWStageSelectionNetworkViewModel.h

#pragma once

#include "CoreMinimal.h"
#include "UI/ViewModel/CWBaseGameStateViewModel.h"
#include "CWStageSelectionNetworkViewModel.generated.h"

class AGameStateBase;

// 스테이지 선택 상태 변경을 위젯에 알림
// 팝업 생성·닫기와 복제 선택 반영에 사용함
DECLARE_DYNAMIC_MULTICAST_DELEGATE(
	FCWOnStageSelectionNetworkChangedSignature
);

UCLASS(BlueprintType)
class CROSSWALK_API UCWStageSelectionNetworkViewModel
	: public UCWBaseGameStateViewModel
{
	GENERATED_BODY()

#pragma region StageSelectionRequests

public:
	// 서버에 전원의 스테이지 선택창 열기를 요청함
	UFUNCTION(
		BlueprintCallable,
		Category = "CW|UI|StageSelection"
	)
	bool RequestOpenStageSelection();

	// 서버에 호스트의 현재 선택 변경을 요청함
	UFUNCTION(
		BlueprintCallable,
		Category = "CW|UI|StageSelection"
	)
	bool RequestSelectStage(FName StageId);

	// 서버에 선택창 닫기를 요청함
	UFUNCTION(
		BlueprintCallable,
		Category = "CW|UI|StageSelection"
	)
	bool RequestCloseStageSelection();

	// 서버에 현재 선택 스테이지 시작을 요청함
	UFUNCTION(
		BlueprintCallable,
		Category = "CW|UI|StageSelection"
	)
	bool RequestStartSelectedStage(FName StageId);

#pragma endregion

#pragma region StageSelectionData

public:
	// 전원에게 선택창을 표시해야 하는지 반환함
	UFUNCTION(
		BlueprintPure,
		Category = "CW|UI|StageSelection"
	)
	bool GetIsStageSelectionOpen() const
	{
		return bIsStageSelectionOpen;
	}

	// 호스트가 현재 선택한 StageId를 반환함
	// 선택이 없으면 NAME_None임
	UFUNCTION(
		BlueprintPure,
		Category = "CW|UI|StageSelection"
	)
	FName GetSelectedStageId() const
	{
		return SelectedStageId;
	}

	// 현재 로컬 플레이어가 Listen Server 호스트인지 반환함
	UFUNCTION(
		BlueprintPure,
		Category = "CW|UI|StageSelection"
	)
	bool GetIsHost() const
	{
		return bIsHost;
	}

private:
	// 현재 GameState의 선택 상태를 세 값 함께 반영함
	void RefreshStageSelectionState();

#pragma endregion

#pragma region GameStateBinding

protected:
	// RoomGameState와 인게임 GameState의 공용 부모를 구독 대상으로 지정함
	virtual UClass* GetGameStateClass() const override;

	// 공용 GameState의 선택 상태 변경 이벤트를 구독함
	virtual void SubscribeToGameState(
		AGameStateBase* InGameState
	) override;

	// 공용 GameState의 선택 상태 변경 이벤트 구독을 해제함
	virtual void UnsubscribeFromGameState(
		AGameStateBase* InGameState
	) override;

	// 바인딩 직후 현재 선택 상태를 즉시 반영함
	virtual void RefreshFromGameState() override;

	// 이전 월드의 선택 상태를 기본값으로 초기화함
	virtual void ResetViewModelState() override;

private:
	UFUNCTION()
	void HandleStageSelectionStateChanged();

#pragma endregion

#pragma region ViewModelSetters

private:
	void SetIsStageSelectionOpen(bool bInOpen);
	void SetSelectedStageId(FName InStageId);
	void SetIsHost(bool bInIsHost);

#pragma endregion

#pragma region ViewModelState

private:
	// 복제된 선택창 열림 상태임
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|StageSelection",
		meta = (AllowPrivateAccess = "true")
	)
	bool bIsStageSelectionOpen = false;

	// 호스트가 현재 선택한 복제 StageId임
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|StageSelection",
		meta = (AllowPrivateAccess = "true")
	)
	FName SelectedStageId = NAME_None;

	// 현재 로컬 플레이어의 Listen Server 호스트 여부임
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|StageSelection",
		meta = (AllowPrivateAccess = "true")
	)
	bool bIsHost = false;

#pragma endregion

#pragma region ViewModelEvents

public:
	// 열림 상태·현재 선택·호스트 여부 반영이 끝난 뒤 발생함
	UPROPERTY(
		BlueprintAssignable,
		Category = "CW|UI|StageSelection"
	)
	FCWOnStageSelectionNetworkChangedSignature
		OnStageSelectionChanged;

#pragma endregion
};
