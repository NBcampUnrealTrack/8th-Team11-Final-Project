#include "Painting/Zone/CWBaseWorkZone.h"

#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

ACWBaseWorkZone::ACWBaseWorkZone()
{
	PrimaryActorTick.bCanEverTick = false;

	// 진행도와 완료는 팀 전원이 같은 값을 봐야 한다.
	bReplicates = true;
	// 존은 제자리에 고정이다. 위치를 전파할 이유가 없다.
	SetReplicateMovement(false);

	Area = CreateDefaultSubobject<UBoxComponent>(TEXT("Area"));
	SetRootComponent(Area);
	Area->SetBoxExtent(FVector(200.f, 100.f, 20.f));

	// 커서 트레이스에만 걸리고, 캐릭터나 차량 이동은 막지 않는다.
	Area->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Area->SetCollisionResponseToAllChannels(ECR_Ignore);
	Area->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

float ACWBaseWorkZone::GetSquaredDistanceToPoint(const FVector& Point) const
{
	return Area->Bounds.GetBox().ComputeSquaredDistanceToPoint(Point);
}

void ACWBaseWorkZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACWBaseWorkZone, Progress);
	DOREPLIFETIME(ACWBaseWorkZone, bCompleted);
	DOREPLIFETIME(ACWBaseWorkZone, bLocked);
}

void ACWBaseWorkZone::BeginPlay()
{
	Super::BeginPlay();

	// 잠금 판정은 서버만 한다. bLocked 는 리플리케이트되므로 클라이언트는 결과만 받는다.
	if (!HasAuthority())
	{
		return;
	}

	CollectWatchedZones();

	for (ACWBaseWorkZone* Zone : WatchedZones)
	{
		Zone->OnWorkProgress.AddDynamic(this, &ACWBaseWorkZone::HandleWatchedZoneProgress);
	}

	// 기다릴 존이 하나라도 있으면 일단 잠가둔다.
	// 이 시점에는 앞 존의 BeginPlay 가 아직 안 돌았을 수 있어서 진행도를 믿을 수 없다.
	bLocked = (WatchedZones.Num() > 0);

	// 전원의 BeginPlay 가 끝난 뒤에 제대로 판정한다.
	// 존의 시작 진행도는 각자의 BeginPlay 에서 정해지는데(칸 존의 BuildGrid) 액터의
	// BeginPlay 순서는 보장되지 않는다. 한 틱 미루면 순서를 신경 쓸 필요가 없다.
	GetWorldTimerManager().SetTimerForNextTick(this, &ACWBaseWorkZone::RefreshLock);
}

void ACWBaseWorkZone::CollectWatchedZones()
{
	WatchedZones.Reset();

	// WorkOrder 로 표현 못 하는 예외 의존. 순서와 무관하게 항상 기다린다.
	if (PrerequisiteZone != nullptr && PrerequisiteZone != this)
	{
		WatchedZones.Add(PrerequisiteZone);
	}

	// 0 이면 아무것도 기다리지 않는다. 순서를 안 쓰는 존은 여기서 끝난다.
	if (WorkOrder <= 0)
	{
		return;
	}

	// 레벨의 존은 많아야 5~6개라 전부 훑어도 부담이 없다.
	for (TActorIterator<ACWBaseWorkZone> ZoneIter(GetWorld()); ZoneIter; ++ZoneIter)
	{
		ACWBaseWorkZone* Zone = *ZoneIter;
		if (Zone == nullptr || Zone == this || Zone->WorkGroup != WorkGroup)
		{
			continue;
		}

		// 나보다 앞 순서만. 같은 값을 준 존들은 서로를 기다리지 않고 동시에 열린다.
		if (Zone->WorkOrder <= 0 || Zone->WorkOrder >= WorkOrder)
		{
			continue;
		}

		WatchedZones.AddUnique(Zone);
	}
}

void ACWBaseWorkZone::RefreshLock()
{
	if (!HasAuthority())
	{
		return;
	}

	const bool bNewLocked = ShouldBeLocked();
	if (bLocked == bNewLocked)
	{
		return;
	}

	bLocked = bNewLocked;

	// 잠금이 바뀌면 HUD 체크리스트의 "지금 할 수 있는 작업" 표시도 따라와야 한다.
	OnWorkProgress.Broadcast(this, Progress);

	ShowDebugProgress();
}

bool ACWBaseWorkZone::ShouldBeLocked() const
{
	for (const ACWBaseWorkZone* Zone : WatchedZones)
	{
		if (Zone != nullptr && !Zone->IsCompleted())
		{
			return true;
		}
	}

	return false;
}

void ACWBaseWorkZone::HandleWatchedZoneProgress(ACWBaseWorkZone* Zone, float InProgress)
{
	RefreshLock();
}

void ACWBaseWorkZone::ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds)
{
	// 기본 동작 없음. 자식 클래스에서 구현한다.
}

