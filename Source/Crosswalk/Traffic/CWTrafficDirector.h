// CWTrafficDirector.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CWTrafficSignalState.h"
#include "Data/Enums/CWStageTypes.h"
#include "CWTrafficDirector.generated.h"

class ACWGameState;
class ACWTrafficLane;
class ACWTrafficDirector;

/** 그룹의 색이 바뀔 때. 신호등과 연출이 여기 붙는다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FCWOnSignalGroupChangedSignature, ACWTrafficDirector*, Director, FName, GroupName, ECWTrafficSignalState, NewState, ECWTrafficSignalState, OldState);

/** 시퀀스 단계가 넘어갈 때. 사운드나 카메라 연출을 붙일 자리다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCWOnSignalStepChangedSignature, ACWTrafficDirector*, Director, int32, StepIndex, FName, StepName);

/**
 * 레벨의 교통 규칙을 굴린다. 신호등이 무슨 색인지 정하는 유일한 곳이다.
 *
 * 무엇을 위한 도구인가
 *   교차로 시뮬레이터가 아니라 **연출 도구**다. 레벨 디자이너가 "이 판의 교통은
 *   이렇게 돌아간다" 를 에디터에서 적고, 차량과 신호등이 그대로 따라가게 하는 것이 전부다.
 *
 * 세 층으로 되어 있고, 위에서부터 필요한 만큼만 쓴다
 *
 *   ① 그룹    같은 색을 함께 받는 차선 묶음. 이것만 있으면 색이 고정된 도로가 된다.
 *   ② 시퀀스  순서대로 도는 단계. 비우면 신호가 돌지 않고 그룹 기본색이 유지된다.
 *   ③ 규칙    게임에서 무슨 일이 생기면 개입한다. 비우면 아무 일도 안 생긴다.
 *
 *   왕복 2차선 연출용 레벨   - 그룹 2개(차량/보행), 시퀀스 없음, 규칙 1개
 *   8차선 교차로            - 그룹 5개, 시퀀스 4단계, 규칙은 필요할 때만
 *
 * 스폰은 건드리지 않는다
 *   신호는 **이미 나온 차를 세우고 보낼 뿐**이고, 차가 몇 대 나오는지는 스포너와
 *   ACWTrafficDifficulty 가 따로 정한다. 신호가 스폰까지 쥐면 신호 주기가 곧 차량
 *   밀도가 되어 외울 수 있는 패턴이 생기고, "초록 동안 나온 차가 정지선을 지나치면
 *   안 된다" 같은 맵 전제조건이 줄줄이 딸려온다. 그래서 분리한다.
 *
 * 네트워크 - 그룹별 색을 복제한다.
 *   정지선 판정을 서버와 클라이언트가 각자 하므로 양쪽이 같은 색을 알아야 한다.
 *   단계 인덱스만 복제하면 규칙이 개입한 상태를 표현할 수 없어서 색을 직접 보낸다.
 *   그룹당 1바이트라 단계 인덱스보다 크지만 항상 정확하다.
 */
UCLASS()
class CROSSWALK_API ACWTrafficDirector : public AActor
{
	GENERATED_BODY()

public:
	ACWTrafficDirector();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * 이 차선이 신호 통제를 받는지. 통제받지 않으면 차는 정지선을 무시하고 지나간다.
	 *
	 * 그룹의 색이 꺼짐이면 통제하지 않는 것으로 본다. 신호를 잠시 없애는 연출을
	 * 그룹 하나를 끄는 것으로 표현할 수 있어야 하기 때문이다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Signal")
	bool ControlsLane(const ACWTrafficLane* Lane) const;

	/**
	 * 이 차선이 지금 통행 가능한지. **황색은 초록과 같이 통행으로 본다.**
	 *
	 * 차를 세울지 말지는 색이 아니라 이 함수로 판정한다. 색 그대로가 필요한 곳
	 * (신호등 연출)은 GetGroupState 나 IsLaneGreen 을 쓴다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Signal")
	bool IsLanePassable(const ACWTrafficLane* Lane) const;

	/**
	 * 이 차선의 색이 초록인지. **황색은 false 다** - 색을 그대로 묻는 함수다.
	 *
	 * 차량 정지 판정에는 쓰지 않는다. 그건 IsLanePassable 이다.
	 * 예전에는 이 함수가 정지 판정까지 겸하고 있어서 황색에 차가 섰다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Signal")
	bool IsLaneGreen(const ACWTrafficLane* Lane) const;

	/** 그룹의 지금 색. 없는 그룹이면 꺼짐. */
	UFUNCTION(BlueprintPure, Category = "Traffic|Signal")
	ECWTrafficSignalState GetGroupState(FName GroupName) const;

