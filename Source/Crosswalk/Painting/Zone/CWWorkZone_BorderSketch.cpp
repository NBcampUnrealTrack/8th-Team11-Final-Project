#include "Painting/Zone/CWWorkZone_BorderSketch.h"

#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Painting/Screens/CWBorderSketchWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "Painting/Components/CWWorkerComponent.h"

ACWWorkZone_BorderSketch::ACWWorkZone_BorderSketch()
{
	// 작업 키를 떼도 선이 계속 나아가야 하므로 존 자체가 틱을 돈다.
	PrimaryActorTick.bCanEverTick = true;

	// 횡단보도 테두리 한 줄 정도의 기본 크기. 레벨에서 조절한다.
	Area->SetBoxExtent(FVector(400.f, 60.f, 20.f));

	// 한 바퀴를 다 돌아야 완료다. 부모 기본값(0.98)은 격자 칸 존을 위한 값이라 여기서는 맞지 않는다.
	CompleteThreshold = 1.f;

	// 이 존은 라인 테두리 도구 전용이다. 기본값으로 넣어 두면 배치할 때 빼먹을 일이 없다.
	AcceptedTools.Add(ECWToolType::Liner);

	BorderMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BorderMeshes"));
	BorderMeshes->SetupAttachment(Area);
	BorderMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// 반투명 가이드가 그림자를 드리우면 바닥에 얼룩처럼 보인다.
	BorderMeshes->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		BorderMeshes->SetStaticMesh(PlaneMesh.Object);
	}

	// 기본 머티리얼은 불투명이라 반투명 연출이 나오지 않는다.
	// 형태는 바로 보이게 해두고, 반투명 머티리얼은 컴포넌트 Details 의 Materials 슬롯에서 교체한다.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BasicMaterial.Succeeded())
	{
		BorderMeshes->SetMaterial(0, BasicMaterial.Object);
	}

	// 애셋을 만들지 않아도 미니게임이 보이도록 C++ 위젯을 기본값으로 둔다.
	// WBP 로 꾸미고 싶으면 이 클래스를 부모로 하는 위젯 BP 를 만들어 여기에 지정하면 된다.
	WidgetClass = UCWBorderSketchWidget::StaticClass();
}

void ACWWorkZone_BorderSketch::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	FitBorderToArea();
}

void ACWWorkZone_BorderSketch::BeginPlay()
{
	Super::BeginPlay();

	BorderMaterial = BorderMeshes->CreateAndSetMaterialInstanceDynamic(0);

	// 시작 모습은 전환 없이 바로 맞춘다. 이미 완료된 상태로 시작할 수도 있다.
	PaintedBlend = IsCompleted() ? 1.f : 0.f;
	UpdateBorderVisual(0.f);
}

void ACWWorkZone_BorderSketch::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 도색 완료 전환은 모든 인스턴스에서 돌아야 한다.
	UpdateBorderVisual(DeltaSeconds);

	// 서버가 완료를 확정했으면 로컬 잠금은 역할을 다했다.
	// 이후에는 CanWorkNow 가 알아서 막으므로 잠금을 들고 있을 이유가 없다.
	if (bLocallyCompleted && IsCompleted())
	{
		bLocallyCompleted = false;
	}

	// 위젯 표시는 OnRep 에만 기대지 않고 매 프레임 현재 상태로 다시 맞춘다.
	// 서버가 한 업데이트 안에서 상태를 껐다 켜면 OnRep 이 아예 안 오는 경우가 있고,
	// 그때 OnRep 만 믿으면 UI 가 잘못된 상태로 굳는다.
	// Show/HideWidget 은 이미 맞는 상태면 바로 빠져나오므로 비용은 없다.
	RefreshWidget();

	if (!bMinigameActive)
	{
		return;
	}

	// 도구를 놓거나 존에서 벗어나면 UI 를 내리고 멈춘다. 진행도는 남으므로 이어서 할 수 있다.
	if (!IsWorkerEligible())
	{
		StopMinigame();
		return;
	}

	// 꼭짓점에서는 올바른 방향키가 들어올 때까지 선이 멈춰 있는다.
	if (!bWaitingAtCorner && ActiveWorker != nullptr)
	{
		const bool bBoosting = GetWorld()->TimeSince(LastBoostTime) <= 2.f * DeltaSeconds;

		// 체온이 높으면 선도 느려진다. (체온 기획서 4.2)
		//
		// 작업 컴포넌트가 계수를 곱해 넘겨주는 시간은 ApplyWork 로만 오는데, 이 존은 진행이
		// 여기 Tick 에서 돌아서 그 시간을 쓰지 않는다. 그래서 계수를 직접 물어와 곱한다.
		const float EffectiveSeconds = DeltaSeconds
			* (bBoosting ? BoostMultiplier : 1.f)
			* ActiveWorker->GetWorkSpeedFactor();

		// 선이 나아간 만큼 체온이 오른다. 부스트 중이면 그만큼 더 오른다.
		// 꼭짓점에서 멈춰 있는 동안에는 진행이 없으므로 이 블록에 들어오지 않아 체온도 안 오른다.
		//
		// AdvanceLine 보다 먼저 부르는 이유:
		// 선이 한 바퀴를 채우면 AdvanceLine 안에서 미니게임이 끝나면서 ActiveWorker 가 지워진다.
		// 뒤에서 부르면 그 프레임에 null 포인터를 건드린다.
		ActiveWorker->SubmitWorkHeat(EffectiveSeconds);

		AdvanceLine(DrawSpeed * EffectiveSeconds);
	}

	ReportProgressIfDue(false);
}

