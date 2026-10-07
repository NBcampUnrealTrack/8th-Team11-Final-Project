// CWObjectiveViewModel.h

#pragma once

#include "CoreMinimal.h"
#include "UI/ViewModel/CWBaseGameStateViewModel.h"
#include "Data/Enums/CWStageTypes.h"
#include "Data/Structs/CWStageObjective.h"
#include "CWObjectiveViewModel.generated.h"

class ACWGameState;
class AGameStateBase;
class URetainerBox;

UCLASS(BlueprintType)
class CROSSWALK_API UCWObjectiveViewModel : public UCWBaseGameStateViewModel
{
	GENERATED_BODY()

#pragma region ObjectiveData

public:
	// HUD에 표시할 현재 작업 목표 목록을 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Objective")
	const TArray<FCWStageObjective>& GetObjectives() const
	{
		return Objectives;
	}

private:
	// 목표 목록을 갱신하고 FieldNotify를 발생
	void SetObjectives(const TArray<FCWStageObjective>& InObjectives);
	void PublishObjectives(const TArray<FCWStageObjective>& InObjectives);
	void UpdateObjectiveTransition();
	bool PrepareObjectiveInkLayer();
	void StopObjectiveTransition();

	UPROPERTY(Transient)
	TArray<FCWStageObjective> PendingObjectives;

	UPROPERTY(Transient)
	TWeakObjectPtr<URetainerBox> ObjectiveInkLayer;

	FTimerHandle ObjectiveTransitionTimer;
	float ObjectiveTransitionStarted = 0.f;
	bool bTransitioningObjectives = false;
	bool bTransitionListReplaced = false;
	bool bCleanupVisible = false;

	// 스테이지 클리어 여부를 갱신하고 FieldNotify를 발생
	void SetIsStageCleared(bool bInIsStageCleared);

#pragma endregion

#pragma region GameStateBinding

protected:
	// 바인딩할 GameState 클래스
	virtual UClass* GetGameStateClass() const override;

	// GameState에 목표·스테이지 상태 delegate 구독
	virtual void SubscribeToGameState(AGameStateBase* InGameState) override;

	// GameState에서 목표·스테이지 상태 delegate 해제
	virtual void UnsubscribeFromGameState(AGameStateBase* InGameState) override;

	// 현재 GameState의 목표·클리어 상태를 즉시 반영
	virtual void RefreshFromGameState() override;

	// 표시 상태를 기본값으로 되돌림
	virtual void ResetViewModelState() override;

private:
	// GameState의 목표 목록이 변경되면 ViewModel을 갱신
	UFUNCTION()
	void HandleStageObjectivesChanged(ACWGameState* InGameState);

	// GameState의 스테이지 상태가 변경되면 클리어 여부를 갱신
	UFUNCTION()
	void HandleStageStateChanged(
		ACWGameState* InGameState,
		ECWStageState InStageState
	);

#pragma endregion

#pragma region ViewModelState

private:
	// MVVM Binding에 표시되는 현재 스테이지의 목표 목록
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Objective",
		meta = (AllowPrivateAccess = "true")
	)
	TArray<FCWStageObjective> Objectives;

	// 스테이지 클리어 여부
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Objective",
		meta = (AllowPrivateAccess = "true")
	)
	bool bIsStageCleared = false;

#pragma endregion
};
