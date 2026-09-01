// CWStageSelectionGameStateBase.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "CWStageSelectionGameStateBase.generated.h"

// 스테이지 선택창 열림 여부 또는 현재 선택 StageId 변경을 UI에 알림
DECLARE_MULTICAST_DELEGATE(FCWOnStageSelectionStateChangedSignature);

UCLASS()
class CROSSWALK_API ACWStageSelectionGameStateBase : public AGameStateBase
{
	GENERATED_BODY()

#pragma region GameStateLifecycle

public:
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps
	) const override;

#pragma endregion

#pragma region StageSelection

public:
	// 전원에게 스테이지 선택창을 표시해야 하는지
	UFUNCTION(BlueprintPure, Category = "CW|StageSelection")
	bool IsStageSelectionOpen() const
	{
		return bIsStageSelectionOpen;
	}

	// 호스트가 현재 선택한 스테이지. 선택이 없으면 NAME_None
	UFUNCTION(BlueprintPure, Category = "CW|StageSelection")
	FName GetSelectedStageId() const
	{
		return SelectedStageId;
	}

	// 서버가 선택창 열림 여부를 변경
	void SetStageSelectionOpen(bool bOpen);

	// 서버가 현재 선택 스테이지를 변경
	void SetSelectedStageId(FName InStageId);

private:
	UFUNCTION()
	void OnRep_IsStageSelectionOpen();

	UFUNCTION()
	void OnRep_SelectedStageId();

	// 서버 Setter와 클라이언트 OnRep의 공통 알림
	void NotifyStageSelectionStateChanged();

	UPROPERTY(ReplicatedUsing = OnRep_IsStageSelectionOpen)
	bool bIsStageSelectionOpen = false;

	UPROPERTY(ReplicatedUsing = OnRep_SelectedStageId)
	FName SelectedStageId = NAME_None;

#pragma endregion

#pragma region StageSelectionEvents

public:
	FCWOnStageSelectionStateChangedSignature
		OnStageSelectionStateChanged;

#pragma endregion
};