void ACWWorkZone_BorderSketch::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	HideWidget();

	Super::EndPlay(EndPlayReason);
}

void ACWWorkZone_BorderSketch::ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds)
{
	// 미니게임은 그리는 본인 기계에서만 돈다.
	// 서버가 원격 플레이어 몫까지 돌리면 같은 게임이 두 벌 돌아가며 서로 어긋난다.
	if (Worker == nullptr || !Worker->IsLocalWorker() || !CanWorkNow(Worker->GetHeldTool()))
	{
		return;
	}

	// 이미 한 바퀴를 끝냈으면 서버가 완료를 확정해 줄 때까지 다시 시작하지 않는다.
	if (bLocallyCompleted)
	{
		return;
	}

	if (!bMinigameActive)
	{
		StartMinigame(Worker);
	}
	else if (ActiveWorker != Worker)
	{
		// 이미 다른 사람이 그리는 중이면 끼어들지 않는다.
		// 도구가 1인 1개라 보통 일어나지 않지만, 도구가 늘어나면 여기서 걸린다.
		return;
	}

	// 작업 키를 누르고 있는 동안 선이 빨라진다.
	LastBoostTime = GetWorld()->GetTimeSeconds();
}

bool ACWWorkZone_BorderSketch::ConsumeDirectionInput(const FVector2D& MoveInput)
{
	if (!bMinigameActive)
	{
		return false;
	}

	// 이동 입력은 누르고 있는 내내 매 프레임 들어온다.
	// 한 프레임이라도 끊겼다면 키를 뗐다가 새로 누른 것이다.
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bContinuedHold = (Now - LastDirectionInputTime) <= DirectionInputHoldWindow;
	LastDirectionInputTime = Now;

	if (!bContinuedHold)
	{
		bSuppressCancelUntilRelease = false;
	}

	// 이 함수는 서버와 그리는 클라이언트 양쪽에서 돈다.
	// 클라이언트 쪽 변경은 예측이고, 서버 값이 리플리케이트되면 정정된다.
	// 꼭짓점에서 올바른 방향이면 선을 돌린다. 이 입력만 소비하므로 캐릭터는 제자리에 선다.
	if (bWaitingAtCorner)
	{
		const FVector2D RequiredInput = GetRequiredCornerInput();
		if (!RequiredInput.IsNearlyZero() && GetDominantAxis(MoveInput).Equals(RequiredInput, 0.01f))
		{
			++CurrentSegmentIndex;
			bWaitingAtCorner = false;

			// 전환에 쓴 키는 보통 몇 프레임 더 눌려 있다.
			// 그걸 중단으로 받으면 방향을 꺾자마자 작업이 끊긴다.
			bSuppressCancelUntilRelease = true;

			return true;
		}
	}

	// 미니게임 중에는 이동 입력을 전부 여기서 소비한다. 캐릭터는 제자리에 선다.
	//
	// 이동 입력을 중단 신호로도 쓰던 때는 방향을 꺾자마자 작업이 끊기고,
	// 꼭짓점에서 키를 잘못 누르면 UI 가 닫혀 조작이 계속 튕겼다.
	// 중단은 전용 입력(CancelWork) 하나로만 받는다.
	return true;
}

