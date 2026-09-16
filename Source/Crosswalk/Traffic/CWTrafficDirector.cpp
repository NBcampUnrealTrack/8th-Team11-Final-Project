// CWTrafficDirector.cpp

#include "CWTrafficDirector.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "CWTrafficLane.h"
#include "CWTrafficStopLine.h"
#include "CWTrafficSubsystem.h"
#include "Core/CWGameState.h"
#include "Traffic/CWTrafficLog.h"

namespace
{
	/** 남북 축인지. 교차 판정에서 축이 같은지 다른지가 첫 갈림길이다. */
	bool IsNorthSouthAxis(ECWTrafficApproach Approach)
	{
		return Approach == ECWTrafficApproach::North || Approach == ECWTrafficApproach::South;
	}
}

FText ACWTrafficDirector::GetSignalStateText(ECWTrafficSignalState State)
{
	return UEnum::GetDisplayValueAsText(State);
}

ACWTrafficDirector::ACWTrafficDirector()
{
	PrimaryActorTick.bCanEverTick = false;

	// 그룹 색을 클라이언트도 알아야 한다. 정지선 판정을 양쪽이 각자 하기 때문이다.
	bReplicates = true;

	/**
	 * 관련성 컬링에서 뺀다. 이걸 안 하면 참가자 화면에서 차가 정지선을 그냥 지나간다.
	 *
	 * 이 액터는 보이는 물건이 아니라 레벨 전체의 신호 상태를 들고 있는 논리 액터인데,
	 * 기본 관련성은 액터 위치와 플레이어 거리로 판정한다. 루트 컴포넌트가 없어 위치가
	 * 월드 원점으로 잡히고, 플레이 구역이 원점에서 기본 컬링 거리(150m) 밖이면
	 * 클라이언트에는 GroupStates 가 영영 도착하지 않는다.
	 *
	 * 그러면 ResolveGroupState 가 복제값 대신 그룹의 기본색(보통 초록)으로 답하고,
	 * FindActiveStopLine 이 "초록이니 안 세운다" 로 빠져나간다. 호스트만 서고 참가자는
	 * 그대로 지나가는 것이 이 경로다. 주행 자체는 멀쩡해서 원인이 안 보인다.
	 *
	 * 비용은 무시할 만하다. 흐르는 것은 그룹 수만큼의 uint8 배열 하나이고, 색이 바뀔 때만이다.
	 */
	bAlwaysRelevant = true;
}

void ACWTrafficDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACWTrafficDirector, GroupStates);
}

bool ACWTrafficDirector::ControlsLane(const ACWTrafficLane* Lane) const
{
	// 꺼진 그룹은 통제하지 않는 것으로 본다. 신호를 잠시 없애는 연출이 가능해야 한다.
	return ResolveLaneState(Lane) != ECWTrafficSignalState::Off;
}

bool ACWTrafficDirector::IsLaneGreen(const ACWTrafficLane* Lane) const
{
	return ResolveLaneState(Lane) == ECWTrafficSignalState::Green;
}

bool ACWTrafficDirector::IsLanePassable(const ACWTrafficLane* Lane) const
{
	/**
	 * 황색을 통행으로 본다.
	 *
	 * 황색은 "곧 적색이 된다" 는 예고이지 정지 지시가 아니다. 여기서 세우면 정지선을 막 지난
	 * 차가 교차로 한가운데에서 급정거하고, 그대로 적색이 되어 다음 방향 차들이 지나갈 자리를
	 * 막는다. 실제 도로에서 황색에 이미 진입한 차를 보내는 이유와 같다.
	 *
	 * 단계 사이의 ClearanceDuration 이 그 차들이 빠져나갈 시간을 이미 벌어 준다.
	 * 황색에 세우면 그 시간이 하는 일이 없어진다.
	 *
	 * 꺼짐은 여기서 통행으로 치지 않는다. 신호가 없는 차선은 ControlsLane 이 먼저 걸러내므로
	 * 이 함수까지 오지 않고, 제어기가 둘일 때의 판정을 바꾸지 않기 위해서다.
	 */
	const ECWTrafficSignalState State = ResolveLaneState(Lane);

	return State == ECWTrafficSignalState::Green || State == ECWTrafficSignalState::Yellow;
}

