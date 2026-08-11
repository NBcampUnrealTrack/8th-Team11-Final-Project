// CWObjectiveViewModel.h

#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "Data/Enums/CWStageTypes.h"
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

    // 스테이지 클리어 여부를 갱신하고 FieldNotify를 발생
    void SetIsStageCleared(bool bInIsStageCleared);

#pragma endregion

#pragma region GameStateBinding

private:
    // 현재 월드의 GameState를 구독
    void BindGameState(ACWGameState* InGameState);

    // 기존 GameState 구독을 해제
    void UnbindGameState();

    // GameState의 목표 목록이 변경되면 ViewModel을 갱신
    UFUNCTION()
    void HandleStageObjectivesChanged(ACWGameState* InGameState);

    // GameState의 스테이지 상태가 변경되면 클리어 여부를 갱신
    UFUNCTION()
    void HandleStageStateChanged(
        ACWGameState* InGameState,
        ECWStageState InStageState
    );

    // GameState가 늦게 생성된 경우 다시 구독
    void HandleGameStateSet(AGameStateBase* InGameState);

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

    // 현재 구독 중인 World
    UPROPERTY()
    TWeakObjectPtr<UWorld> BoundWorld;

    // GameState 생성 지연을 처리하는 델리게이트 핸들
    FDelegateHandle GameStateSetHandle;

#pragma endregion
};
