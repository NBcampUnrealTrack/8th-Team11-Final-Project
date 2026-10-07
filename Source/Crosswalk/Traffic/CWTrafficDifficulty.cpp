// CWTrafficDifficulty.cpp

#include "CWTrafficDifficulty.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "CWBaseVehicle.h"
#include "CWTrafficVehicleSpawner.h"
#include "Core/CWGameState.h"
#include "Traffic/CWTrafficLog.h"

ACWTrafficDifficulty::ACWTrafficDifficulty()
{
	PrimaryActorTick.bCanEverTick = false;

	// 스폰은 서버 단독이고 이 액터는 스포너 값만 바꾼다. 클라가 알 필요가 없다.
	bReplicates = false;
}

int32 ACWTrafficDifficulty::FindStepForProgress(float Progress) const
{
	int32 BestIndex = INDEX_NONE;
	float BestStart = -1.f;

	for (int32 Index = 0; Index < Steps.Num(); ++Index)
	{
		const FCWTrafficDifficultyStep& Step = Steps[Index];
		if (Step.StartProgress > Progress)
		{
			continue;
		}

		// 조건을 만족하는 것 중 가장 늦게 시작하는 단계가 이긴다. 배열 순서를 안 따진다.
		if (Step.StartProgress > BestStart)
		{
			BestStart = Step.StartProgress;
			BestIndex = Index;
		}
	}

	return BestIndex;
}

void ACWTrafficDifficulty::RefreshNow()
{
	RefreshPreview();

	if (!HasAuthority())
	{
		return;
	}

	const ACWGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACWGameState>() : nullptr;
	const float Progress = GameState ? GameState->GetStageProgress() : 0.f;

	ApplyStep(FindStepForProgress(Progress));
}

void ACWTrafficDifficulty::BeginPlay()
{
	Super::BeginPlay();

	if (Steps.Num() == 0)
	{
		UE_LOG(LogCWTraffic, Warning, TEXT("[%s] Steps 가 비어 있어 난이도가 움직이지 않는다. 스포너 자기 값이 그대로 쓰인다."), *GetName());
		return;
	}

	// 값을 바꾸는 쪽은 서버뿐이다. 클라에서 돌면 스포너 값만 갈리고 스폰은 안 바뀐다.
	if (!HasAuthority())
	{
		return;
	}

	BindStageProgress();

	// 진행도 0 의 단계를 시작 시점에 한 번 밀어 넣는다. 첫 목표를 달성하기 전까지는
	// 알림이 오지 않으므로, 이것을 안 하면 그동안 스포너 기본값으로 굴러간다.
	ApplyStep(FindStepForProgress(0.f));
}

void ACWTrafficDifficulty::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACWGameState* GameState = BoundGameState.Get())
	{
		GameState->OnStageProgressChanged.RemoveDynamic(this, &ACWTrafficDifficulty::HandleStageProgressChanged);
	}
	BoundGameState.Reset();

	Super::EndPlay(EndPlayReason);
}

void ACWTrafficDifficulty::PostLoad()
{
	Super::PostLoad();

	// 미리보기는 Transient 라 저장되지 않는다. 레벨을 열 때 다시 만든다.
	RefreshPreview();
}

void ACWTrafficDifficulty::HandleStageProgressChanged(ACWGameState* GameState, int32 CompletedCount, int32 TotalCount)
{
	if (!GameState)
	{
		return;
	}

	ApplyStep(FindStepForProgress(GameState->GetStageProgress()));
}

void ACWTrafficDifficulty::ApplyStep(int32 StepIndex)
{
	if (!Steps.IsValidIndex(StepIndex) || StepIndex == CurrentStepIndex)
	{
		return;
	}

	const FCWTrafficDifficultyStep& Step = Steps[StepIndex];
	const TArray<ACWTrafficVehicleSpawner*> Targets = ResolveSpawners();

	if (Targets.Num() == 0)
	{
		UE_LOG(LogCWTraffic, Warning, TEXT("[%s] 대상 스포너를 찾지 못해 단계 '%s' 를 적용하지 못했다."), *GetName(), *Step.StepName.ToString());
		return;
	}

	// 적용에 성공한 뒤에 기록한다. 먼저 기록하면 실패한 단계를 다음 진행도 알림에서 다시 시도하지 않는다.
	CurrentStepIndex = StepIndex;

	for (ACWTrafficVehicleSpawner* Spawner : Targets)
	{
		Spawner->SetSpawnInterval(Step.SpawnIntervalMin, Step.SpawnIntervalMax);

		// 비어 있으면 건드리지 않는다. 차선마다 다른 차종을 굴리는 배치를 지우지 않기 위해서다.
		if (Step.VehicleMix.Num() > 0)
		{
			/**
			 * 덮어쓴다는 사실을 남긴다.
			 *
			 * 스포너 인스턴스에 차종을 넣어 두고 왜 그 차가 안 나오는지 찾는 일이 실제로 있었다.
			 * 레벨 배치만 보면 스포너에 값이 멀쩡히 들어 있어서, 여기가 조용하면 원인을
			 * 짚을 방법이 없다. 배치를 지우는 쪽이 로그를 남겨야 한다.
			 */
			UE_LOG(LogCWTraffic, Log, TEXT("[%s] %s 의 VehicleMix 를 단계 '%s' 값(%d종)으로 덮어쓴다. 스포너에 배치한 차종 목록은 무시된다."),
				*GetName(), *Spawner->GetName(), *Step.StepName.ToString(), Step.VehicleMix.Num());

			Spawner->SetVehicleMix(Step.VehicleMix);
		}
	}

	UE_LOG(LogCWTraffic, Log, TEXT("[%s] 난이도 단계 %d(%s) / 진행도 %.0f%% 부터 / 주기 %.1f~%.1f초 / 스포너 %d개"),
		*GetName(), StepIndex, *Step.StepName.ToString(), Step.StartProgress * 100.f,
		Step.SpawnIntervalMin, Step.SpawnIntervalMax, Targets.Num());
}