bool ACWBaseWorkZone::CanUseTool(ECWToolType ToolType) const
{
	// 기획서상 4단계 전부 도구가 필요하다. 맨손으로는 어떤 존도 작업할 수 없다.
	// (AcceptedTools 를 채우는 걸 잊어도 도구 없이 클리어되지 않도록 여기서 막는다)
	if (ToolType == ECWToolType::None)
	{
		return false;
	}

	if (AcceptedTools.Num() == 0)
	{
		return true;
	}

	return AcceptedTools.Contains(ToolType);
}

bool ACWBaseWorkZone::CanWorkNow(ECWToolType ToolType) const
{
	return !bLocked && !bCompleted && CanUseTool(ToolType);
}

void ACWBaseWorkZone::SetProgress(float NewProgress)
{
	// 진행도는 서버만 정한다. 클라이언트에서 불러도 조용히 무시한다.
	if (!HasAuthority())
	{
		return;
	}

	const float NewClamped = FMath::Clamp(NewProgress, 0.f, 1.f);
	if (FMath::IsNearlyEqual(NewClamped, Progress))
	{
		return;
	}

	Progress = NewClamped;

	// 완료 여부를 먼저 정하고 나서 연출과 화면 표시를 갱신해야 한다.
	// 순서가 반대면 100% 가 된 프레임의 표시가 "미완료" 색으로 그려지고,
	// 그 뒤로는 진행도가 안 바뀌어 다시 그릴 일이 없으니 영영 그 색으로 남는다.
	//
	// 완료가 풀릴 수도 있다. 차량이 페인트를 또 밟으면 "정리" 목표가 다시 열린다.
	const bool bWasCompleted = bCompleted;
	bCompleted = (Progress >= CompleteThreshold);

	HandleProgressChanged();

	if (bCompleted && !bWasCompleted)
	{
		OnWorkCompleted.Broadcast(this);
	}
}

void ACWBaseWorkZone::ReceiveReportedProgress(float NewProgress)
{
	if (!HasAuthority())
	{
		return;
	}

	SetProgress(FMath::Max(Progress, NewProgress));
}

void ACWBaseWorkZone::HandleProgressChanged()
{
	OnProgressChanged(Progress);
	OnWorkProgress.Broadcast(this, Progress);

	ShowDebugProgress();
}

void ACWBaseWorkZone::OnRep_Progress()
{
	// 서버에서 올라온 진행도. 연출과 델리게이트는 클라이언트에서도 같은 경로를 탄다.
	HandleProgressChanged();
}

void ACWBaseWorkZone::OnRep_Completed()
{
	// HUD 체크리스트가 클라이언트에서도 갱신되도록 완료를 알린다.
	if (bCompleted)
	{
		OnWorkCompleted.Broadcast(this);
	}

	ShowDebugProgress();
}

void ACWBaseWorkZone::InitializeProgress(float InProgress)
{
	Progress = FMath::Clamp(InProgress, 0.f, 1.f);
	bCompleted = (Progress >= CompleteThreshold);

	OnProgressChanged(Progress);
}

void ACWBaseWorkZone::OnProgressChanged(float NewProgress)
{
	// 자식 클래스에서 연출을 넣는 자리.
}

void ACWBaseWorkZone::NotifyStateChanged()
{
	// 진행도가 바뀐 것과 같은 경로를 탄다. 델리게이트를 듣는 쪽은 무엇이 바뀌었는지
	// 구분할 필요가 없다. 어차피 존에게 현재 상태를 다시 물어보기 때문이다.
	HandleProgressChanged();
}

void ACWBaseWorkZone::ShowDebugProgress() const
{
	if (!bShowDebugProgress || GEngine == nullptr)
	{
		return;
	}

	// 존을 겹쳐 놓으면 뷰포트만 봐서는 어느 단계가 열려 있는지 알 수 없다.
	// 순서와 잠금 상태를 같이 찍어야 순서 설정이 틀렸는지 바로 보인다.
	const FColor LineColor = bLocked ? FColor::Silver : (bCompleted ? FColor::Green : FColor::Yellow);
	const TCHAR* StateText = bLocked ? TEXT(" [잠김]") : TEXT("");

	// 소수점 한 자리까지 찍는다. 반올림하면 99.6% 가 "100%" 로 나와서
	// "100% 인데 완료가 안 된다" 로 보인다. 완료 기준(CompleteThreshold)도 같이 찍어야
	// 덜 칠한 건지 기준이 빡빡한 건지 구분된다.
	// 키를 액터별로 고정해야 존이 여러 개일 때 각자 한 줄씩 갱신된다. (매 프레임 새 줄이 쌓이지 않음)
	// GetUniqueID() 는 uint32 라서 uint64 / int32 오버로드 양쪽에 걸린다. 캐스트로 하나를 지정해야 한다.
	GEngine->AddOnScreenDebugMessage(
		static_cast<uint64>(GetUniqueID()),
		2.f,
		LineColor,
		FString::Printf(TEXT("%d. %s : %.1f%% / %.0f%%%s%s"),
			WorkOrder,
			*ObjectiveName.ToString(),
			Progress * 100.f,
			CompleteThreshold * 100.f,
			*GetDebugDetailText(),
			StateText));
}

