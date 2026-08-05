#include "Work/CWBaseWorkZone.h"

#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"

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

	// 선행 존이 있으면, 그게 끝날 때까지 잠가둔다.
	if (PrerequisiteZone != nullptr && !PrerequisiteZone->IsCompleted())
	{
		bLocked = true;
		PrerequisiteZone->OnWorkCompleted.AddDynamic(this, &ACWBaseWorkZone::HandlePrerequisiteCompleted);
	}
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
	HandleProgressChanged();

	if (Progress >= CompleteThreshold)
	{
		if (!bCompleted)
		{
			bCompleted = true;
			OnWorkCompleted.Broadcast(this);
		}
	}
	else
	{
		// 완료를 취소할 수도 있다. 차량이 페인트를 또 밟으면 "정리" 목표가 다시 열린다.
		bCompleted = false;
	}
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

void ACWBaseWorkZone::ShowDebugProgress() const
{
	if (!bShowDebugProgress || GEngine == nullptr)
	{
		return;
	}

	// 키를 액터별로 고정해야 존이 여러 개일 때 각자 한 줄씩 갱신된다. (매 프레임 새 줄이 쌓이지 않음)
	// GetUniqueID() 는 uint32 라서 uint64 / int32 오버로드 양쪽에 걸린다. 캐스트로 하나를 지정해야 한다.
	GEngine->AddOnScreenDebugMessage(
		static_cast<uint64>(GetUniqueID()),
		2.f,
		bCompleted ? FColor::Green : FColor::Yellow,
		FString::Printf(TEXT("%s : %.0f%%"), *ObjectiveName.ToString(), Progress * 100.f));
}

void ACWBaseWorkZone::HandlePrerequisiteCompleted(ACWBaseWorkZone* Zone)
{
	bLocked = false;
}
