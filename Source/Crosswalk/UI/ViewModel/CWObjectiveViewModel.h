// CWObjectiveViewModel.h

#pragma once

#include "CoreMinimal.h"
#include "UI/ViewModel/CWBaseGameStateViewModel.h"
#include "Data/Enums/CWStageTypes.h"
#include "Data/Structs/CWStageObjective.h"
#include "CWObjectiveViewModel.generated.h"

class ACWGameState;
class AGameStateBase;

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

	// 스테이지 클리어 여부를 갱신하고 FieldNotify를 발생
	void SetIsStageCleared(bool bInIsStageCleared);

#pragma endregion

#pragma region GameStateBinding

protected:
	// 프로젝트 전용 GameState일 때만 목표 데이터를 구독
	virtual void BindGameState(AGameStateBase* InGameState) override;

	// 기존 GameState 구독을 해제
	virtual void UnbindGameState() override;

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

	// 현재 구독 중인 GameState
	UPROPERTY()
	TWeakObjectPtr<ACWGameState> BoundGameState;

#pragma endregion
};