ECWTrafficSignalState ACWTrafficDirector::ResolveLaneState(const ACWTrafficLane* Lane) const
{
	if (!Lane)
	{
		return ECWTrafficSignalState::Off;
	}

	// 같은 차선이 여러 그룹에 들어 있으면 먼저 찾은 그룹이 이긴다. 배치 실수라 여기서 뭉개지 않는다.
	for (int32 Index = 0; Index < Groups.Num(); ++Index)
	{
		if (Groups[Index].Lanes.Contains(Lane))
		{
			return ResolveGroupState(Index);
		}
	}

	return ECWTrafficSignalState::Off;
}

ECWTrafficSignalState ACWTrafficDirector::GetGroupState(FName GroupName) const
{
	return ResolveGroupState(FindGroupIndex(GroupName));
}

ECWTrafficSignalState ACWTrafficDirector::ResolveGroupState(int32 GroupIndex) const
{
	if (!Groups.IsValidIndex(GroupIndex))
	{
		return ECWTrafficSignalState::Off;
	}

	/**
	 * 아직 초기화 전이면 기본색으로 답한다.
	 *
	 * 액터 초기화 순서는 보장되지 않아서, 신호등이나 차량이 제어기보다 먼저 깨어나
	 * 색을 물어볼 수 있다. 그때 꺼짐을 돌려주면 신호등은 "이미 꺼짐이네" 하고 넘어가고,
	 * 그 뒤로 색이 바뀔 일이 없으면 영영 꺼진 채로 남는다. 실제로 그렇게 됐다.
	 *
	 * 아무 로직도 돌기 전의 색은 정의상 기본색이므로 이렇게 답하는 것이 맞다.
	 */
	return GroupStates.IsValidIndex(GroupIndex) ? GroupStates[GroupIndex] : Groups[GroupIndex].DefaultState;
}

void ACWTrafficDirector::StartSequence()
{
	if (!HasAuthority() || Sequence.Num() == 0)
	{
		return;
	}

	EnterStep(0);
}

void ACWTrafficDirector::StopSequence()
{
	if (!HasAuthority())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(StepTimerHandle);
	CurrentStepIndex = INDEX_NONE;
}

void ACWTrafficDirector::SetGroupState(FName GroupName, ECWTrafficSignalState NewState)
{
	if (!HasAuthority())
	{
		return;
	}

	const int32 Index = FindGroupIndex(GroupName);
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogCWTraffic, Warning, TEXT("[%s] '%s' 그룹이 없어 색을 바꾸지 못했다."), *GetName(), *GroupName.ToString());
		return;
	}

	SetGroupStateAt(Index, NewState);
}

void ACWTrafficDirector::TriggerRule(FName RuleName)
{
	if (!HasAuthority())
	{
		return;
	}

	EnsureRuleStateSized();

	for (int32 Index = 0; Index < Rules.Num(); ++Index)
	{
		if (Rules[Index].RuleName != RuleName || FiredRules[Index])
		{
			continue;
		}

		FiredRules[Index] = true;
		ScheduleRule(Index);

		return;
	}
}

