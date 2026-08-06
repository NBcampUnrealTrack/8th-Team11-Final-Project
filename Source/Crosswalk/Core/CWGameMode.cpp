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

	UE_LOG(LogTemp, Log, TEXT("[Stage] 작업 목표 %d개를 찾았습니다."), WorkZones.Num());

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

	for (const TObjectPtr<ACWBaseWorkZone>& Zone : WorkZones)
	{
		if (Zone == nullptr)
		{
			continue;
		}

		++TotalCount;

		if (Zone->IsCompleted())
		{
			++CompletedCount;
		}
	}

	CrosswalkGameState->SetObjectiveCounts(CompletedCount, TotalCount);

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

		UE_LOG(LogTemp, Log, TEXT("[Stage] 상태 변경: %s (%d/%d)"),
			bAllCompleted ? TEXT("클리어") : TEXT("진행 중"), CompletedCount, TotalCount);
	}
}

void ACWGameMode::HandleZoneProgress(ACWBaseWorkZone* /*Zone*/, float /*Progress*/)
{
	RefreshStageProgress();
}
