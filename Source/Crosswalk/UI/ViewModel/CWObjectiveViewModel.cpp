// CWObjectiveViewModel.cpp

#include "UI/ViewModel/CWObjectiveViewModel.h"
#include "CoreMinimal.h"
#include "Core/CWGameState.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

#pragma region ViewModelLifecycle

void UCWObjectiveViewModel::Initialize(UObject* WorldContextObject)
{
    // 재초기화 시 기존 델리게이트가 중복 등록되지 않도록 정리
    Uninitialize();

    if (!IsValid(WorldContextObject))
    {
        return;
    }

    // 전달받은 객체에서 현재 플레이 월드 확인
    UWorld* World = WorldContextObject->GetWorld();

    if (!IsValid(World))
    {
        return;
    }

    BoundWorld = World;

    // GameState가 늦게 생성되는 상황을 처리하기 위해 월드 이벤트 구독
    GameStateSetHandle = World->GameStateSetEvent.AddUObject(
        this,
        &UCWObjectiveViewModel::HandleGameStateSet
    );

    // 이미 GameState가 존재하면 즉시 구독하고 현재 데이터까지 읽음
    BindGameState(World->GetGameState<ACWGameState>());
}

void UCWObjectiveViewModel::Uninitialize()
{
    // 현재 GameState의 목표 변경 이벤트 구독 해제
    UnbindGameState();

    // 월드의 GameState 생성 이벤트 구독 해제
    if (UWorld* World = BoundWorld.Get())
    {
        if (GameStateSetHandle.IsValid())
        {
            World->GameStateSetEvent.Remove(GameStateSetHandle);
        }
    }

    GameStateSetHandle.Reset();
    BoundWorld.Reset();

    // 이전 월드의 목표가 다음 월드에 남지 않도록 초기화
    SetObjectives(TArray<FCWStageObjective>());

    // 이전 스테이지의 클리어 상태가 다음 월드에 남지 않도록 초기화
    SetIsStageCleared(false);
}

#pragma endregion

#pragma region ObjectiveData

void UCWObjectiveViewModel::SetObjectives(
    const TArray<FCWStageObjective>& InObjectives
)
{
    // 값이 실제로 변경된 경우에만 MVVM 알림 발생
    UE_MVVM_SET_PROPERTY_VALUE(Objectives, InObjectives);
}

void UCWObjectiveViewModel::SetIsStageCleared(
    bool bInIsStageCleared
)
{
    // 클리어 여부가 실제로 변경된 경우에만 MVVM 알림 발생
    UE_MVVM_SET_PROPERTY_VALUE(
        bIsStageCleared,
        bInIsStageCleared
    );
}

#pragma endregion

#pragma region GameStateBinding

void UCWObjectiveViewModel::BindGameState(
    ACWGameState* InGameState
)
{
    if (!IsValid(InGameState))
    {
        return;
    }

    // 동일한 GameState에 중복 바인딩하지 않음
    if (BoundGameState.Get() == InGameState)
    {
        return;
    }

    UnbindGameState();

    BoundGameState = InGameState;

    // 복제된 목표 목록이 변경될 때 알림 수신
    InGameState->OnStageObjectivesChanged.AddDynamic(
        this,
        &UCWObjectiveViewModel::HandleStageObjectivesChanged
    );

    // 복제된 스테이지 상태가 변경될 때 알림 수신
    InGameState->OnStageStateChanged.AddDynamic(
        this,
        &UCWObjectiveViewModel::HandleStageStateChanged
    );

    // OnRep가 이미 지난 경우를 위해 현재 값을 즉시 한 번 읽음
    SetObjectives(InGameState->GetObjectives());
    SetIsStageCleared(InGameState->IsStageCleared());
}

void UCWObjectiveViewModel::UnbindGameState()
{
    if (ACWGameState* GameState = BoundGameState.Get())
    {
        GameState->OnStageObjectivesChanged.RemoveDynamic(
            this,
            &UCWObjectiveViewModel::HandleStageObjectivesChanged
        );

        GameState->OnStageStateChanged.RemoveDynamic(
            this,
            &UCWObjectiveViewModel::HandleStageStateChanged
        );
    }

    BoundGameState.Reset();
}

void UCWObjectiveViewModel::HandleStageObjectivesChanged(
    ACWGameState* InGameState
)
{
    if (!IsValid(InGameState))
    {
        return;
    }

    // 서버에서 갱신·복제된 최신 목표 배열 저장
    SetObjectives(InGameState->GetObjectives());
}

void UCWObjectiveViewModel::HandleStageStateChanged(
    ACWGameState* InGameState,
    ECWStageState InStageState
)
{
    if (!IsValid(InGameState))
    {
        return;
    }

    // GameMode가 확정하고 복제한 스테이지 상태를 그대로 반영
    SetIsStageCleared(
        InStageState == ECWStageState::Cleared
    );
}

void UCWObjectiveViewModel::HandleGameStateSet(
    AGameStateBase* InGameState
)
{
    // 프로젝트 전용 GameState일 때만 목표 목록 구독
    BindGameState(Cast<ACWGameState>(InGameState));
}

#pragma endregion