void ACWTrafficDirector::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	/**
	 * 색 초기화는 BeginPlay 가 아니라 여기서 한다.
	 *
	 * 월드는 모든 액터의 PostInitializeComponents 를 끝낸 뒤에야 BeginPlay 를 돌린다.
	 * BeginPlay 에서 채우면 신호등이나 차량이 먼저 깨어났을 때 빈 배열을 보게 되고,
	 * 그러면 신호등이 꺼진 채로 남는다. 클라이언트에서도 해야 복제가 도착하기 전까지
	 * ControlsLane 이 제 답을 낸다.
	 */
	ResetGroupStatesToDefault();

	/**
	 * 관련성을 여기서 다시 못박는다. **생성자에서 켠 것만으로는 실제로 안 켜졌다.**
	 *
	 * 블루프린트 CDO 는 부모 C++ 기본값을 실시간으로 따라가지 않고 에셋에 굳은 값을 쓴다.
	 * 그래서 생성자의 bAlwaysRelevant = true 가 BP 에 저장된 옛날 값(꺼짐)으로 덮이고,
	 * 컴파일은 통과하는데 런타임에는 꺼진 채로 돈다. 2인 테스트에서 실제로 그랬다 -
	 * 서버는 색을 바꿨는데 클라이언트 신호등은 끝까지 초록이었다.
	 *
	 * BP 의 체크박스를 켜서 고칠 수도 있지만 그러면 수정이 uasset 안에 숨는다. grep 도
	 * 리뷰도 안 되고, 새로 만드는 제어기 BP 마다 같은 실수가 반복된다.
	 *
	 * 이 값은 배치로 정할 성질이 아니다. 관련성 컬링에 걸리면 클라이언트가 신호를 영영
	 * 못 받아 정지선이 없는 것처럼 주행하므로, 끌 수 있는 값으로 두면 안 된다.
	 */
	bAlwaysRelevant = true;

	/**
	 * 복제도 같은 이유로 여기서 다시 못박는다. **생성자의 bReplicates = true 도 안 먹었다.**
	 *
	 * 위에서 bAlwaysRelevant 하나만 막아 둔 것이 그대로 버그가 됐다. 같은 BP CDO 가 같은
	 * 방식으로 덮는 값인데 한 칸만 막아 두었으니, 관련성은 살아나고 복제는 꺼진 채로 남았다.
	 * 복제가 꺼지면 관련성은 볼 것도 없다 - 내보낼 것이 애초에 없다.
	 *
	 * 실측: [TrafficSync] 복제설정 줄이 서버에서 "복제 꺼짐 / 항상관련 켜짐" 으로 나왔다.
	 * 그 상태에서 클라이언트는 GroupStates 를 영영 못 받아 그룹 기본색(보통 초록)으로 답하고,
	 * 신호등은 첫 색에 굳고, 차량은 정지선이 없는 것처럼 달린다. 증상 셋이 전부 여기서 나온다.
	 *
	 * SetReplicates 는 권한 쪽에서만 부른다. 그렇지 않으면 엔진이 경고만 찍고 아무것도
	 * 하지 않는다. 실제로 복제를 시작시키는 쪽은 서버이므로 그것으로 충분하다.
	 */
	if (GetLocalRole() == ROLE_Authority)
	{
		SetReplicates(true);
	}
}

void ACWTrafficDirector::BeginPlay()
{
	Super::BeginPlay();

	if (Groups.Num() == 0)
	{
		UE_LOG(LogCWTraffic, Warning, TEXT("[%s] 신호 그룹이 없어 아무 차선도 통제하지 않는다."), *GetName());
	}

	/**
	 * 복제에 필요한 조건이 런타임에 실제로 켜져 있는지 찍는다. 서버·클라이언트 양쪽에서.
	 *
	 * 이 값들은 C++ 생성자에서 정하지만, 블루프린트 CDO 에 굳은 옛날 값이 그것을 덮을 수
	 * 있다(PostInitializeComponents 의 bAlwaysRelevant 주석 참고). 그러면 컴파일도 통과하고
	 * 코드를 읽어도 켜져 있는 것처럼 보이는데 런타임에는 꺼져 있다. 소스만 봐서는
	 * 확인할 수 없는 값이라 찍어야 알 수 있다.
	 *
	 * 증상은 "클라이언트 차량이 정지선을 그냥 지나간다" 하나로 뭉쳐서 나오므로, 이 줄이
	 * 없으면 복제가 꺼진 것인지 관련성에 걸린 것인지 휴면인지 갈라낼 수 없다.
	 */
	if (UCWTrafficSubsystem::IsSyncLoggingEnabled())
	{
		/**
		 * 월드 역할과 액터 권한을 따로 찍는다. 복제되지 않는 액터는 클라이언트에서도
		 * 권한이 "있음" 으로 남는다 - 서버 채널에 묶인 적이 없기 때문이다. 그래서 이 두
		 * 칸이 "클라 / 있음" 으로 나오면 그 자체가 복제가 안 됐다는 증거가 된다.
		 */
		UE_LOG(LogCWTraffic, Log, TEXT("[TrafficSync][%s] %s 복제설정 - 권한 %s / 복제 %s / 항상관련 %s / 휴면 %d / 갱신빈도 %.1f / 그룹 %d개"),
			GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("클라") : TEXT("서버"),
			*GetName(),
			HasAuthority() ? TEXT("있음") : TEXT("없음"),
			GetIsReplicated() ? TEXT("켜짐") : TEXT("꺼짐"),
			bAlwaysRelevant ? TEXT("켜짐") : TEXT("꺼짐"),
			static_cast<int32>(NetDormancy),
			GetNetUpdateFrequency(),
			Groups.Num());
	}

	// 등록은 클라이언트에서도 한다. 정지선 판정을 양쪽 차량이 각자 하기 때문이다.
	if (UCWTrafficSubsystem* TrafficSubsystem = UCWTrafficSubsystem::Get(this))
	{
		TrafficSubsystem->RegisterSignalDirector(this);
	}

	/**
	 * 계획 검산은 제어기가 한 번 한다.
	 *
	 * 예전에는 신호등 컴포넌트마다 BeginPlay 에서 불러서, 8차선이면 같은 검산 로그가
	 * 8번 찍혔다. 검산 대상은 신호등이 아니라 이 액터의 계획이므로 여기가 맞다.
	 * 신호등이 하나도 없는 레벨도 검산은 받아야 한다 - 정지선 배선 실수는 신호등과 무관하다.
	 */
	ValidateSignalPlan();

	if (!HasAuthority())
	{
		return;
	}

	// Init 으로 비우지 않는다. BeginPlay 전에 TriggerRule 로 발동한 기록이 있으면 남겨야 한다.
	EnsureRuleStateSized();

	BindStageEvents();

	if (bAutoStart && Sequence.Num() > 0)
	{
		EnterStep(0);
	}

	// 시작 시점에 이미 조건이 맞는 규칙이 있을 수 있다. (진행도 0% 규칙 등)
	EvaluateRules();
}

void ACWTrafficDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StepTimerHandle);
		for (FTimerHandle& Handle : RuleTimerHandles)
		{
			World->GetTimerManager().ClearTimer(Handle);
		}
	}

	if (ACWGameState* GameState = BoundGameState.Get())
	{
		GameState->OnStageProgressChanged.RemoveDynamic(this, &ACWTrafficDirector::HandleStageProgressChanged);
		GameState->OnStageStateChanged.RemoveDynamic(this, &ACWTrafficDirector::HandleStageStateChanged);
	}
	BoundGameState.Reset();

	if (UCWTrafficSubsystem* TrafficSubsystem = UCWTrafficSubsystem::Get(this))
	{
		TrafficSubsystem->UnregisterSignalDirector(this);
	}

	Super::EndPlay(EndPlayReason);
}

void ACWTrafficDirector::PostLoad()
{
	Super::PostLoad();

	RefreshPreview();
}

void ACWTrafficDirector::ResetGroupStatesToDefault()
{
	GroupStates.SetNum(Groups.Num());
	for (int32 Index = 0; Index < Groups.Num(); ++Index)
	{
		GroupStates[Index] = Groups[Index].DefaultState;
	}

	NotifiedStates = GroupStates;
}

void ACWTrafficDirector::EnterStep(int32 StepIndex)
{
	if (!Sequence.IsValidIndex(StepIndex))
	{
		return;
	}

	CurrentStepIndex = StepIndex;

	const FCWTrafficSignalStep& Step = Sequence[StepIndex];
	ApplyAssignments(Step.Assignments);

	UE_LOG(LogCWTraffic, Log, TEXT("[%s] 단계 %d(%s) / %.1f초"), *GetName(), StepIndex, *Step.StepName.ToString(), Step.Duration);

	OnStepChanged.Broadcast(this, StepIndex, Step.StepName);

	// 단계가 하나뿐이면 넘어갈 곳이 없다. 그 색으로 고정된다.
	if (Sequence.Num() > 1)
	{
		GetWorldTimerManager().SetTimer(StepTimerHandle, this, &ACWTrafficDirector::BeginClearance, Step.Duration, false);
	}
}

void ACWTrafficDirector::BeginClearance()
{
	const FCWTrafficSignalStep* Step = Sequence.IsValidIndex(CurrentStepIndex) ? &Sequence[CurrentStepIndex] : nullptr;
	const float ClearanceDuration = Step ? Step->ClearanceDuration : 0.f;

	if (ClearanceDuration <= KINDA_SMALL_NUMBER)
	{
		AdvanceStep();
		return;
	}

	/**
	 * 전환 구간은 전부 적색이다.
	 *
	 * 황색을 통행 가능으로 두면 전환 구간 내내 차가 계속 들어와서 전환의 의미가 없어진다.
	 * 이미 정지선을 지난 차는 어차피 계속 가므로(FindActiveStopLine 이 지난 정지선을
	 * 무시한다), 적색으로 두는 것만으로 "들어오지 않고 빠져나가기만 하는" 구간이 된다.
	 */
	ApplyStateToAll(ECWTrafficSignalState::Red);

	GetWorldTimerManager().SetTimer(StepTimerHandle, this, &ACWTrafficDirector::AdvanceStep, ClearanceDuration, false);
}