TArray<ACWTrafficVehicleSpawner*> ACWTrafficDifficulty::ResolveSpawners() const
{
	TArray<ACWTrafficVehicleSpawner*> Result;

	// 지정한 것이 있으면 그것만. 일부 차선만 따로 굴리는 배치다.
	if (Spawners.Num() > 0)
	{
		for (const TObjectPtr<ACWTrafficVehicleSpawner>& Entry : Spawners)
		{
			if (Entry)
			{
				Result.Add(Entry);
			}
		}

		return Result;
	}

	// 비어 있으면 판 전체다. 차선을 새로 그어도 배선을 다시 안 해도 되게 하기 위해서다.
	UWorld* World = GetWorld();
	if (!World)
	{
		return Result;
	}

	for (TActorIterator<ACWTrafficVehicleSpawner> It(World); It; ++It)
	{
		Result.Add(*It);
	}

	return Result;
}

void ACWTrafficDifficulty::BindStageProgress()
{
	ACWGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACWGameState>() : nullptr;
	if (!GameState)
	{
		UE_LOG(LogCWTraffic, Warning, TEXT("[%s] CWGameState 를 찾지 못해 진행도를 따라가지 못한다. 첫 단계로 고정된다."), *GetName());
		return;
	}

	GameState->OnStageProgressChanged.AddDynamic(this, &ACWTrafficDifficulty::HandleStageProgressChanged);
	BoundGameState = GameState;
}

float ACWTrafficDifficulty::GetLongestPassTime(const FCWTrafficDifficultyStep& Step) const
{
	float LongestLength = 0.f;
	float BaseSpeed = 0.f;

	for (const FCWTrafficVehicleEntry& Entry : Step.VehicleMix)
	{
		if (!Entry.VehicleClass || Entry.Weight <= 0.f)
		{
			continue;
		}

		// CDO 에서 읽으면 차종 BP 가 정한 콜리전 크기와 속도가 그대로 반영된다.
		const ACWBaseVehicle* VehicleTemplate = Entry.VehicleClass->GetDefaultObject<ACWBaseVehicle>();
		if (!VehicleTemplate)
		{
			continue;
		}

		const float Length = VehicleTemplate->GetVehicleLength();
		if (Length > LongestLength)
		{
			LongestLength = Length;
			BaseSpeed = VehicleTemplate->GetBaseSpeed();
		}
	}

	return (BaseSpeed > 0.f) ? (LongestLength / BaseSpeed) : 0.f;
}

void ACWTrafficDifficulty::RefreshPreview()
{
	Preview.Reset();

	if (Steps.Num() == 0)
	{
		Preview.Add(TEXT("(단계가 없음 - 스포너 자기 값이 그대로 쓰인다)"));
		return;
	}

	// 다음 단계가 시작되는 지점이 이 단계의 끝이다. 배열 순서를 안 믿으므로 매번 찾는다.
	for (int32 Index = 0; Index < Steps.Num(); ++Index)
	{
		const FCWTrafficDifficultyStep& Step = Steps[Index];

		float NextStart = 1.f;
		for (const FCWTrafficDifficultyStep& Other : Steps)
		{
			if (Other.StartProgress > Step.StartProgress && Other.StartProgress < NextStart)
			{
				NextStart = Other.StartProgress;
			}
		}

		FString Line = FString::Printf(TEXT("[%d] %-8s 진행도 %3.0f~%3.0f%%   주기 %.1f~%.1f초"),
			Index, *Step.StepName.ToString(), Step.StartProgress * 100.f, NextStart * 100.f,
			Step.SpawnIntervalMin, Step.SpawnIntervalMax);

		const float PassTime = GetLongestPassTime(Step);
		if (PassTime > 0.f)
		{
			// 틈 = 주기에서 차 한 대가 지나가는 시간을 뺀 것. 이것이 그 차선에서 일할 수 있는 시간이다.
			Line += FString::Printf(TEXT("   틈 %.1f~%.1f초"),
				Step.SpawnIntervalMin - PassTime, Step.SpawnIntervalMax - PassTime);

			/**
			 * 하한을 넘으면 밀도 레버가 반응 레버로 변질된다.
			 *
			 * 앞차가 지나가는 데 PassTime 이 걸리는데 그 사이 다음 차가 이미 시야에 들어와
			 * 있으면, 플레이어에게는 "차가 끊이지 않는다" 로 보인다. 그 상태에서는 밀도를
			 * 더 올려도 어려워지는 게 아니라 피할 방법이 없어질 뿐이다.
			 */
			const float Floor = PassTime + MinReactionTime;
			if (Step.SpawnIntervalMin < Floor)
			{
				Line += FString::Printf(TEXT("   <<< 주기 하한 %.1f초 아래! 피할 틈이 사라진다"), Floor);
			}
		}
		else
		{
			Line += TEXT("   (차종 구성을 넣으면 틈이 계산된다)");
		}

		Preview.Add(Line);
	}

	if (FindStepForProgress(0.f) == INDEX_NONE)
	{
		Preview.Add(TEXT("<<< 진행도 0 을 담당하는 단계가 없다! 시작 구간이 스포너 기본값으로 굴러간다"));
	}
}

#if WITH_EDITOR
void ACWTrafficDifficulty::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	RefreshPreview();
}
#endif