	/**
	 * 색 이름. 로그·검산 패널·신호등이 전부 이걸 쓴다.
	 *
	 * 열거형의 DisplayName 을 그대로 돌려준다. 색 이름 표를 여기 말고 다른 곳에 두면
	 * 표가 갈라진다 - 실제로 제어기 안에 표 하나, UEnum 호출 두 벌로 세 갈래였다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Signal")
	static FText GetSignalStateText(ECWTrafficSignalState State);

	/** 지금 돌고 있는 시퀀스 단계. 시퀀스가 없거나 멈췄으면 INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "Traffic|Signal")
	int32 GetCurrentStepIndex() const { return CurrentStepIndex; }

	/** 시퀀스를 시작한다. bAutoStart 를 껐을 때 BP 에서 부른다. */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Signal")
	void StartSequence();

	/** 시퀀스를 멈춘다. 색은 멈춘 시점 그대로 남는다. */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Signal")
	void StopSequence();

	/**
	 * 그룹 색을 직접 바꾼다. 시퀀스는 건드리지 않으므로 다음 단계에서 덮어써진다.
	 * 계속 유지하려면 StopSequence 를 먼저 부른다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Signal")
	void SetGroupState(FName GroupName, ECWTrafficSignalState NewState);

	/** 이름으로 규칙을 발동한다. 조건이 '직접 호출' 인 규칙을 BP 에서 쏠 때 쓴다. */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Signal")
	void TriggerRule(FName RuleName);

	/**
	 * [검산] 신호 계획을 점검해 패널과 로그에 뿌린다.
	 *
	 * 제일 중요한 것은 **경로가 교차하는 그룹이 같은 단계에 초록인지**다. 그 상태에서는
	 * 차들이 교차로 한복판에서 서로를 통과하는데(차간 거리 유지가 같은 차선 안에서만
	 * 동작한다), 화면에서는 그냥 이상해 보일 뿐 원인이 안 보인다.
	 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Traffic|Signal")
	void ValidateSignalPlan();

protected:
	/** 색 초기화 자리. 모든 액터의 BeginPlay 보다 먼저 돌아야 신호등이 첫 색을 읽는다. */
	virtual void PostInitializeComponents() override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** 그룹 색을 기본값으로 채운다. 배열 길이를 Groups 와 맞추는 곳이기도 하다. */
	void ResetGroupStatesToDefault();

	/** 단계에 진입해 색을 적용하고 다음 전환을 예약한다. */
	void EnterStep(int32 StepIndex);

	/** 전환 구간. 모두 적색으로 두어 앞 단계의 차가 교차로를 빠져나가게 한다. */
	void BeginClearance();

	/** 전환이 끝나면 다음 단계로. */
	void AdvanceStep();

	/** 지정 목록대로 색을 칠하고, 목록에 없는 그룹은 적색으로 만든다. */
	void ApplyAssignments(const TArray<FCWTrafficSignalAssignment>& Assignments);

	/** 모든 그룹을 한 색으로. */
	void ApplyStateToAll(ECWTrafficSignalState NewState);

	/** 한 그룹의 색을 바꾸고 바뀌었으면 알린다. */
	void SetGroupStateAt(int32 GroupIndex, ECWTrafficSignalState NewState);

	/** 규칙을 예약한다. 지연이 없으면 바로 적용한다. */
	void ScheduleRule(int32 RuleIndex);

	/** 규칙을 실제로 적용한다. 지연이 끝난 뒤 불린다. */
	void ExecuteRule(int32 RuleIndex);

	/** 조건이 맞는 규칙을 찾아 예약한다. 이미 발동한 규칙은 건너뛴다. */
	void EvaluateRules();

	/**
	 * FiredRules 와 RuleTimerHandles 의 길이를 Rules 에 맞춘다. 두 배열에 쓰기 직전에 부른다.
	 *
	 * Rules 는 BlueprintReadWrite 라 런타임에 길이가 바뀔 수 있고, TriggerRule 은 다른
	 * 액터의 BeginPlay 에서 이 액터의 BeginPlay 보다 먼저 들어올 수 있다. 그 두 경우에
	 * 인덱스로 바로 쓰면 범위 밖이다.
	 *
	 * 기존 값은 보존한다. BeginPlay 가 이미 발동한 기록을 지우면 그 규칙이 두 번 발동한다.
	 */
	void EnsureRuleStateSized();

	/** 게임 상태 알림을 구독한다. */
	void BindStageEvents();

	UFUNCTION()
	void HandleStageProgressChanged(ACWGameState* GameState, int32 CompletedCount, int32 TotalCount);

	UFUNCTION()
	void HandleStageStateChanged(ACWGameState* GameState, ECWStageState NewStageState);

	UFUNCTION()
	void OnRep_GroupStates();

	/**
	 * 그룹 색이 바뀐 사건을 서버·클라이언트가 각자 찍는다. 진단 전용이고 게임플레이는 안 본다.
	 *
	 * 두 로그의 시각을 비교하면 신호가 클라이언트에 **언제** 도착했는지가 나온다.
	 * 클라이언트 쪽 줄이 아예 없으면 관련성 컬링에 걸려 복제가 안 되고 있다는 뜻이다.
	 */
	void LogSyncSignalChange(FName GroupName, ECWTrafficSignalState NewState, ECWTrafficSignalState OldState) const;