void ACWTrafficDirector::AdvanceStep()
{
	if (Sequence.Num() == 0)
	{
		return;
	}

	const int32 NextIndex = (CurrentStepIndex + 1) % Sequence.Num();
	EnterStep(NextIndex);
}

void ACWTrafficDirector::ApplyAssignments(const TArray<FCWTrafficSignalAssignment>& Assignments)
{
	for (int32 GroupIndex = 0; GroupIndex < Groups.Num(); ++GroupIndex)
	{
		// 지정 목록에 없는 그룹은 적색이다. 단계마다 모든 그룹을 나열하지 않아도 되게 한
		// 것이고, 빠뜨린 그룹이 조용히 초록으로 남는 것을 막는다.
		ECWTrafficSignalState NewState = ECWTrafficSignalState::Red;

		for (const FCWTrafficSignalAssignment& Assignment : Assignments)
		{
			if (Assignment.GroupName == Groups[GroupIndex].GroupName)
			{
				NewState = Assignment.State;
				break;
			}
		}

		SetGroupStateAt(GroupIndex, NewState);
	}
}

void ACWTrafficDirector::ApplyStateToAll(ECWTrafficSignalState NewState)
{
	for (int32 GroupIndex = 0; GroupIndex < Groups.Num(); ++GroupIndex)
	{
		SetGroupStateAt(GroupIndex, NewState);
	}
}

void ACWTrafficDirector::SetGroupStateAt(int32 GroupIndex, ECWTrafficSignalState NewState)
{
	if (!GroupStates.IsValidIndex(GroupIndex) || GroupStates[GroupIndex] == NewState)
	{
		return;
	}

	const ECWTrafficSignalState OldState = GroupStates[GroupIndex];
	GroupStates[GroupIndex] = NewState;

	if (NotifiedStates.IsValidIndex(GroupIndex))
	{
		NotifiedStates[GroupIndex] = NewState;
	}

	LogSyncSignalChange(Groups[GroupIndex].GroupName, NewState, OldState);

	OnGroupStateChanged.Broadcast(this, Groups[GroupIndex].GroupName, NewState, OldState);
}

void ACWTrafficDirector::LogSyncSignalChange(FName GroupName, ECWTrafficSignalState NewState, ECWTrafficSignalState OldState) const
{
	if (!UCWTrafficSubsystem::IsSyncLoggingEnabled())
	{
		return;
	}

	UE_LOG(LogCWTraffic, Log, TEXT("[TrafficSync][%s] %s 그룹 '%s' %s <- %s"),
		UCWTrafficSubsystem::GetNetRoleTag(this),
		*GetName(),
		*GroupName.ToString(),
		*GetSignalStateText(NewState).ToString(),
		*GetSignalStateText(OldState).ToString());
}

void ACWTrafficDirector::ScheduleRule(int32 RuleIndex)
{
	if (!Rules.IsValidIndex(RuleIndex))
	{
		return;
	}

	if (Rules[RuleIndex].Delay > 0.f && RuleTimerHandles.IsValidIndex(RuleIndex))
	{
		FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &ACWTrafficDirector::ExecuteRule, RuleIndex);
		GetWorldTimerManager().SetTimer(RuleTimerHandles[RuleIndex], Delegate, Rules[RuleIndex].Delay, false);
		return;
	}

	ExecuteRule(RuleIndex);
}

void ACWTrafficDirector::ExecuteRule(int32 RuleIndex)
{
	if (!Rules.IsValidIndex(RuleIndex))
	{
		return;
	}

	const FCWTrafficSignalRule& Rule = Rules[RuleIndex];

	if (Rule.bStopSequence)
	{
		StopSequence();
	}

	ApplyAssignments(Rule.Assignments);

	UE_LOG(LogCWTraffic, Log, TEXT("[%s] 규칙 '%s' 발동"), *GetName(), *Rule.RuleName.ToString());
}

