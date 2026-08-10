// Copyright Crosswalk Team. All Rights Reserved.
// CWGameState.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Data/Enums/CWStageTypes.h"
#include "Data/Structs/CWStageObjective.h"
#include "CWGameState.generated.h"

class ACWGameState;

/** 목표 달성 개수가 바뀔 때. 서버와 클라이언트 양쪽에서 발행된다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCWOnStageProgressChangedSignature, ACWGameState*, GameState, int32, CompletedCount, int32, TotalCount);

/** 스테이지 상태가 바뀔 때. 클리어 연출과 결과 화면이 여기에 붙는다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnStageStateChangedSignature, ACWGameState*, GameState, ECWStageState, StageState);

/** 목표 체크리스트가 바뀔 때. HUD 좌측 목록(기획서 3.6)이 여기에 붙는다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCWOnStageObjectivesChangedSignature, ACWGameState*, GameState);

/**
 * 한 판의 진행 상황을 전원이 같은 값으로 들고 있는 곳.
 *
 * 값은 서버가 정하고 여기에 실어 보낸다. 클라이언트는 읽기만 한다.
 * 판정 자체는 ACWGameMode 가 하고, 이 클래스는 결과를 담아 나르는 역할만 한다.
 * (게임모드는 서버에만 존재해서 클라이언트 HUD 가 참조할 수 없다)
 */
UCLASS()
class CROSSWALK_API ACWGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 달성한 작업 목표 수를 갱신한다. 서버에서만 유효하다. */
	void SetObjectiveCounts(int32 InCompletedCount, int32 InTotalCount);

	/** 스테이지 상태를 바꾼다. 서버에서만 유효하다. */
	void SetStageState(ECWStageState NewStageState);

	/**
	 * 목표 체크리스트를 갱신한다. 내용이 같으면 아무 일도 하지 않는다. 서버에서만 유효하다.
	 * @return 실제로 바뀌었으면 true
	 */
	bool SetObjectives(const TArray<FCWStageObjective>& NewObjectives);

	/** HUD 체크리스트용 목표 목록. 작업 순서대로 정렬돼 있다. */
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Stage")
	const TArray<FCWStageObjective>& GetObjectives() const { return Objectives; }

	UFUNCTION(BlueprintPure, Category = "Crosswalk|Stage")
	int32 GetCompletedObjectiveCount() const { return CompletedObjectiveCount; }

	UFUNCTION(BlueprintPure, Category = "Crosswalk|Stage")
	int32 GetTotalObjectiveCount() const { return TotalObjectiveCount; }

	UFUNCTION(BlueprintPure, Category = "Crosswalk|Stage")
	ECWStageState GetStageState() const { return StageState; }

	/** 전체 목표 대비 달성 비율(0~1). 목표가 없으면 0. HUD 진행 바에 그대로 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Stage")
	float GetStageProgress() const;

	UFUNCTION(BlueprintPure, Category = "Crosswalk|Stage")
	bool IsStageCleared() const { return StageState == ECWStageState::Cleared; }

private:
	UFUNCTION()
	void OnRep_ObjectiveCounts();

	UFUNCTION()
	void OnRep_StageState();

	UFUNCTION()
	void OnRep_Objectives();

private:
	// 아래 3개는 서버가 정하고 전원에게 전파된다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_ObjectiveCounts, Category = "Crosswalk|Stage", meta = (AllowPrivateAccess = "true"))
	int32 CompletedObjectiveCount = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_ObjectiveCounts, Category = "Crosswalk|Stage", meta = (AllowPrivateAccess = "true"))
	int32 TotalObjectiveCount = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_StageState, Category = "Crosswalk|Stage", meta = (AllowPrivateAccess = "true"))
	ECWStageState StageState = ECWStageState::InProgress;

	// 이름으로 묶은 목표 목록. 존 개수가 아니라 이 배열의 길이가 체크리스트 줄 수다
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_Objectives, Category = "Crosswalk|Stage", meta = (AllowPrivateAccess = "true"))
	TArray<FCWStageObjective> Objectives;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Crosswalk|Stage")
	FCWOnStageProgressChangedSignature OnStageProgressChanged;

	UPROPERTY(BlueprintAssignable, Category = "Crosswalk|Stage")
	FCWOnStageStateChangedSignature OnStageStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Crosswalk|Stage")
	FCWOnStageObjectivesChangedSignature OnStageObjectivesChanged;
};