	/** 이름으로 그룹을 찾는다. 없으면 INDEX_NONE. */
	int32 FindGroupIndex(FName GroupName) const;

	/** 인덱스로 색을 읽는다. 아직 초기화 전이면 그룹의 기본색으로 답한다. */
	ECWTrafficSignalState ResolveGroupState(int32 GroupIndex) const;

	/** 이 차선을 담당하는 그룹의 색. 담당 그룹이 없으면 꺼짐. */
	ECWTrafficSignalState ResolveLaneState(const ACWTrafficLane* Lane) const;

	/** 두 그룹의 경로가 교차하는지. 검증에만 쓴다. */
	static bool DoGroupsConflict(const FCWTrafficSignalGroup& A, const FCWTrafficSignalGroup& B);

	/** 진입 방향 하나끼리의 교차 판정. */
	static bool DoApproachesConflict(ECWTrafficApproach ApproachA, ECWTrafficTurn TurnA, ECWTrafficApproach ApproachB, ECWTrafficTurn TurnB);

	/** 검산 표시를 다시 만든다. */
	void RefreshPreview();

	/**
	 * 정지선이 담당하는데 어느 그룹에도 없는 차선을 경고한다.
	 *
	 * 그룹에 없으면 정지선이 통째로 무시되는데, 증상이 "저 줄만 안 선다" 로만 보여서
	 * 원인에 도달하기 어렵다. 차선을 지웠다 다시 만들면 반드시 이 상태가 된다.
	 */
	void WarnOnUngroupedStopLineLanes();

protected:
	/**
	 * 같은 색을 함께 받는 차선 묶음. 하나 이상 있어야 한다.
	 *
	 * 왕복 2차선이면 차량 그룹 하나로 충분하고, 보행 신호 연출을 붙이려면 차선이 없는
	 * 그룹을 하나 더 만든다. 8차선 교차로는 방향×회전으로 4~5개가 된다.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Traffic|Signal")
	TArray<FCWTrafficSignalGroup> Groups;

	/**
	 * 순서대로 도는 단계. **비우면 신호가 돌지 않는다.**
	 *
	 * 비워두는 것이 잘못된 설정이 아니다 - 계속 초록인 도로가 그 경우이고, 그때는
	 * 그룹의 기본색이 그대로 유지된 채 규칙만 기다린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Signal")
	TArray<FCWTrafficSignalStep> Sequence;

	/** 게임 이벤트로 신호에 개입하는 규칙. 비우면 시퀀스만 돈다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Signal")
	TArray<FCWTrafficSignalRule> Rules;

	/** BeginPlay 에 시퀀스를 바로 돌릴지. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Signal")
	bool bAutoStart = true;

	/**
	 * [검산] 이 신호 계획이 실제로 어떻게 도는지. 값을 고치면 바로 갱신된다.
	 *
	 * 경로 교차 경고가 여기 뜬다. PIE 를 돌리지 않고 배치 단계에서 잡기 위한 것이다.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Traffic|Signal|Preview")
	TArray<FString> Preview;

private:
	/**
	 * 그룹별 현재 색. Groups 와 같은 순서다.
	 *
	 * 단계 인덱스가 아니라 색을 직접 복제하는 이유는 규칙 때문이다. 규칙이 개입하면
	 * 색이 어느 단계와도 일치하지 않는 상태가 되는데, 인덱스만 보내면 클라이언트가
	 * 그 상태를 만들 방법이 없다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_GroupStates)
	TArray<ECWTrafficSignalState> GroupStates;

	/**
	 * 마지막으로 알린 색. OnRep 에서 무엇이 바뀌었는지 가려내는 데만 쓴다.
	 *
	 * 배열 복제는 통째로 도착하므로 "어느 그룹이 바뀌었는가" 를 복제만으로는 알 수 없다.
	 * 그 정보가 없으면 신호등이 매번 전부 다시 그려지고, 색이 안 바뀐 그룹까지
	 * 연출 이벤트를 받는다.
	 */
	TArray<ECWTrafficSignalState> NotifiedStates;

	/** 지금 단계. 서버 기록용이라 복제하지 않는다. 클라이언트는 색만 알면 된다. */
	int32 CurrentStepIndex = INDEX_NONE;

	/** 이미 발동한 규칙. 같은 규칙이 두 번 걸리지 않게 한다. */
	TArray<bool> FiredRules;

	FTimerHandle StepTimerHandle;
	TArray<FTimerHandle> RuleTimerHandles;

	TWeakObjectPtr<ACWGameState> BoundGameState;

public:
	UPROPERTY(BlueprintAssignable, Category = "Traffic|Signal")
	FCWOnSignalGroupChangedSignature OnGroupStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Traffic|Signal")
	FCWOnSignalStepChangedSignature OnStepChanged;
};