void ACWTrafficDirector::EvaluateRules()
{
	const ACWGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACWGameState>() : nullptr;
	if (!GameState)
	{
		return;
	}

	EnsureRuleStateSized();

	for (int32 Index = 0; Index < Rules.Num(); ++Index)
	{
		if (FiredRules[Index])
		{
			continue;
		}

		const FCWTrafficSignalRule& Rule = Rules[Index];

		bool bMet = false;
		switch (Rule.Trigger)
		{
		case ECWTrafficSignalTrigger::StageProgress:
			bMet = GameState->GetStageProgress() >= Rule.ProgressThreshold;
			break;

		case ECWTrafficSignalTrigger::StageCleared:
			bMet = GameState->IsStageCleared();
			break;

		default:
			// 직접 호출은 여기서 걸리지 않는다. TriggerRule 만이 발동시킨다.
			break;
		}

		if (!bMet)
		{
			continue;
		}

		FiredRules[Index] = true;
		ScheduleRule(Index);
	}
}

void ACWTrafficDirector::EnsureRuleStateSized()
{
	// SetNum 은 늘어난 칸을 0(false / 빈 핸들)으로 채우고 기존 칸은 건드리지 않는다.
	if (FiredRules.Num() != Rules.Num())
	{
		FiredRules.SetNum(Rules.Num());
	}

	if (RuleTimerHandles.Num() != Rules.Num())
	{
		RuleTimerHandles.SetNum(Rules.Num());
	}
}

void ACWTrafficDirector::BindStageEvents()
{
	ACWGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACWGameState>() : nullptr;
	if (!GameState)
	{
		UE_LOG(LogCWTraffic, Warning, TEXT("[%s] CWGameState 를 찾지 못해 게임 이벤트 규칙이 동작하지 않는다."), *GetName());
		return;
	}

	GameState->OnStageProgressChanged.AddDynamic(this, &ACWTrafficDirector::HandleStageProgressChanged);
	GameState->OnStageStateChanged.AddDynamic(this, &ACWTrafficDirector::HandleStageStateChanged);
	BoundGameState = GameState;
}

void ACWTrafficDirector::HandleStageProgressChanged(ACWGameState* /*GameState*/, int32 /*CompletedCount*/, int32 /*TotalCount*/)
{
	EvaluateRules();
}

void ACWTrafficDirector::HandleStageStateChanged(ACWGameState* /*GameState*/, ECWStageState /*NewStageState*/)
{
	EvaluateRules();
}

void ACWTrafficDirector::OnRep_GroupStates()
{
	// 복제는 배열이 통째로 오므로 어느 그룹이 바뀌었는지 직접 비교한다.
	NotifiedStates.SetNum(GroupStates.Num());

	for (int32 Index = 0; Index < GroupStates.Num(); ++Index)
	{
		if (NotifiedStates[Index] == GroupStates[Index])
		{
			continue;
		}

		const ECWTrafficSignalState OldState = NotifiedStates[Index];
		NotifiedStates[Index] = GroupStates[Index];

		const FName GroupName = Groups.IsValidIndex(Index) ? Groups[Index].GroupName : NAME_None;
		LogSyncSignalChange(GroupName, GroupStates[Index], OldState);

		OnGroupStateChanged.Broadcast(this, GroupName, GroupStates[Index], OldState);
	}
}

