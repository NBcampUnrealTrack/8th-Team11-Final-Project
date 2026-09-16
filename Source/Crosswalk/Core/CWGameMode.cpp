#include "Core/CWGameMode.h"

#include "TimerManager.h"
#include "Characters/CWPlayerCharacter.h"
#include "Characters/CWPlayerController.h"
#include "Core/CWGameState.h"
#include "Engine/World.h"
#include "Painting/CWPaintingSubsystem.h"
#include "Painting/Zone/CWBaseWorkZone.h"

namespace
{
	/**
	 * 이름이 같은 줄을 찾아 돌려주고, 없으면 새로 만든다. (기획서 3.6 체크리스트 묶기)
	 * 존이 내는 줄과 존이 추가로 내는 줄이 같은 규칙으로 묶이도록 한 곳에 모아 뒀다.
	 */
	FCWStageObjective& FindOrAddObjective(TArray<FCWStageObjective>& Objectives, const FText& DisplayName, int32 WorkOrder)
	{
		FCWStageObjective* Found = Objectives.FindByPredicate(
			[&DisplayName](const FCWStageObjective& Candidate)
			{
				return Candidate.DisplayName.EqualTo(DisplayName);
			});

		if (Found != nullptr)
		{
			// 같은 묶음에서 가장 이른 순서를 대표값으로 쓴다. 체크리스트가 작업 순서대로 선다.
			Found->WorkOrder = FMath::Min(Found->WorkOrder, WorkOrder);
			return *Found;
		}

		FCWStageObjective& Added = Objectives.AddDefaulted_GetRef();
		Added.DisplayName = DisplayName;
		Added.WorkOrder = WorkOrder;

		return Added;
	}
}

ACWGameMode::ACWGameMode()
{
	DefaultPawnClass = ACWPlayerCharacter::StaticClass();
	PlayerControllerClass = ACWPlayerController::StaticClass();
	GameStateClass = ACWGameState::StaticClass();
}

void ACWGameMode::BeginPlay()
{
	Super::BeginPlay();

	UCWPaintingSubsystem* PaintingSubsystem = UCWPaintingSubsystem::Get(this);
	if (PaintingSubsystem == nullptr)
	{
		return;
	}

	// 한 번 모으고 끝내지 않고 등록을 계속 지켜본다.
	// 시작할 때만 모으면 레벨 스트리밍이나 런타임 스폰으로 늦게 들어온 존이 체크리스트에
	// 영영 안 잡힌다. 존이 스스로 등록할 때 알려주므로 들어오고 나가는 것을 모두 따라간다.
	PaintingSubsystem->OnWorkZoneRegistered.AddDynamic(this, &ACWGameMode::HandleZoneRegistered);
	PaintingSubsystem->OnWorkZoneUnregistered.AddDynamic(this, &ACWGameMode::HandleZoneUnregistered);

	// 액터의 BeginPlay 순서는 보장되지 않아서 게임모드보다 먼저 시작한 존이 있을 수 있다.
	// 그 존들은 등록 이벤트를 이미 흘려보냈으므로 지금 목록에서 직접 가져온다.
	for (const TWeakObjectPtr<ACWBaseWorkZone>& WeakZone : PaintingSubsystem->GetZones())
	{
		HandleZoneRegistered(WeakZone.Get());
	}
}

bool ACWGameMode::ReturnToRoom()
{
	if (!HasAuthority())
	{
		return false;
	}

	UWorld* World = GetWorld();

	if (!IsValid(World))
	{
		return false;
	}

	if (RoomMap.IsNull())
	{
		UE_LOG(LogTemp, Error,
			TEXT("GameMode: RoomMap is not configured."));

		return false;
	}

	const FString RoomMapPath =
		RoomMap
		.ToSoftObjectPath()
		.GetLongPackageName();

	if (RoomMapPath.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("GameMode: RoomMap path is empty."));

		return false;
	}

	// 이미 다른 맵으로 이동 중이면 중복 요청 무시
	if (!World->NextURL.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("GameMode: travel is already pending."));

		return false;
	}

	// 현재 연결된 플레이어 전원을 방 대기실로 이동
	const bool bTravelStarted =
		World->ServerTravel(RoomMapPath);

	if (!bTravelStarted)
	{
		UE_LOG(LogTemp, Error,
			TEXT("GameMode: failed to start ServerTravel. Map=%s"),
			*RoomMapPath);

		return false;
	}

	UE_LOG(LogTemp, Log,
		TEXT("GameMode: ServerTravel started. Map=%s"),
		*RoomMapPath);

	return true;
}

#pragma region StageSelectionValidation

bool ACWGameMode::CanOpenStageSelection() const
{
	const ACWGameState* CWGameState =
		GetGameState<ACWGameState>();

	if (!IsValid(CWGameState))
	{
		return false;
	}

	// Result가 표시된 클리어 상태에서만 선택창을 염
	return CWGameState->IsStageCleared();
}

bool ACWGameMode::CanStartSelectedStage() const
{
	const ACWGameState* CWGameState =
		GetGameState<ACWGameState>();

	if (!IsValid(CWGameState))
	{
		return false;
	}

	// 선택창이 열린 사이 상태가 바뀌었을 경우를 방어함
	return CWGameState->IsStageCleared();
}

#pragma endregion

void ACWGameMode::HandleZoneRegistered(ACWBaseWorkZone* Zone)
{
	if (Zone == nullptr || WorkZones.Contains(Zone))
	{
		return;
	}

	WorkZones.Add(Zone);

	// 완료 델리게이트가 아니라 진행도 델리게이트에 붙는다.
	// 완료가 풀리는 순간에는 OnWorkCompleted 가 발행되지 않아서 그것만 봐서는 되돌림을 놓친다.
	Zone->OnWorkProgress.AddDynamic(this, &ACWGameMode::HandleZoneProgress);

	ScheduleStageProgressRefresh();
}