void ACWWorkZone_BorderSketch::CancelWork()
{
	// 미니게임 상태는 전부 로컬이라 이 기계에서 바로 끄면 된다.
	// 서버 확정을 기다릴 이유가 없다. UI 는 누른 프레임에 사라진다.
	StopMinigame();
}

void ACWWorkZone_BorderSketch::GetFullPath(TArray<FVector2D>& OutPoints) const
{
	OutPoints.Reset(GetSegmentCount() + 1);
	for (int32 PointIndex = 0; PointIndex <= GetSegmentCount(); ++PointIndex)
	{
		OutPoints.Add(GetPathPoint(PointIndex));
	}
}

void ACWWorkZone_BorderSketch::GetDrawnPath(TArray<FVector2D>& OutPoints) const
{
	OutPoints.Reset(GetSegmentCount() + 1);
	OutPoints.Add(GetPathPoint(0));

	float Remaining = DrawnLength;

	for (int32 SegmentIndex = 0; SegmentIndex < GetSegmentCount(); ++SegmentIndex)
	{
		const float SegmentLength = GetSegmentLength(SegmentIndex);

		// 이 변을 완전히 지났으면 끝점을 그대로 찍고 다음 변으로 넘어간다.
		if (Remaining >= SegmentLength)
		{
			OutPoints.Add(GetPathPoint(SegmentIndex + 1));
			Remaining -= SegmentLength;
			continue;
		}

		if (Remaining > 0.f)
		{
			const FVector2D SegmentStart = GetPathPoint(SegmentIndex);
			const FVector2D SegmentEnd = GetPathPoint(SegmentIndex + 1);
			OutPoints.Add(SegmentStart + (SegmentEnd - SegmentStart) * (Remaining / SegmentLength));
		}

		break;
	}
}

FVector2D ACWWorkZone_BorderSketch::GetRequiredCornerInput() const
{
	if (!bWaitingAtCorner || CurrentSegmentIndex + 1 >= GetSegmentCount())
	{
		return FVector2D::ZeroVector;
	}

	const FVector2D NextDirection = GetSegmentDirection(CurrentSegmentIndex + 1);

	// 화면 좌표는 Y 가 아래로 증가하지만 이동 입력은 W(앞)가 +Y 다. Y 부호를 뒤집는다.
	return FVector2D(NextDirection.X, -NextDirection.Y);
}

void ACWWorkZone_BorderSketch::ReportProgressIfDue(bool bForce)
{
	if (ActiveWorker == nullptr)
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (!bForce && Now - LastProgressReportTime < ProgressReportInterval)
	{
		return;
	}

	LastProgressReportTime = Now;

	// 서버에 올리는 건 숫자 하나뿐이다. 되돌아오는 값을 게임플레이에 쓰지 않으므로 정정이 없다.
	ActiveWorker->ReportWorkProgress(this, DrawnLength / GetTotalLength());
}

bool ACWWorkZone_BorderSketch::IsLocalDrawer() const
{
	if (ActiveWorker == nullptr)
	{
		return false;
	}

	const APawn* WorkerPawn = Cast<APawn>(ActiveWorker->GetOwner());

	return WorkerPawn != nullptr && WorkerPawn->IsLocallyControlled();
}

void ACWWorkZone_BorderSketch::StartMinigame(UCWWorkerComponent* Worker)
{
	ActiveWorker = Worker;
	bMinigameActive = true;
	bWaitingAtCorner = false;
	bSuppressCancelUntilRelease = false;

	// 선 상태는 로컬이라 이 기계에는 남아 있지 않을 수 있다.
	// (다른 사람이 그리다 만 것을 이어받는 경우, 또는 이 존을 처음 만지는 경우)
	// 모두가 공유하는 진행도에서 선 위치를 역산해 이어 그린다.
	DrawnLength = FMath::Clamp(GetProgress(), 0.f, 1.f) * GetTotalLength();

	CurrentSegmentIndex = 0;
	while (CurrentSegmentIndex < GetSegmentCount() - 1
		&& DrawnLength >= GetSegmentEndLength(CurrentSegmentIndex))
	{
		++CurrentSegmentIndex;
	}

	RefreshWidget();
}