int32 ACWTrafficDirector::FindGroupIndex(FName GroupName) const
{
	for (int32 Index = 0; Index < Groups.Num(); ++Index)
	{
		if (Groups[Index].GroupName == GroupName)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

bool ACWTrafficDirector::DoGroupsConflict(const FCWTrafficSignalGroup& A, const FCWTrafficSignalGroup& B)
{
	// 방향을 안 적은 그룹은 검증에서 빠진다. 보행 그룹처럼 차선이 없는 경우다.
	for (ECWTrafficApproach ApproachA : A.Approaches)
	{
		for (ECWTrafficApproach ApproachB : B.Approaches)
		{
			if (DoApproachesConflict(ApproachA, A.Turn, ApproachB, B.Turn))
			{
				return true;
			}
		}
	}

	return false;
}

bool ACWTrafficDirector::DoApproachesConflict(ECWTrafficApproach ApproachA, ECWTrafficTurn TurnA, ECWTrafficApproach ApproachB, ECWTrafficTurn TurnB)
{
	// 우회전은 우측통행에서 양보 후 통행이므로 경로 검증 대상이 아니다.
	if (TurnA == ECWTrafficTurn::Right || TurnB == ECWTrafficTurn::Right)
	{
		return false;
	}

	// 같은 방향에서 들어온 차끼리는 교차하지 않는다. 나란히 나갈 뿐이다.
	if (ApproachA == ApproachB)
	{
		return false;
	}

	// 축이 다르면(남북 vs 동서) 무조건 교차한다.
	if (IsNorthSouthAxis(ApproachA) != IsNorthSouthAxis(ApproachB))
	{
		return true;
	}

	/**
	 * 여기부터는 마주보는 관계다(북 vs 남, 또는 동 vs 서).
	 *
	 *   직진 + 직진     : 나란히 지나간다. 교차 없음
	 *   좌회전 + 좌회전 : 서로 앞을 스치지 않는다. 교차 없음 (동시 보호 좌회전)
	 *   직진 + 좌회전   : 좌회전이 마주오는 직진의 앞을 가로지른다. 교차
	 */
	return TurnA != TurnB;
}

void ACWTrafficDirector::ValidateSignalPlan()
{
	RefreshPreview();

	for (const FString& Line : Preview)
	{
		UE_LOG(LogCWTraffic, Log, TEXT("[%s] %s"), *GetName(), *Line);
	}
}

void ACWTrafficDirector::WarnOnUngroupedStopLineLanes()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	/**
	 * 정지선이 담당하는데 어느 그룹에도 없는 차선을 찾는다.
	 *
	 * 그룹에 없는 차선은 ControlsLane 이 false 를 내서 정지선을 통째로 무시한다.
	 * 그 차선의 차들은 빨간불에도 그냥 지나가는데, 화면에서는 "저 줄만 안 선다" 로만
	 * 보이고 원인이 어디에도 표시되지 않는다.
	 *
	 * 정지선이 걸린 차선만 본다. 신호와 무관한 차선(인도 등)까지 잡으면 경고가
	 * 일상이 되어 아무도 안 읽게 된다 - 정지선을 놓았다는 것이 곧 "통제할 생각이었다"
	 * 는 표시이므로, 그 차선이 그룹에 없으면 확실한 실수다.
	 */
	for (TActorIterator<ACWTrafficStopLine> It(World); It; ++It)
	{
		const ACWTrafficStopLine* StopLine = *It;
		if (!StopLine)
		{
			continue;
		}

		for (const ACWTrafficLane* Lane : StopLine->GetLanes())
		{
			if (!Lane)
			{
				continue;
			}

			bool bInAnyGroup = false;
			for (const FCWTrafficSignalGroup& Group : Groups)
			{
				if (Group.Lanes.Contains(Lane))
				{
					bInAnyGroup = true;
					break;
				}
			}

			if (!bInAnyGroup)
			{
				Preview.Add(FString::Printf(
					TEXT("   <<< '%s' 는 %s 가 담당하는데 어느 그룹에도 없다! 그 차선은 빨간불을 무시하고 지나간다"),
					*Lane->GetName(), *StopLine->GetName()));
			}
		}
	}
}

void ACWTrafficDirector::RefreshPreview()
{
	Preview.Reset();

	if (Groups.Num() == 0)
	{
		Preview.Add(TEXT("<<< 신호 그룹이 없다. 아무 차선도 통제하지 않는다"));
		return;
	}

	// 그룹 요약. 차선이 0인데 방향이 적혀 있으면 배선을 빠뜨린 것이다.
	for (const FCWTrafficSignalGroup& Group : Groups)
	{
		int32 ValidLanes = 0;
		for (const TObjectPtr<ACWTrafficLane>& Lane : Group.Lanes)
		{
			if (Lane)
			{
				++ValidLanes;
			}
		}

		FString Line = FString::Printf(TEXT("그룹 '%s' : 차선 %d개 / 기본 %s"),
			*Group.GroupName.ToString(), ValidLanes, *GetSignalStateText(Group.DefaultState).ToString());

		/**
		 * 빈 칸이 있으면 반드시 알린다.
		 *
		 * 차선 액터를 지웠다 다시 만들면 여기 참조가 끊기는데, 배열 길이는 그대로라
		 * 개수만 봐서는 원래 그런 건지 날아간 건지 구분이 안 된다. 그리고 그룹에 없는
		 * 차선은 ControlsLane 이 false 를 내서 정지선을 통째로 무시하고 지나간다 -
		 * 화면에서는 "저 줄만 안 선다" 로만 보이고 원인이 안 보인다. 실제로 그렇게 헤맸다.
		 */
		const int32 EmptySlots = Group.Lanes.Num() - ValidLanes;
		if (EmptySlots > 0)
		{
			Line += FString::Printf(TEXT("   <<< %d칸 중 %d칸이 비어 있다! 참조가 끊겼다"), Group.Lanes.Num(), EmptySlots);
		}
		else if (ValidLanes == 0 && Group.Approaches.Num() > 0)
		{
			Line += TEXT("   <<< 방향은 적혀 있는데 차선이 없다");
		}

		Preview.Add(Line);
	}

	WarnOnUngroupedStopLineLanes();

	if (Sequence.Num() == 0)
	{
		Preview.Add(TEXT("시퀀스 없음 - 신호가 돌지 않고 그룹 기본색이 유지된다"));
	}

	// 단계별 요약과 경로 교차 검사.
	float CycleDuration = 0.f;
	for (int32 StepIndex = 0; StepIndex < Sequence.Num(); ++StepIndex)
	{
		const FCWTrafficSignalStep& Step = Sequence[StepIndex];
		CycleDuration += Step.Duration + Step.ClearanceDuration;

		FString GreenNames;
		TArray<int32> GreenGroups;

		for (const FCWTrafficSignalAssignment& Assignment : Step.Assignments)
		{
			const int32 GroupIndex = FindGroupIndex(Assignment.GroupName);
			if (GroupIndex == INDEX_NONE)
			{
				Preview.Add(FString::Printf(TEXT("단계 %d(%s) : '%s' 그룹이 없다   <<< 이름 오타"),
					StepIndex, *Step.StepName.ToString(), *Assignment.GroupName.ToString()));
				continue;
			}

			if (Assignment.State != ECWTrafficSignalState::Green)
			{
				continue;
			}

			GreenGroups.Add(GroupIndex);
			GreenNames += GreenNames.IsEmpty() ? TEXT("") : TEXT(", ");
			GreenNames += Assignment.GroupName.ToString();
		}

		Preview.Add(FString::Printf(TEXT("단계 %d(%s) : 초록 [%s] / %.1f초 + 전환 %.1f초"),
			StepIndex, *Step.StepName.ToString(),
			GreenNames.IsEmpty() ? TEXT("없음") : *GreenNames, Step.Duration, Step.ClearanceDuration));

		for (int32 A = 0; A < GreenGroups.Num(); ++A)
		{
			for (int32 B = A + 1; B < GreenGroups.Num(); ++B)
			{
				if (!DoGroupsConflict(Groups[GreenGroups[A]], Groups[GreenGroups[B]]))
				{
					continue;
				}

				Preview.Add(FString::Printf(
					TEXT("   <<< '%s' 와 '%s' 는 경로가 교차한다. 같이 초록이면 차가 교차로에서 서로를 통과한다"),
					*Groups[GreenGroups[A]].GroupName.ToString(), *Groups[GreenGroups[B]].GroupName.ToString()));
			}
		}

		if (Sequence.Num() > 1 && Step.ClearanceDuration <= KINDA_SMALL_NUMBER)
		{
			Preview.Add(TEXT("   <<< 전환 시간이 0이라 앞 단계의 마지막 차가 교차로에 남은 채 다음 단계가 시작된다"));
		}
	}

	if (Sequence.Num() > 1)
	{
		Preview.Add(FString::Printf(TEXT("주기 %.1f초"), CycleDuration));
	}

	for (const FCWTrafficSignalRule& Rule : Rules)
	{
		FString TriggerText;
		switch (Rule.Trigger)
		{
		case ECWTrafficSignalTrigger::StageCleared:
			TriggerText = TEXT("스테이지 클리어");
			break;

		case ECWTrafficSignalTrigger::StageProgress:
			TriggerText = FString::Printf(TEXT("진행도 %.0f%% 도달"), Rule.ProgressThreshold * 100.f);
			break;

		default:
			TriggerText = TEXT("직접 호출");
			break;
		}

		Preview.Add(FString::Printf(TEXT("규칙 '%s' : %s%s%s"),
			*Rule.RuleName.ToString(), *TriggerText,
			Rule.Delay > 0.f ? *FString::Printf(TEXT(" / %.1f초 뒤"), Rule.Delay) : TEXT(""),
			Rule.bStopSequence ? TEXT(" / 시퀀스 정지") : TEXT("")));
	}
}

#if WITH_EDITOR
void ACWTrafficDirector::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	RefreshPreview();
}
#endif
