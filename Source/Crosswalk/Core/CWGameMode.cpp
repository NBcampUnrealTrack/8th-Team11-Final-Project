#include "Core/CWGameMode.h"

#include "EngineUtils.h"
#include "TimerManager.h"
#include "Characters/CWPlayerCharacter.h"
#include "Characters/CWPlayerController.h"
#include "Core/CWGameState.h"
#include "Painting/Zone/CWBaseWorkZone.h"

ACWGameMode::ACWGameMode()
{
	DefaultPawnClass = ACWPlayerCharacter::StaticClass();
	PlayerControllerClass = ACWPlayerController::StaticClass();
	GameStateClass = ACWGameState::StaticClass();
}

void ACWGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 존 수집을 다음 틱으로 미룬다.
	// 액터들의 BeginPlay 순서는 보장되지 않아서, 여기서 바로 모으면 아직 시작 진행도를
	// 세팅하지 않은 존을 잡을 수 있다.
	GetWorldTimerManager().SetTimerForNextTick(this, &ACWGameMode::CollectWorkZones);
}

void ACWGameMode::CollectWorkZones()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	WorkZones.Reset();

	for (TActorIterator<ACWBaseWorkZone> It(World); It; ++It)
	{
		ACWBaseWorkZone* Zone = *It;
		if (Zone == nullptr)
		{
			continue;
		}

		WorkZones.Add(Zone);

		// 완료 델리게이트가 아니라 진행도 델리게이트에 붙는다.
		// 완료가 풀리는 순간에는 OnWorkCompleted 가 발행되지 않아서 그것만 봐서는 되돌림을 놓친다.
		Zone->OnWorkProgress.AddDynamic(this, &ACWGameMode::HandleZoneProgress);
	}

	UE_LOG(LogTemp, Log, TEXT("[Stage] 작업 존 %d개를 찾았습니다."), WorkZones.Num());

	RefreshStageProgress();
}

void ACWGameMode::RefreshStageProgress()
{
	ACWGameState* CrosswalkGameState = GetGameState<ACWGameState>();
	if (CrosswalkGameState == nullptr)
	{
		return;
	}

	int32 CompletedCount = 0;
	int32 TotalCount = 0;

	// 이름이 같은 존은 체크리스트에서 한 줄로 묶는다. (기획서 3.6)
	// 횡단보도 줄무늬처럼 같은 작업을 여러 존으로 쪼개 놓으면 존 개수만큼 줄이 늘어난다.
	TArray<FCWStageObjective> Objectives;

	for (const TObjectPtr<ACWBaseWorkZone>& Zone : WorkZones)
	{
		if (Zone == nullptr)
		{
			continue;
		}

		++TotalCount;

		const bool bZoneCompleted = Zone->IsCompleted();
		if (bZoneCompleted)
		{
			++CompletedCount;
		}

		const FText ObjectiveName = Zone->GetObjectiveName();

		FCWStageObjective* Objective = Objectives.FindByPredicate(
			[&ObjectiveName](const FCWStageObjective& Candidate)
			{
				//return Candidate.DisplayName.IdenticalTo(ObjectiveName);
				return Candidate.DisplayName.EqualTo(ObjectiveName);
			});

		if (Objective == nullptr)
		{
			Objective = &Objectives.AddDefaulted_GetRef();
			Objective->DisplayName = ObjectiveName;
			Objective->WorkOrder = Zone->GetWorkOrder();
		}

		// 같은 묶음에서 가장 이른 순서를 대표값으로 쓴다. 체크리스트가 작업 순서대로 선다.
		Objective->WorkOrder = FMath::Min(Objective->WorkOrder, Zone->GetWorkOrder());
		Objective->TotalCount += 1;
		Objective->CompletedCount += bZoneCompleted ? 1 : 0;
	}

	Objectives.Sort(
		[](const FCWStageObjective& A, const FCWStageObjective& B)
		{
			return A.WorkOrder < B.WorkOrder;
		});

	CrosswalkGameState->SetObjectiveCounts(CompletedCount, TotalCount);

	// 목표 구성이나 달성 수가 바뀔 때마다 현황을 찍는다.
	// 상태 변경 때만 찍으면 시작 시점(이미 InProgress)에는 아무것도 나오지 않아
	// 존 설정이 맞는지 확인할 방법이 없다.
	if (CrosswalkGameState->SetObjectives(Objectives))
	{
		UE_LOG(LogTemp, Log, TEXT("[Stage] 목표 %d줄 (존 %d/%d)"), Objectives.Num(), CompletedCount, TotalCount);

		for (const FCWStageObjective& Objective : Objectives)
		{
			UE_LOG(LogTemp, Log, TEXT("[Stage]   [%s] %s (%d/%d)"),
				Objective.IsCompleted() ? TEXT("O") : TEXT(" "),
				*Objective.DisplayName.ToString(), Objective.CompletedCount, Objective.TotalCount);
		}
	}

	// 존이 하나도 없는 맵에서 시작하자마자 클리어가 뜨지 않게 막는다.
	// 트래픽 테스트 맵처럼 작업 존을 두지 않는 레벨이 실제로 있다.
	if (TotalCount <= 0)
	{
		return;
	}

	const bool bAllCompleted = (CompletedCount >= TotalCount);
	const ECWStageState NewState = bAllCompleted ? ECWStageState::Cleared : ECWStageState::InProgress;

	// 되돌아가는 전이도 허용한다. 끝났던 존이 다시 열리면 스테이지도 진행 중으로 돌아가야 한다.
	if (CrosswalkGameState->GetStageState() != NewState)
	{
		CrosswalkGameState->SetStageState(NewState);

		UE_LOG(LogTemp, Log, TEXT("[Stage] 상태 변경: %s (존 %d/%d)"),
			bAllCompleted ? TEXT("클리어") : TEXT("진행 중"), CompletedCount, TotalCount);
	}
}

void ACWGameMode::HandleZoneProgress(ACWBaseWorkZone* /*Zone*/, float /*Progress*/)
{
	RefreshStageProgress();
}