void ACWWorkZone_BorderSketch::StopMinigame()
{
	// 어떤 이유로 멈추든(완주 · 취소 · 존 이탈) 마지막 진행도는 올려보낸다.
	// 안 그러면 마지막 보고 이후에 그린 만큼이 버려져서, 다시 왔을 때 조금 뒤로 밀려 있다.
	// ActiveWorker 를 지우기 전에 해야 한다. 보고 경로가 그걸 통해 나간다.
	ReportProgressIfDue(true);

	bMinigameActive = false;
	ActiveWorker = nullptr;

	RefreshWidget();
}

bool ACWWorkZone_BorderSketch::IsWorkerEligible() const
{
	const UCWWorkerComponent* Worker = ActiveWorker;
	if (Worker == nullptr)
	{
		return false;
	}

	// 라인 테두리 도구를 계속 들고 있어야 한다.
	if (!CanUseTool(Worker->GetHeldTool()))
	{
		return false;
	}

	// 차량에 치여 다운된 동안에는 미니게임도 중단된다.
	if (Worker->IsWorkBlocked())
	{
		return false;
	}

	const AActor* WorkerActor = Worker->GetOwner();
	if (WorkerActor == nullptr)
	{
		return false;
	}

	return GetSquaredDistanceToPoint(WorkerActor->GetActorLocation()) <= FMath::Square(WorkRange);
}

void ACWWorkZone_BorderSketch::AdvanceLine(float Amount)
{
	const float SegmentEndLength = GetSegmentEndLength(CurrentSegmentIndex);
	const float NewLength = DrawnLength + Amount;

	if (NewLength < SegmentEndLength)
	{
		DrawnLength = NewLength;
		return;
	}

	// 꼭짓점에 딱 맞춰 세운다. 넘어간 만큼은 버린다. (그래야 꼭짓점을 지나쳐 그려지지 않는다)
	DrawnLength = SegmentEndLength;

	if (CurrentSegmentIndex >= GetSegmentCount() - 1)
	{
		// 마지막 변 끝 = 시작점 복귀 = 한 바퀴 완주.
		// StopMinigame 이 100% 를 즉시 신뢰 채널로 올려보낸다.
		bLocallyCompleted = true;
		StopMinigame();
		return;
	}

	bWaitingAtCorner = true;
}

void ACWWorkZone_BorderSketch::RefreshWidget()
{
	// 도구를 든 본인 화면에만 띄운다. 나머지 플레이어는 진행도/완료만 공유받는다.
	if (bMinigameActive && IsLocalDrawer())
	{
		ShowWidget();
	}
	else
	{
		HideWidget();
	}
}

void ACWWorkZone_BorderSketch::ShowWidget()
{
	if (Widget != nullptr || WidgetClass == nullptr)
	{
		return;
	}

	const APawn* WorkerPawn = (ActiveWorker != nullptr) ? Cast<APawn>(ActiveWorker->GetOwner()) : nullptr;
	APlayerController* PlayerController = (WorkerPawn != nullptr) ? Cast<APlayerController>(WorkerPawn->GetController()) : nullptr;
	if (PlayerController == nullptr)
	{
		return;
	}

	Widget = CreateWidget<UCWBorderSketchWidget>(PlayerController, WidgetClass);
	if (Widget == nullptr)
	{
		return;
	}

	Widget->SetZone(this);

	// AddToViewport 가 아니라 AddToPlayerScreen 을 쓰는 이유:
	// 화면 분할 협동에서 도구를 든 플레이어의 화면에만 떠야 한다.
	Widget->AddToPlayerScreen(10);
}

void ACWWorkZone_BorderSketch::HideWidget()
{
	if (Widget == nullptr)
	{
		return;
	}

	Widget->RemoveFromParent();
	Widget = nullptr;
}

