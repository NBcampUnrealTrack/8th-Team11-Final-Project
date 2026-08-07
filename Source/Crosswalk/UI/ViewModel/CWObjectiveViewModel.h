// CWObjectiveViewModel.h

#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "Data/Structs/CWStageObjective.h"
#include "CWObjectiveViewModel.generated.h"

class ACWGameState;
class AGameStateBase;
class UWorld;

UCLASS(BlueprintType)
class CROSSWALK_API UCWObjectiveViewModel : public UMVVMViewModelBase
{
    GENERATED_BODY()

#pragma region ViewModelLifecycle

public:
    // 현재 월드의 GameState를 찾아서 목표 데이터 구독 시작
    UFUNCTION(BlueprintCallable, Category = "CW|UI|Objective")
    void Initialize(UObject* WorldContextObject);

    // GameState와 World 델리게이트 구독을 정리
    UFUNCTION(BlueprintCallable, Category = "CW|UI|Objective")
    void Uninitialize();

#pragma endregion

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

#pragma endregion

#pragma region GameStateBinding

private:
    // 현재 월드에 생성된 GameState를 구독
    void BindGameState(ACWGameState* InGameState);

    // 현재 구독 중인 GameState에서 구독을 해제
    void UnbindGameState();

    // GameState의 목표 목록이 변경되면 ViewModel을 갱신
    UFUNCTION()
    void HandleStageObjectivesChanged(ACWGameState* InGameState);

    // GameState가 늦게 생성된 경우 다시 구독
    void HandleGameStateSet(AGameStateBase* InGameState);

#pragma endregion

#pragma region ViewModelState

private:
    // MVVM Binding에 표시되는 작업 목표 배열
    UPROPERTY(
        BlueprintReadOnly,
        FieldNotify,
        Category = "CW|UI|Objective",
        meta = (AllowPrivateAccess = "true")
    )
    TArray<FCWStageObjective> Objectives;

    // 현재 구독 중인 GameState
    UPROPERTY()
    TWeakObjectPtr<ACWGameState> BoundGameState;

    // 초기화에 사용된 현재 월드
    UPROPERTY()
    TWeakObjectPtr<UWorld> BoundWorld;

    // GameState 생성 알림 구독을 해제하기 위한 핸들
    FDelegateHandle GameStateSetHandle;

#pragma endregion
};