void ACWGameMode::HandleZoneUnregistered(ACWBaseWorkZone* Zone)
{
	if (Zone == nullptr || WorkZones.Remove(Zone) == 0)
	{
		return;
	}

	Zone->OnWorkProgress.RemoveDynamic(this, &ACWGameMode::HandleZoneProgress);

	ScheduleStageProgressRefresh();
}

void ACWGameMode::ScheduleStageProgressRefresh()
{
	// 집계를 다음 틱으로 미룬다.
	//
	// 두 가지를 한 번에 해결한다.
	// 하나, 존은 BeginPlay 맨 앞에서 등록하므로 이 시점에는 시작 진행도가 아직 없다.
	//       (칸 존의 BuildGrid 가 등록 뒤에 돈다) 한 틱 미루면 확정된 값을 본다.
	// 둘, 존이 여러 개 한꺼번에 등록돼도 집계는 한 번만 돈다.
	if (bStageProgressRefreshScheduled)
	{
		return;
	}

	bStageProgressRefreshScheduled = true;

	GetWorldTimerManager().SetTimerForNextTick(this, &ACWGameMode::FlushStageProgressRefresh);
}

void ACWGameMode::FlushStageProgressRefresh()
{
	bStageProgressRefreshScheduled = false;

	/**
	 * 집계 자체는 찍지 않는다. 붓질 한 번이 곧 집계 한 번이라 한 판에 3000줄이 넘게 쌓였고
	 * (09-16 로그에서 3122줄), 내용은 "존 29개" 가 계속 반복될 뿐이라 읽을 값이 없다.
	 * 실제로 볼 값인 "목표가 몇 줄이고 몇 개 달성됐나" 는 RefreshStageProgress 가
	 * 구성이 바뀔 때만 찍는다 — 그쪽은 같은 판에서 18줄이었다.
	 */
	UE_LOG(LogTemp, Verbose, TEXT("[Stage] 작업 존 %d개를 집계합니다."), WorkZones.Num());

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

	// 존이 추가로 내는 줄을 받는 임시 버퍼. 반복문 밖에 둬서 매번 새로 할당하지 않는다.
	TArray<FCWStageObjective> ExtraObjectives;

	for (const TObjectPtr<ACWBaseWorkZone>& Zone : WorkZones)
	{
		if (Zone == nullptr)
		{
			continue;
		}

		const bool bZoneCompleted = Zone->IsCompleted();

		++TotalCount;
		CompletedCount += bZoneCompleted ? 1 : 0;

		FCWStageObjective& Objective = FindOrAddObjective(Objectives, Zone->GetObjectiveName(), Zone->GetWorkOrder());
		Objective.TotalCount += 1;
		Objective.CompletedCount += bZoneCompleted ? 1 : 0;

		// 한 존이 두 가지 일을 겸하면 줄을 더 낸다.
		// 도색 존은 Area 안쪽으로 "도색 작업" 을, 바깥 고리로 "페인트 정리" 를 동시에 들고 있다.
		// 삐져나온 페인트는 도색 존의 격자 안에 있어서 별도 존으로 쪼갤 수가 없다.
		ExtraObjectives.Reset();
		Zone->GetExtraObjectives(ExtraObjectives);

		for (const FCWStageObjective& Extra : ExtraObjectives)
		{
			FCWStageObjective& Merged = FindOrAddObjective(Objectives, Extra.DisplayName, Extra.WorkOrder);
			Merged.TotalCount += Extra.TotalCount;
			Merged.CompletedCount += Extra.CompletedCount;

			// 클리어 판정에도 들어간다. 체크리스트 한 줄이 곧 작업 목표다. (기획서 3.5, 3.6)
			TotalCount += Extra.TotalCount;
			CompletedCount += Extra.CompletedCount;
		}
	}

	Objectives.Sort(
		[](const FCWStageObjective& A, const FCWStageObjective& B)
		{
			if (A.WorkOrder != B.WorkOrder)
			{
				return A.WorkOrder < B.WorkOrder;
			}

			// 순서가 같을 때 기준이 없으면 줄 순서가 갱신할 때마다 뒤바뀐다.
			// TArray::Sort 는 안정 정렬이 아니라서 같은 값끼리의 순서를 보장하지 않는다.
			return A.DisplayName.CompareTo(B.DisplayName) < 0;
		});

	CrosswalkGameState->SetObjectiveCounts(CompletedCount, TotalCount);

	// 목표 구성이나 달성 수가 바뀔 때마다 현황을 찍는다.
	// 상태 변경 때만 찍으면 시작 시점(이미 InProgress)에는 아무것도 나오지 않아
	// 존 설정이 맞는지 확인할 방법이 없다.
	if (CrosswalkGameState->SetObjectives(Objectives))
	{
		UE_LOG(LogTemp, Log, TEXT("[Stage] 목표 %d줄 (%d/%d 달성)"), Objectives.Num(), CompletedCount, TotalCount);

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
	// 바로 집계하지 않고 다음 틱으로 모은다.
	//
	// 이 델리게이트는 진행도가 바뀔 때마다 온다. 칸을 칠하는 존은 붓질 한 번이 곧 진행도
	// 변화라서, 여러 명이 칠하는 동안에는 프레임마다 여러 번 들어온다. 그때마다 전체 존을
	// 훑고 FText 를 비교하고 정렬하면 그게 그대로 서버 비용이 된다.
	//
	// 한 프레임 안에서 몇 번을 계산하든 마지막 값 하나만 의미가 있다.
	ScheduleStageProgressRefresh();
}