void ACWWorkZone_BorderSketch::FitBorderToArea()
{
	BorderMeshes->ClearInstances();

	const FVector AreaExtent = Area->GetUnscaledBoxExtent();
	const float SizeX = AreaExtent.X * 2.f;
	const float SizeY = AreaExtent.Y * 2.f;

	// 존이 아주 얇을 때 테두리끼리 겹쳐 사각형이 뭉개지지 않도록 두께를 제한한다.
	const float Thickness = FMath::Min(BorderThickness, FMath::Min(SizeX, SizeY) * 0.5f);
	const float Inset = Thickness * 0.5f;

	// /Engine/BasicShapes/Plane 은 100x100 이라 크기를 100 으로 나누면 그대로 배율이 된다.
	// 긴 변은 모서리까지 꽉 채우고, 짧은 변이 그 사이를 잇는다.
	const FVector LongBarScale(SizeX / 100.f, Thickness / 100.f, 1.f);
	const FVector ShortBarScale(Thickness / 100.f, SizeY / 100.f, 1.f);

	BorderMeshes->AddInstance(FTransform(FRotator::ZeroRotator,
		FVector(0.f, -AreaExtent.Y + Inset, GuideHeightOffset), LongBarScale));
	BorderMeshes->AddInstance(FTransform(FRotator::ZeroRotator,
		FVector(0.f, AreaExtent.Y - Inset, GuideHeightOffset), LongBarScale));
	BorderMeshes->AddInstance(FTransform(FRotator::ZeroRotator,
		FVector(-AreaExtent.X + Inset, 0.f, GuideHeightOffset), ShortBarScale));
	BorderMeshes->AddInstance(FTransform(FRotator::ZeroRotator,
		FVector(AreaExtent.X - Inset, 0.f, GuideHeightOffset), ShortBarScale));
}

void ACWWorkZone_BorderSketch::UpdateBorderVisual(float DeltaSeconds)
{
	if (BorderMaterial == nullptr)
	{
		return;
	}

	const float TargetBlend = IsCompleted() ? 1.f : 0.f;
	const float CurrentProgress = GetProgress();

	// 전환이 끝났고 진행도도 그대로면 머티리얼을 건드릴 이유가 없다.
	if (FMath::IsNearlyEqual(PaintedBlend, TargetBlend) && FMath::IsNearlyEqual(AppliedProgress, CurrentProgress))
	{
		return;
	}

	if (PaintTransitionSeconds > 0.f)
	{
		PaintedBlend = FMath::FInterpConstantTo(PaintedBlend, TargetBlend, DeltaSeconds, 1.f / PaintTransitionSeconds);
	}
	else
	{
		PaintedBlend = TargetBlend;
	}

	BorderMaterial->SetVectorParameterValue(ColorParameterName, FMath::Lerp(GuideColor, PaintedColor, PaintedBlend));
	BorderMaterial->SetScalarParameterValue(OpacityParameterName, FMath::Lerp(GuideOpacity, PaintedOpacity, PaintedBlend));
	BorderMaterial->SetScalarParameterValue(ProgressParameterName, CurrentProgress);

	AppliedProgress = CurrentProgress;
}

FVector2D ACWWorkZone_BorderSketch::GetPathPoint(int32 PointIndex) const
{
	const float Height = RectHeightRatio;

	switch (PointIndex)
	{
	case 0:  return FVector2D(0.f, Height * 0.5f);	// 시작점 (왼쪽 변 중앙)
	case 1:  return FVector2D(0.f, 0.f);			// 좌상
	case 2:  return FVector2D(1.f, 0.f);			// 우상
	case 3:  return FVector2D(1.f, Height);			// 우하
	case 4:  return FVector2D(0.f, Height);			// 좌하
	default: return FVector2D(0.f, Height * 0.5f);	// 시작점으로 복귀
	}
}

FVector2D ACWWorkZone_BorderSketch::GetSegmentDirection(int32 SegmentIndex) const
{
	const FVector2D Delta = GetPathPoint(SegmentIndex + 1) - GetPathPoint(SegmentIndex);

	return Delta.GetSafeNormal();
}

float ACWWorkZone_BorderSketch::GetSegmentLength(int32 SegmentIndex) const
{
	return (GetPathPoint(SegmentIndex + 1) - GetPathPoint(SegmentIndex)).Size();
}

float ACWWorkZone_BorderSketch::GetSegmentEndLength(int32 SegmentIndex) const
{
	float Total = 0.f;
	for (int32 Index = 0; Index <= SegmentIndex; ++Index)
	{
		Total += GetSegmentLength(Index);
	}

	return Total;
}

float ACWWorkZone_BorderSketch::GetTotalLength() const
{
	return GetSegmentEndLength(GetSegmentCount() - 1);
}

FVector2D ACWWorkZone_BorderSketch::GetDominantAxis(const FVector2D& Input)
{
	if (Input.IsNearlyZero())
	{
		return FVector2D::ZeroVector;
	}

	if (FMath::Abs(Input.X) > FMath::Abs(Input.Y))
	{
		return FVector2D(FMath::Sign(Input.X), 0.f);
	}

	return FVector2D(0.f, FMath::Sign(Input.Y));
}
