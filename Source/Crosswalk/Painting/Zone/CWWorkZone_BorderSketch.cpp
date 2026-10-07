#include "Painting/Zone/CWWorkZone_BorderSketch.h"

#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Painting/Components/CWWorkerComponent.h"
#include "Painting/CWPaintingSubsystem.h"
#include "Painting/Zone/CWWorkZone_Cells.h"
#include "Painting/Tools/CWBaseTool.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// BorderMeshes 의 per-instance custom data 칸 수. [0] 도색 정도, [1] 대시 개수.
	constexpr int32 BorderCustomDataCount = 2;
}

ACWWorkZone_BorderSketch::ACWWorkZone_BorderSketch()
{
	// 작업 키를 뗀 것은 ApplyWork 가 끊기는 것으로만 알 수 있다. 그걸 재려면 존이 틱을 돌아야 한다.
	PrimaryActorTick.bCanEverTick = true;

	// 횡단보도 테두리 한 줄 정도의 기본 크기. 레벨에서 조절한다.
	Area->SetBoxExtent(FVector(400.f, 60.f, 20.f));

	// 칸을 하나도 남기지 않아야 완료다. 부모 기본값(0.985)은 칸이 수천 개인 도색 존을 위한
	// 여유분이라, 칸이 수십 개뿐인 여기서는 마지막 한 칸이 그냥 건너뛰어진다.
	CompleteThreshold = 1.f;

	// 이 존은 라인 테두리 도구 전용이다. 기본값으로 넣어 두면 배치할 때 빼먹을 일이 없다.
	AcceptedTools.Add(ECWToolType::Liner);

	// 그려둔 가이드라인 위에는 페인트가 묻지 않는다.
	//
	// 이 존이 내는 선은 3단계 도색의 기준선이라 계속 보여야 한다. 오염 레이어가 이 자리에도
	// 칸을 두면 롤러가 지나갈 때마다 선 위가 덮여서, 어디까지 칠해야 하는지가 안 보인다.
	// 레벨에서 끄고 싶으면 인스턴스에서 내리면 된다.
	bBlocksPaintSpill = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));

	/**
	 * 임시 머티리얼이다. 형태만 보이게 해두는 용도다.
	 *
	 * 이 존이 제대로 보이려면 PerInstanceCustomData 를 읽는 전용 머티리얼이 필요하다.
	 * (BorderMeshes 주석 참고) 그게 붙기 전까지는 모든 칸이 한 색으로 납작하게 보인다.
	 * 컴포넌트 Details 의 Materials 슬롯에서 교체한다.
	 */
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	BorderMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BorderMeshes"));
	BorderMeshes->SetupAttachment(Area);
	BorderMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// 반투명 가이드가 그림자를 드리우면 바닥에 얼룩처럼 보인다.
	BorderMeshes->SetCastShadow(false);

	// 칸마다 두 값을 넘긴다. [0] 도색 정도(0 = 가이드, 1 = 도색 완료), [1] 대시 개수.
	//
	// 이 줄이 빠지면 값이 항상 0 으로 들어가서 칠해도 색이 안 바뀐다.
	// 증상이 "머티리얼이 안 먹는다" 처럼 나타나 원인을 찾기 어렵다.
	BorderMeshes->NumCustomDataFloats = BorderCustomDataCount;

	PreviewBox = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewBox"));
	PreviewBox->SetupAttachment(Area);
	PreviewBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewBox->SetCastShadow(false);

	// 위치가 캐릭터를 따라다니므로 존의 트랜스폼을 따라가면 안 된다.
	// 매 프레임 월드 좌표로 직접 놓는다.
	PreviewBox->SetUsingAbsoluteLocation(true);
	PreviewBox->SetUsingAbsoluteRotation(true);
	PreviewBox->SetUsingAbsoluteScale(true);

	// 띄울 일이 생기기 전까지는 숨겨 둔다.
	PreviewBox->SetVisibility(false);

	PreviewBandMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PreviewBandMeshes"));
	PreviewBandMeshes->SetupAttachment(Area);
	PreviewBandMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewBandMeshes->SetCastShadow(false);

	if (PlaneMesh.Succeeded())
	{
		BorderMeshes->SetStaticMesh(PlaneMesh.Object);
		PreviewBox->SetStaticMesh(PlaneMesh.Object);
		PreviewBandMeshes->SetStaticMesh(PlaneMesh.Object);
	}

	if (BasicMaterial.Succeeded())
	{
		BorderMeshes->SetMaterial(0, BasicMaterial.Object);
		PreviewBox->SetMaterial(0, BasicMaterial.Object);
		PreviewBandMeshes->SetMaterial(0, BasicMaterial.Object);
	}
}

const ACWWorkZone_Cells* ACWWorkZone_BorderSketch::FindFillHighlightZone() const
{
	const UCWPaintingSubsystem* Painting = UCWPaintingSubsystem::Get(this);
	if (Painting == nullptr || GetWorkGroup().IsNone())
	{
		return nullptr;
	}
	const ACWWorkZone_Cells* Match = nullptr;
	for (const TWeakObjectPtr<ACWBaseWorkZone>& WeakZone : Painting->GetZones())
	{
		const ACWWorkZone_Cells* Fill = Cast<ACWWorkZone_Cells>(WeakZone.Get());
		if (Fill == nullptr || !Fill->IsFillMode() || Fill->GetWorkGroup() != GetWorkGroup())
		{
			continue;
		}
		if (Match != nullptr)
		{
			return nullptr;
		}
		Match = Fill;
	}
	return Match;
}

ECWZoneHighlightState ACWWorkZone_BorderSketch::GetHighlightStateFor(ECWToolType ToolType) const
{
	// 라인 도구의 가이드는 기존 칸 머티리얼이 담당한다. 롤러 안내는 앞 작업이 끝나야 켠다.
	if (!bHighlightable || !IsCompleted() || IsLocked())
	{
		return ECWZoneHighlightState::None;
	}
	const ACWWorkZone_Cells* Fill = FindFillHighlightZone();
	// 실제 붓질과 같은 판정으로 AcceptedTools + FillCells 설정도 인식한다.
	return Fill != nullptr && Fill->CanUseTool(ToolType) && Fill->ResolveFillForTool(ToolType)
		? Fill->GetHighlightStateFor(ToolType)
		: ECWZoneHighlightState::None;
}

void ACWWorkZone_BorderSketch::OnHighlightStateChanged_Implementation(ECWZoneHighlightState NewState)
{
	// BorderMeshes는 칠한 칸을 흰 선으로 바꾼다. 전체 데칼을 덧그리면 그 위에 안내가 남는다.
	Super::OnHighlightStateChanged_Implementation(ECWZoneHighlightState::None);
	const ACWWorkZone_Cells* Fill = FindFillHighlightZone();
	const bool bShow = NewState == ECWZoneHighlightState::Active && IsCompleted() && !IsLocked()
		&& Fill != nullptr && !Fill->IsLocked() && !Fill->IsCompleted();
	if (!bShow)
	{
		if (FillHighlightMaterial != nullptr)
		{
			FillHighlightMaterial->SetScalarParameterValue(TEXT("PaintHighlight"), 0.f);
		}
		return;
	}
	if (FillHighlightMaterial == nullptr)
	{
		FillHighlightMaterial = BorderMeshes->CreateDynamicMaterialInstance(0);
	}
	if (FillHighlightMaterial != nullptr)
	{
		Fill->ApplyPaintHighlightStyle(FillHighlightMaterial);
		FillHighlightMaterial->SetScalarParameterValue(TEXT("PaintHighlight"), 1.f);
	}
}

void ACWWorkZone_BorderSketch::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACWWorkZone_BorderSketch, PaintedCellFlags);
}

void ACWWorkZone_BorderSketch::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 에디터에서 Area 나 칸 길이를 바꾸면 바로 보이게 한다.
	RebuildCells();
}

void ACWWorkZone_BorderSketch::BeginPlay()
{
	Super::BeginPlay();

	// OnConstruction 이 이미 만들었지만 다시 만든다.
	// 스폰 경로에 따라 OnConstruction 이 돌지 않는 경우가 있고, 칸 배열이 비어 있으면
	// 이 존은 아무 일도 하지 않는 채로 조용히 서 있게 된다.
	RebuildCells();

	SetupMaterials();

	if (HasAuthority())
	{
		PaintedCellFlags.Init(0, Cells.Num());

		// 시작 진행도가 있으면 그만큼의 칸을 미리 칠해 둔다. (부모의 InitialProgress)
		//
		// 부모는 Progress 값 하나만 넣어주는데, 이 존은 "어느 칸이 칠해졌는가" 가 진짜 상태이고
		// 진행도는 거기서 계산되는 결과다. 그대로 두면 진행도만 1 이고 테두리는 전부
		// 가이드 색인 상태가 되어, 다 그려진 선을 가이드로 쓰려는 용도가 깨진다.
		const int32 InitialPaintedCount =
			FMath::Clamp(FMath::RoundToInt(InitialProgress * Cells.Num()), 0, Cells.Num());
		for (int32 CellIndex = 0; CellIndex < InitialPaintedCount; ++CellIndex)
		{
			PaintedCellFlags[CellIndex] = 1;
		}

		RefreshProgressFromCells();
	}
	else if (PaintedCellFlags.Num() != Cells.Num())
	{
		// 서버 값이 아직 안 왔을 수 있다. 빈 상태로 두면 인덱스가 어긋나므로 크기만 맞춰 둔다.
		// 값이 도착하면 OnRep 이 통째로 덮는다.
		PaintedCellFlags.Init(0, Cells.Num());
	}

	// 시작 상태는 연출 없이 그대로 켠다. 레벨을 열자마자 이미 그려져 있던 선이
	// 눈앞에서 한 칸씩 그려지면 방금 누가 칠한 것처럼 보인다.
	RevealedCellFlags = PaintedCellFlags;

	RefreshPaintedInstances();
}

void ACWWorkZone_BorderSketch::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 차징을 먼저 처리한다. 이 프레임에 릴리즈됐으면 프리뷰도 같은 프레임에 사라져야 한다.
	TickCharge(DeltaSeconds);

	TickCellReveal();

	UpdatePreview();
}

void ACWWorkZone_BorderSketch::TickCharge(float DeltaSeconds)
{
	if (!bCharging)
	{
		return;
	}

	// 도구를 놓거나, 차에 치이거나, 존을 벗어나면 차징을 버린다. 페인트는 나가지 않는다.
	if (!IsChargingWorkerEligible())
	{
		AbortCharge();
		return;
	}

	// ApplyWork 가 끊겼으면 키를 뗀 것이다. (ReleaseGraceSeconds 주석 참고)
	//
	// 유예를 프레임 시간과 같이 본다. 고정값만 쓰면 프레임이 그 값보다 길어지는 순간
	// (설정값 0.05 기준 20 FPS) 누르고 있는데도 매 프레임 릴리즈로 잡혀서,
	// 사양이 낮은 기계에서만 차징이 아예 안 되는 버그가 된다.
	const float ReleaseGrace = FMath::Max(ReleaseGraceSeconds, DeltaSeconds * 2.f);
	if (GetWorld()->TimeSince(LastApplyWorkTime) > ReleaseGrace)
	{
		ReleaseCharge();
		return;
	}

	// 단계가 오른 프레임에만 디버그를 띄운다. 매 프레임 띄우면 화면이 덮인다.
	const int32 CurrentLevel = GetChargeLevel();
	if (CurrentLevel != LastReportedChargeLevel)
	{
		LastReportedChargeLevel = CurrentLevel;
		ShowChargeDebug(TEXT("단계"), CurrentLevel, 0);
	}

	// 최대 단계에서 FullChargeHoldSeconds 가 지나면 저절로 내보낸다.
	// 더 눌러봐야 범위가 늘지 않고 차에 노출되는 시간만 길어진다. 다만 닿자마자 나가면
	// 최대 범위를 볼 틈이 없으므로 잠깐 붙잡아 둔다. (FullChargeHoldSeconds 주석 참고)
	if (ChargeThresholds.Num() == 0 || CurrentLevel < ChargeThresholds.Num())
	{
		return;
	}

	// 체온 계수가 곱해지지 않은 실제 프레임 시간을 쌓는다.
	FullChargeElapsed += DeltaSeconds;
	if (FullChargeElapsed < FullChargeHoldSeconds)
	{
		return;
	}

	// 위의 IsChargingWorkerEligible 이 작업자가 있는 것을 이미 확인했다.
	UCWWorkerComponent* const Worker = ChargingWorker;
	ReleaseCharge();

	// 키를 뗀 것과 똑같이 작업 자체를 끝낸다. 차징만 막으면 bWorking 이 켜진 채로 남아서
	// 작업 애니메이션과 체온 상승이 계속 돈다. 서버에도 같이 알려지므로 남의 화면에서도 멈춘다.
	// 작업 시작은 키를 누른 순간(Started)에만 들어오므로, 다음 차징은 키를 다시 눌러야 시작된다.
	Worker->StopWork();
}

void ACWWorkZone_BorderSketch::ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds)
{
	// 차징은 누르는 본인 기계에서만 돈다.
	//
	// 서버가 원격 플레이어 몫까지 같이 재면 차징이 두 벌 돌아간다. 서버 쪽은 bWorking 이
	// 리플리케이트되는 것을 보고 시작하므로 핑만큼 늦게 출발하고, 그만큼 단계가 덜 찬 채로
	// 릴리즈된다. 그래서 4칸을 노렸는데 3칸만 칠해지는 일이 생긴다.
	// 서버는 릴리즈 때 올라오는 칸 목록만 받는다. (ReceiveBorderCells)
	if (Worker == nullptr || !Worker->IsLocalWorker() || !CanWorkNow(Worker->GetHeldTool()))
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();

	// 릴리즈 붓질이 끝나기 전에는 다음 차징을 받지 않는다.
	// 없으면 키를 연타해서 모션을 건너뛰고 1칸씩 계속 찍을 수 있다.
	if (Now < ReleaseEndTime)
	{
		return;
	}

	// 위에서 로컬 작업자만 통과시키므로, 이미 차징 중이라면 그 사람이 바로 이 작업자다.
	if (!bCharging)
	{
		BeginCharge(Worker);
	}

	LastApplyWorkTime = Now;

	// 넘어온 시간에는 이미 체온 계수가 곱해져 있다. 더울수록 차징이 느려진다.
	ChargeSeconds += DeltaSeconds;
}

bool ACWWorkZone_BorderSketch::ConsumeDirectionInput(const FVector2D& MoveInput)
{
	// 차징 중과 릴리즈 모션 동안에는 이동 입력을 전부 삼킨다. 캐릭터는 제자리에 선다.
	//
	// 여기서 입력을 방향 전환 같은 다른 용도로 쓰지 않는다. 못 움직인다는 사실 자체가
	// 이 존의 위험 부담이고, 이동으로 빠져나갈 구멍을 주면 그게 없어진다.
	return bCharging || GetWorld()->GetTimeSeconds() < ReleaseEndTime;
}

void ACWWorkZone_BorderSketch::CancelWork()
{
	// 차징 상태는 전부 로컬이라 이 기계에서 바로 끄면 된다. 서버 확정을 기다릴 이유가 없다.
	AbortCharge();
}

int32 ACWWorkZone_BorderSketch::GetChargeLevel() const
{
	int32 Level = 0;
	for (int32 ThresholdIndex = 0; ThresholdIndex < ChargeThresholds.Num(); ++ThresholdIndex)
	{
		if (ChargeSeconds >= ChargeThresholds[ThresholdIndex])
		{
			Level = ThresholdIndex + 1;
		}
	}

	return Level;
}

void ACWWorkZone_BorderSketch::GetChargeTargetCells(TArray<int32>& OutCells) const
{
	OutCells.Reset();

	// 차징 중에만 불린다. (ReleaseCharge)
	const int32 Level = GetChargeLevel();
	if (Level <= 0)
	{
		return;
	}

	CollectCellsInArea(ChargeOriginLocation, ChargeForwardDirection, GetChargeAreaSize(Level), OutCells);
}

FVector2D ACWWorkZone_BorderSketch::GetChargeAreaSize(int32 Level) const
{
	if (ChargeAreaSizes.Num() == 0)
	{
		return FVector2D::ZeroVector;
	}

	// 단계 0 은 최소 홀드 전이다. 아직 아무것도 안 나가지만 어디를 겨누는지는 보여줘야 하므로
	// 1단계 크기를 쓴다. 색이 빨강이라 "지금 놓으면 안 나간다" 는 따로 전달된다.
	const int32 SizeIndex = FMath::Clamp(Level - 1, 0, ChargeAreaSizes.Num() - 1);

	return ChargeAreaSizes[SizeIndex];
}

void ACWWorkZone_BorderSketch::CollectCellsInArea(const FVector& Origin, const FVector& Forward,
	const FVector2D& AreaSize, TArray<int32>& OutCells) const
{
	OutCells.Reset();

	if (AreaSize.X <= 0.f || AreaSize.Y <= 0.f || Cells.Num() == 0)
	{
		return;
	}

	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
	const float HalfWidth = AreaSize.Y * 0.5f;

	// 거리와 함께 모아서 가까운 순으로 정렬한다. 페인트가 모자라면 앞에서부터 끊기는데,
	// 순서가 뒤죽박죽이면 범위 한가운데가 비고 끝만 칠해지는 그림이 나온다.
	TArray<TPair<float, int32>> Found;

	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		const FVector ToCell = GetCellWorldLocation(CellIndex) - Origin;

		// 높이는 보지 않는다. 사각형은 바닥에 눕힌 평면이다.
		const float AlongForward = FVector::DotProduct(ToCell, Forward);
		if (AlongForward < 0.f || AlongForward > AreaSize.X)
		{
			continue;
		}

		if (FMath::Abs(FVector::DotProduct(ToCell, Right)) > HalfWidth)
		{
			continue;
		}

		Found.Emplace(AlongForward, CellIndex);
	}

	Found.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B)
	{
		return A.Key < B.Key;
	});

	OutCells.Reserve(Found.Num());
	for (const TPair<float, int32>& Entry : Found)
	{
		OutCells.Add(Entry.Value);
	}
}

void ACWWorkZone_BorderSketch::GetChargeFrame(const UCWWorkerComponent* Worker,
	FVector& OutOrigin, FVector& OutForward) const
{
	// 차징 중이면 시작할 때 얼려둔 값을 그대로 쓴다.
	//
	// 그래서 이 함수는 차징을 시작하는 자리에서 쓰면 안 된다. 얼릴 값을 정하는 시점에
	// 부르면 아직 비어 있는 자기 값을 도로 읽어간다. 그쪽은 ComputeWorkerFrame 을 쓴다.
	if (bCharging)
	{
		OutOrigin = ChargeOriginLocation;
		OutForward = ChargeForwardDirection;
		return;
	}

	ComputeWorkerFrame(Worker, OutOrigin, OutForward);
}

void ACWWorkZone_BorderSketch::ComputeWorkerFrame(const UCWWorkerComponent* Worker,
	FVector& OutOrigin, FVector& OutForward) const
{
	OutOrigin = FVector::ZeroVector;
	OutForward = FVector::ForwardVector;

	const AActor* WorkerActor = (Worker != nullptr) ? Worker->GetOwner() : nullptr;
	if (WorkerActor == nullptr)
	{
		return;
	}

	// 기준점은 캐릭터 발밑이다. 커서를 기준으로 잡으면 범위가 캐릭터와 커서 사이 어디쯤에서
	// 시작할지가 애매해져서, 어디까지 닿는지를 눈으로 익히기 어렵다.
	// 커서는 방향만 정한다. (캐릭터가 커서를 바라보므로 정면이 곧 커서 방향이다)
	OutOrigin = WorkerActor->GetActorLocation();

	FVector Forward = WorkerActor->GetActorForwardVector();
	Forward.Z = 0.f;

	if (!Forward.Normalize())
	{
		Forward = FVector::ForwardVector;
	}

	OutForward = Forward;
}

int32 ACWWorkZone_BorderSketch::GetPaintedCellCount() const
{
	int32 PaintedCount = 0;
	for (const uint8 Flag : PaintedCellFlags)
	{
		PaintedCount += (Flag != 0) ? 1 : 0;
	}

	return PaintedCount;
}

bool ACWWorkZone_BorderSketch::IsCellPainted(int32 CellIndex) const
{
	return PaintedCellFlags.IsValidIndex(CellIndex) && PaintedCellFlags[CellIndex] != 0;
}

FString ACWWorkZone_BorderSketch::GetDebugDetailText() const
{
	return FString::Printf(TEXT(" 칸 %d/%d"), GetPaintedCellCount(), Cells.Num());
}

void ACWWorkZone_BorderSketch::BeginCharge(UCWWorkerComponent* Worker)
{
	ChargingWorker = Worker;
	bCharging = true;
	ChargeSeconds = 0.f;

	// 범위의 기준점과 방향은 차징이 시작될 때 한 번만 정하고 중간에 다시 보지 않는다.
	// 누른 뒤에도 커서를 따라 범위가 돌면 조준한 의미가 없어진다.
	//
	// 여기서 GetChargeFrame 을 쓰면 안 된다. 그쪽은 차징 중이면 얼려둔 값을 돌려주는데,
	// 이 시점에는 bCharging 이 이미 켜져 있어서 아직 빈 값을 자기 자신에 도로 넣는다.
	ComputeWorkerFrame(Worker, ChargeOriginLocation, ChargeForwardDirection);
}

void ACWWorkZone_BorderSketch::ReleaseCharge()
{
	TArray<int32> TargetCells;
	GetChargeTargetCells(TargetCells);

	const int32 Level = GetChargeLevel();
	UCWWorkerComponent* const Worker = ChargingWorker;

	int32 PaintedCount = 0;
	bool bPaintShort = false;

	if (TargetCells.Num() > 0)
	{
		if (HasAuthority())
		{
			// 서버(리슨 서버 호스트 포함)는 바로 확정한다. 올려보낼 곳이 자기 자신이다.
			PaintedCount = ApplyBorderCells(Worker, TargetCells, bPaintShort);
		}
		else
		{
			// 본인 화면에 먼저 칠하고 칸 번호만 올린다. 서버 왕복을 기다리면 붓질이 늦게
			// 나가고, 그 사이 차를 피하려고 움직이는 순간과 겹쳐 조작이 어긋난 것처럼 느껴진다.
			//
			// 예측이 빗나가도 되돌릴 코드가 필요 없다. 서버가 확정한 값이 통째로 내려와
			// OnRep_PaintedCells 에서 덮는다.
			PaintedCount = PredictBorderCells(Worker, TargetCells, bPaintShort);

			// 걸러낸 결과가 아니라 겨눈 칸을 그대로 올린다.
			// 서버는 자기가 가진 칸 상태와 잔량으로 다시 판단해야 한다. 여기서 걸러 보내면
			// 그 사이 남이 칠한 칸을 서버가 알아차릴 기회가 없다.
			Worker->SubmitBorderCells(this, TargetCells);
		}
	}

	ResetChargeState();

	// 붓질 모션이 끝날 때까지 이동을 잠근다. 패링은 이 동안에도 된다.
	ReleaseEndTime = GetWorld()->GetTimeSeconds() + ReleaseSeconds;

	const TCHAR* EventName = TEXT("릴리즈");
	if (Level <= 0)
	{
		EventName = TEXT("허탕(최소 홀드 미달)");
	}
	else if (bPaintShort && PaintedCount == 0)
	{
		EventName = TEXT("헛스윙(페인트 부족)");
	}
	else if (PaintedCount == 0)
	{
		EventName = TEXT("헛스윙(이미 칠해진 칸)");
	}
	else if (bPaintShort)
	{
		EventName = TEXT("일부만 도색(페인트 부족)");
	}

	ShowChargeDebug(EventName, Level, PaintedCount);
}

void ACWWorkZone_BorderSketch::AbortCharge()
{
	if (!bCharging)
	{
		return;
	}

	ResetChargeState();

	// 릴리즈 모션을 걸지 않는다. 차에 치여 날아가는 중에 붓질 모션이 나오면 안 된다.
	ShowChargeDebug(TEXT("중단"), 0, 0);
}

void ACWWorkZone_BorderSketch::ResetChargeState()
{
	bCharging = false;
	ChargeSeconds = 0.f;
	FullChargeElapsed = 0.f;
	LastReportedChargeLevel = 0;
	ChargingWorker = nullptr;
}

bool ACWWorkZone_BorderSketch::IsChargingWorkerEligible() const
{
	const UCWWorkerComponent* Worker = ChargingWorker;
	if (Worker == nullptr)
	{
		return false;
	}

	// 라인 테두리 도구를 계속 들고 있어야 한다. 차징하는 사이에 다른 사람이 마지막 칸을
	// 칠했거나, 앞 단계가 되돌아가 존이 잠겼을 수도 있다. ApplyWork 와 같은 기준이다.
	if (!CanWorkNow(Worker->GetHeldTool()))
	{
		return false;
	}

	// 차량에 치여 다운된 동안에는 차징도 중단된다.
	if (Worker->IsWorkBlocked())
	{
		return false;
	}

	const AActor* WorkerActor = Worker->GetOwner();
	if (WorkerActor == nullptr)
	{
		return false;
	}

	// 차징 중에는 스스로 움직이지 못하지만 넉백 등으로 밀려날 수는 있다.
	return GetSquaredDistanceToPoint(WorkerActor->GetActorLocation()) <= FMath::Square(WorkRange);
}

float ACWWorkZone_BorderSketch::GetCellPaintCost() const
{
	// 존 하나를 다 칠하면 정확히 PaintCostFull 이 든다. (차징 붓 기획서 2.6)
	return (Cells.Num() > 0) ? (PaintCostFull / Cells.Num()) : 0.f;
}

void ACWWorkZone_BorderSketch::ShowChargeDebug(const TCHAR* Event, int32 Level, int32 PaintedCount) const
{
	if (!bShowDebugCharge || GEngine == nullptr)
	{
		return;
	}

	GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Yellow,
		FString::Printf(TEXT("[테두리] %s  단계=%d  칠한칸=%d  차징=%.2f초"),
			Event, Level, PaintedCount, ChargeSeconds));
}

void ACWWorkZone_BorderSketch::RebuildCells()
{
	Cells.Reset();
	BorderMeshes->ClearInstances();

	// 생성자에서 정한 값이지만 여기서 다시 맞춘다. 예전에 1 로 저장된 블루프린트나 레벨 인스턴스는
	// 그 값을 들고 오고, 그러면 [1] 대시 개수가 조용히 버려져 선이 실선으로 돌아간다.
	if (BorderMeshes->NumCustomDataFloats != BorderCustomDataCount)
	{
		BorderMeshes->SetNumCustomDataFloats(BorderCustomDataCount);
	}

	// 점등 상태도 같은 이유로 버린다. 옛 칸 번호는 새 격자에서 다른 자리를 가리킨다.
	RevealedCellFlags.Reset();
	PendingRevealCells.Reset();

	PreviewBandMeshes->ClearInstances();
	PreviewBandCells.Reset();

	const FVector AreaExtent = Area->GetUnscaledBoxExtent();
	const float SizeX = AreaExtent.X * 2.f;
	const float SizeY = AreaExtent.Y * 2.f;
	if (SizeX <= KINDA_SMALL_NUMBER || SizeY <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// 존이 아주 얇을 때 테두리끼리 겹쳐 사각형이 뭉개지지 않도록 두께를 제한한다.
	CellThickness = FMath::Min(BorderThickness, FMath::Min(SizeX, SizeY) * 0.5f);
	const float Inset = CellThickness * 0.5f;

	// X 축 변은 모서리까지 꽉 채우고, Y 축 변이 그 사이를 잇는다.
	// 모서리를 양쪽이 나눠 갖게 하면 두 칸이 겹쳐서 그 자리만 진해 보인다.
	const float NearY = -AreaExtent.Y + Inset;
	const float FarY = AreaExtent.Y - Inset;
	const float RightX = AreaExtent.X - Inset;
	const float LeftX = -AreaExtent.X + Inset;
	const float SideHalfY = FMath::Max(AreaExtent.Y - CellThickness, 0.f);

	// 고리 한 바퀴. 이 순서가 곧 칸 인덱스라 모든 기계에서 같아야 한다.
	AppendSideCells(FVector(-AreaExtent.X, NearY, 0.f), FVector(AreaExtent.X, NearY, 0.f), true);
	AppendSideCells(FVector(RightX, -SideHalfY, 0.f), FVector(RightX, SideHalfY, 0.f), false);
	AppendSideCells(FVector(AreaExtent.X, FarY, 0.f), FVector(-AreaExtent.X, FarY, 0.f), true);
	AppendSideCells(FVector(LeftX, SideHalfY, 0.f), FVector(LeftX, -SideHalfY, 0.f), false);

	for (const FCWBorderCell& Cell : Cells)
	{
		const int32 InstanceIndex = BorderMeshes->AddInstance(MakeCellTransform(Cell));

		// 대시 개수는 칸 모양에서 정해지는 값이라 칸을 만들 때 한 번만 넣는다.
		// 정수로 반올림해야 칸 경계에서 대시가 반토막 나지 않는다. (DashPitch 주석 참고)
		const int32 DashCount = FMath::Max(1, FMath::RoundToInt(Cell.Length / FMath::Max(DashPitch, 1.f)));
		BorderMeshes->SetCustomDataValue(InstanceIndex, 1, static_cast<float>(DashCount), false);
	}

	BorderMeshes->MarkRenderStateDirty();
}

void ACWWorkZone_BorderSketch::AppendSideCells(const FVector& Start, const FVector& End, bool bAlongX)
{
	const FVector Delta = End - Start;
	const float SideLength = Delta.Size();
	if (SideLength <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// 변 안에서 균등하게 나눈다. 그래서 실제 칸 길이는 CellLength 근처에서 조금씩 달라진다.
	// 딱 맞아떨어지는 칸을 깔고 남는 자투리를 두면, 그 자투리 한 칸만 유독 짧아 눈에 띈다.
	const int32 CellCount = FMath::Max(1, FMath::RoundToInt(SideLength / FMath::Max(CellLength, 1.f)));
	const float ActualLength = SideLength / CellCount;
	const FVector Direction = Delta / SideLength;

	for (int32 Index = 0; Index < CellCount; ++Index)
	{
		FCWBorderCell& Cell = Cells.AddDefaulted_GetRef();
		Cell.Center = Start + Direction * (ActualLength * (Index + 0.5f));
		Cell.Length = ActualLength;
		Cell.bAlongX = bAlongX;
	}
}

void ACWWorkZone_BorderSketch::UpdatePreview()
{
	UCWWorkerComponent* const Worker = FindLocalWorker();

	if (!ShouldShowPreview(Worker))
	{
		UpdatePreviewBox(0, false, FVector::ZeroVector, FVector::ForwardVector, -1);

		// 띠도 같이 비운다. 이걸 빼먹으면 사각형만 사라지고 띠는 마지막 모습 그대로 남는다.
		// 붓을 놓은 자리에 띠가 얹힌 채로 굳어서, 방금 칠한 칸들이 덮여 보인다.
		UpdatePreviewBand(TArray<int32>());
		return;
	}

	FVector Origin = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	GetChargeFrame(Worker, Origin, Forward);

	// 차징 중이 아니어도 1단계 범위를 계속 보여준다. 지금 누르면 어디가 나가는지가
	// 늘 보여야 조준이라는 행동이 성립한다.
	const int32 Level = bCharging ? GetChargeLevel() : 0;

	TArray<int32> CellsInArea;
	CollectCellsInArea(Origin, Forward, GetChargeAreaSize(Level), CellsInArea);

	// 실제 릴리즈와 같은 함수로 거른다. 그래야 화면 색과 실제 결과가 갈리지 않는다.
	TArray<int32> AffordableCells;
	bool bPaintShort = false;
	FilterAffordableCells(Worker, CellsInArea, AffordableCells, bPaintShort);

	UpdatePreviewBox(AffordableCells.Num(), bPaintShort, Origin, Forward, Level);

	// 사각형은 "붓이 닿는 영역", 띠는 "그래서 칠해지는 자리" 다.
	// 단계 0 에서는 아무것도 안 나가므로 띠도 비운다. 사각형만 빨갛게 남는다.
	UpdatePreviewBand(Level > 0 ? AffordableCells : TArray<int32>());
}

void ACWWorkZone_BorderSketch::UpdatePreviewBand(const TArray<int32>& AffordableCells)
{
	// 목록이 그대로면 인스턴스를 다시 만들지 않는다. 바뀌는 것은 플레이어가 움직이거나
	// 단계가 오를 때뿐인데 이 함수는 매 프레임 불린다.
	if (PreviewBandCells == AffordableCells)
	{
		return;
	}

	PreviewBandCells = AffordableCells;

	PreviewBandMeshes->ClearInstances();
	for (const int32 CellIndex : PreviewBandCells)
	{
		if (Cells.IsValidIndex(CellIndex))
		{
			PreviewBandMeshes->AddInstance(MakeBandTransform(Cells[CellIndex]));
		}
	}
}

void ACWWorkZone_BorderSketch::UpdatePreviewBox(int32 AffordableCount, bool bPaintShort,
	const FVector& Origin, const FVector& Forward, int32 Level)
{
	// Level 이 음수면 띄울 상황이 아니다.
	if (Level < 0)
	{
		PreviewBox->SetVisibility(false);
		LastPreviewState = -1;
		return;
	}

	const FVector2D AreaSize = GetChargeAreaSize(Level);
	if (AreaSize.X <= 0.f || AreaSize.Y <= 0.f)
	{
		PreviewBox->SetVisibility(false);
		LastPreviewState = -1;
		return;
	}

	// /Engine/BasicShapes/Plane 은 100x100 이고 원점이 가운데다.
	// 발밑에서 앞으로 뻗게 하려면 길이의 절반만큼 앞으로 밀어야 한다.
	FVector BoxLocation = Origin + Forward * (AreaSize.X * 0.5f);
	BoxLocation.Z = GetActorLocation().Z + GuideHeightOffset + PreviewBoxHeightOffset;

	PreviewBox->SetWorldLocationAndRotation(BoxLocation, Forward.Rotation());
	PreviewBox->SetWorldScale3D(FVector(AreaSize.X / 100.f, AreaSize.Y / 100.f, 1.f));
	PreviewBox->SetVisibility(true);

	/**
	 * 숫자 하나로 상태만 넘긴다. 무슨 색으로 그릴지는 머티리얼 인스턴스가 정한다.
	 *
	 *   0 안 나감  — 최소 홀드 전이거나, 겨눈 자리에 칠할 칸이 없거나, 페인트가 0 이다
	 *   1 나간다   — 지금 놓으면 칠해진다
	 *   2 일부만   — 페인트가 모자라 중간에서 끊긴다
	 *
	 * 색을 C++ 이 고르면 아티스트가 MI 에서 바꾼 값이 런타임에 조용히 무시된다.
	 * "머티리얼을 고쳤는데 게임에서는 안 변한다" 가 그래서 생긴다.
	 *
	 * 2 는 **페인트 부족일 때만** 쓴다. 범위 안에 이미 칠해진 칸이 섞였다고 경고를 내면
	 * 반쯤 칠한 뒤부터는 늘 경고라, 정작 보충하러 가야 할 때를 구분할 수 없게 된다.
	 * 이미 칠해진 칸은 페인트도 진행도도 쓰지 않으므로 알릴 일이 아니다.
	 */
	int32 PreviewState = 0;
	if (Level > 0 && AffordableCount > 0)
	{
		PreviewState = bPaintShort ? 2 : 1;
	}

	// 깜빡임은 머티리얼이 Time 으로 스스로 돈다. 여기서 매 프레임 넣을 것이 없으므로
	// 상태가 바뀐 프레임에만 쓴다.
	if (PreviewState == LastPreviewState)
	{
		return;
	}

	LastPreviewState = PreviewState;

	// 사각형과 띠가 같은 상태를 본다. 둘은 머티리얼 인스턴스가 달라서 불투명도는
	// 따로 주되(사각형은 배경, 띠는 전면), 색은 늘 같이 움직여야 한다.
	//
	// 각각 따로 검사한다. 한쪽이 없다고 다른 쪽까지 건너뛰면, 사각형 슬롯이 비었을 때
	// 띠가 영영 상태를 못 받아 기본값 색으로 굳는다.
	if (PreviewBoxMaterial != nullptr)
	{
		PreviewBoxMaterial->SetScalarParameterValue(TEXT("PreviewState"), static_cast<float>(PreviewState));
	}

	if (PreviewBandMaterial != nullptr)
	{
		PreviewBandMaterial->SetScalarParameterValue(TEXT("PreviewState"), static_cast<float>(PreviewState));
	}
}

bool ACWWorkZone_BorderSketch::ShouldShowPreview(const UCWWorkerComponent* Worker) const
{
	if (Worker == nullptr || Cells.Num() == 0 || IsCompleted() || IsLocked())
	{
		return false;
	}

	// 차량에 치여 쓰러져 있는 동안에는 조준이 의미가 없다.
	if (Worker->IsWorkBlocked())
	{
		return false;
	}

	// 붓질 모션이 도는 동안에는 띄우지 않는다. 붓이 나가는 중에 다음 조준이 겹치면
	// 방금 칠한 것인지 앞으로 칠할 것인지 구분이 안 된다.
	if (GetWorld()->GetTimeSeconds() < ReleaseEndTime)
	{
		return false;
	}

	// 차징 중이면 그 차징이 이 플레이어의 것이어야 한다.
	if (bCharging)
	{
		return ChargingWorker == Worker;
	}

	// 작업 컴포넌트가 이 존을 대상으로 잡았을 때만 띄운다.
	// 그쪽이 이미 거리와 도구를 다 보고 고른 값이라, 같은 판정을 여기서 또 하지 않는다.
	return Worker->GetTargetZone() == this && CanWorkNow(Worker->GetHeldTool());
}

UCWWorkerComponent* ACWWorkZone_BorderSketch::FindLocalWorker()
{
	// 데디케이티드 서버에는 볼 사람이 없다.
	if (GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}

	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = (World != nullptr) ? World->GetFirstPlayerController() : nullptr;
	const APawn* LocalPawn = (PlayerController != nullptr) ? PlayerController->GetPawn() : nullptr;

	if (LocalPawn == nullptr)
	{
		CachedLocalWorker = nullptr;
		return nullptr;
	}

	// 캐릭터가 바뀌지 않았으면 찾아둔 것을 그대로 쓴다. 매 프레임 컴포넌트를 뒤질 이유가 없다.
	if (CachedLocalWorker == nullptr || CachedLocalWorker->GetOwner() != LocalPawn)
	{
		CachedLocalWorker = LocalPawn->FindComponentByClass<UCWWorkerComponent>();
	}

	return CachedLocalWorker;
}

void ACWWorkZone_BorderSketch::ReceiveBorderCells(UCWWorkerComponent* Worker, const TArray<int32>& CellIndices)
{
	if (!HasAuthority() || Worker == nullptr)
	{
		return;
	}

	// 도구와 존 상태만 본다. 거리는 보지 않는다.
	//
	// 붓을 놓은 시점과 이 패킷이 도착한 시점 사이에 플레이어는 이미 움직였다. 차를 피하려고
	// 물러난 만큼 거리가 벌어지는데, 그걸로 거절하면 "제대로 칠했는데 안 칠해지는" 일이
	// 핑이 높은 사람에게만 생긴다. 협동 PvE 라 굳이 막을 이유도 없다.
	if (!CanWorkNow(Worker->GetHeldTool()))
	{
		return;
	}

	// 잔량 부족 여부는 여기서 쓰지 않는다. 올려보낸 클라이언트가 자기 예측에서 이미 같은
	// 판정을 하고 연출을 냈다. 서버가 그걸 다시 알려줄 이유가 없다.
	bool bPaintShort = false;
	ApplyBorderCells(Worker, CellIndices, bPaintShort);
}

void ACWWorkZone_BorderSketch::FilterAffordableCells(const UCWWorkerComponent* Worker,
	const TArray<int32>& CellIndices, TArray<int32>& OutCells, bool& bOutPaintShort) const
{
	OutCells.Reset();
	bOutPaintShort = false;

	if (Worker == nullptr)
	{
		return;
	}

	const float CellCost = GetCellPaintCost();

	// 페인트를 쓰지 않는 도구(빗자루 등)는 잔량 개념이 없으므로 제한을 두지 않는다.
	const ACWBaseTool* Tool = Worker->GetHeldToolActor();
	float PaintBudget = TNumericLimits<float>::Max();
	if (Tool != nullptr && Tool->UsesPaint())
	{
		PaintBudget = Tool->GetPaintAmount();
	}

	for (const int32 CellIndex : CellIndices)
	{
		if (!Cells.IsValidIndex(CellIndex))
		{
			continue;
		}

		// 이미 칠해진 칸은 페인트도 진행도도 쓰지 않는다. 차징 범위에 들어와도 그냥 지나간다.
		if (IsCellPainted(CellIndex))
		{
			continue;
		}

		// 남은 페인트로 더 못 사면 거기서 끊는다. 뒤 칸을 건너뛰고 더 싼 칸을 찾지 않는다.
		// 칸 비용은 전부 같으므로 어차피 하나도 못 산다.
		//
		// 여유를 한 톨 두는 이유: 잔량은 칸을 칠할 때마다 조금씩 빼서 만든 값이라 부동소수점
		// 오차가 쌓인다. 가득 찬 통으로 존 하나가 딱 끝나야 하는데(PaintCostFull 규약),
		// 정확히 비교하면 마지막 칸에서 오차 한 톨 때문에 못 사는 일이 생긴다.
		if (PaintBudget + KINDA_SMALL_NUMBER < CellCost)
		{
			// 통에 뭐라도 남아 있으면 그것으로 이 칸 하나는 칠하고 통을 비운다.
			//
			// 이게 없으면 칸 하나 값에 못 미치는 자투리가 통에 갇힌다. 마지막 한 칸을
			// 남겨두고 그 자투리 때문에 보충하러 다녀와야 하는데, 끝내기 직전에 그 왕복을
			// 시키는 것은 벌에 가깝다. 통 하나당 최대 한 칸치를 더 주는 셈이라 밸런싱에
			// 미치는 영향은 작고, "통 하나로 존 하나" 관계도 그대로 유지된다.
			//
			// 넘치게 청구되는 몫은 도구가 0 에서 잘라낸다. (ACWBaseTool::SetPaintAmount)
			if (PaintBudget > KINDA_SMALL_NUMBER)
			{
				PaintBudget = 0.f;
				OutCells.Add(CellIndex);
			}

			// 잔량이 바닥나 여기서 끊겼다는 신호다. 보충하러 갈 때라는 것을 알려야 한다.
			// 잔량이 0 은 아니어서 기존 "페인트 없음" 판정(bPaintDepleted)에는 안 걸린다.
			bOutPaintShort = true;
			break;
		}

		PaintBudget -= CellCost;
		OutCells.Add(CellIndex);
	}
}

int32 ACWWorkZone_BorderSketch::ApplyBorderCells(UCWWorkerComponent* Worker, const TArray<int32>& CellIndices,
	bool& bOutPaintShort)
{
	bOutPaintShort = false;

	if (!HasAuthority())
	{
		return 0;
	}

	TArray<int32> AffordableCells;
	FilterAffordableCells(Worker, CellIndices, AffordableCells, bOutPaintShort);

	const float ProgressBefore = GetProgress();

	int32 PaintedCount = 0;
	for (const int32 CellIndex : AffordableCells)
	{
		if (PaintCell(CellIndex))
		{
			++PaintedCount;
		}
	}

	if (PaintedCount > 0)
	{
		RefreshProgressFromCells();

		// 붓이 지나간 순서 그대로 켠다. 여기서 순서를 잃으면 점등이 붓질로 안 읽힌다.
		EnqueueRevealCells(AffordableCells);

		// 칠한 만큼 페인트를 쓴다. 요청한 양이 아니라 실제로 오른 진행도를 넘긴다.
		ConsumePaintForProgress(Worker, GetProgress() - ProgressBefore);
	}

	return PaintedCount;
}

int32 ACWWorkZone_BorderSketch::PredictBorderCells(const UCWWorkerComponent* Worker, const TArray<int32>& CellIndices,
	bool& bOutPaintShort)
{
	bOutPaintShort = false;

	if (HasAuthority())
	{
		return 0;
	}

	TArray<int32> AffordableCells;
	FilterAffordableCells(Worker, CellIndices, AffordableCells, bOutPaintShort);

	for (const int32 CellIndex : AffordableCells)
	{
		if (PaintedCellFlags.IsValidIndex(CellIndex))
		{
			PaintedCellFlags[CellIndex] = 1;
		}
	}

	EnqueueRevealCells(AffordableCells);

	return AffordableCells.Num();
}

bool ACWWorkZone_BorderSketch::PaintCell(int32 CellIndex)
{
	if (!HasAuthority() || !PaintedCellFlags.IsValidIndex(CellIndex) || PaintedCellFlags[CellIndex] != 0)
	{
		return false;
	}

	PaintedCellFlags[CellIndex] = 1;

	return true;
}

void ACWWorkZone_BorderSketch::RefreshProgressFromCells()
{
	if (!HasAuthority() || Cells.Num() == 0)
	{
		return;
	}

	SetProgress(static_cast<float>(GetPaintedCellCount()) / static_cast<float>(Cells.Num()));
}

void ACWWorkZone_BorderSketch::RefreshPaintedInstances()
{
	// 인스턴스를 더하거나 빼지 않는다. 칸은 처음 만든 그대로 있고 값만 바뀐다.
	// 인스턴스 인덱스가 곧 칸 인덱스라 따로 짝지어 둘 것도 없다.
	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		const bool bRevealed = RevealedCellFlags.IsValidIndex(CellIndex) && RevealedCellFlags[CellIndex] != 0;

		// 마지막 인자가 false 인 이유: 칸마다 렌더 상태를 더럽히면 한 번의 갱신에
		// 수십 번 다시 올린다. 아래에서 한 번에 처리한다.
		BorderMeshes->SetCustomDataValue(CellIndex, 0, bRevealed ? 1.f : 0.f, false);
	}

	BorderMeshes->MarkRenderStateDirty();
}

void ACWWorkZone_BorderSketch::TickCellReveal()
{
	if (PendingRevealCells.Num() == 0)
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextRevealTime)
	{
		return;
	}

	RevealCell(PendingRevealCells[0]);
	PendingRevealCells.RemoveAt(0);

	// 다음 칸까지의 간격. 프레임이 길어져 밀렸어도 한 프레임에 한 칸씩만 켠다.
	// 밀린 만큼 몰아서 켜면 붓질이 아니라 뭉텅이로 보인다.
	NextRevealTime = Now + CellRevealInterval;

	RefreshPaintedInstances();
}

void ACWWorkZone_BorderSketch::EnqueueRevealCells(const TArray<int32>& OrderedCells)
{
	if (OrderedCells.Num() == 0)
	{
		return;
	}

	const bool bWasIdle = (PendingRevealCells.Num() == 0);

	for (const int32 CellIndex : OrderedCells)
	{
		if (!Cells.IsValidIndex(CellIndex) || !IsCellPainted(CellIndex))
		{
			continue;
		}

		if (RevealedCellFlags.IsValidIndex(CellIndex) && RevealedCellFlags[CellIndex] != 0)
		{
			continue;
		}

		PendingRevealCells.AddUnique(CellIndex);
	}

	if (PendingRevealCells.Num() == 0)
	{
		return;
	}

	/**
	 * 붓질 한 번으로 나올 수 없는 양이면 연출 없이 즉시 켠다.
	 *
	 * 늦게 들어와 존 전체를 한꺼번에 받는 경우가 이쪽이다. 그걸 한 칸씩 켜면 남이 이미
	 * 오래전에 칠해둔 선이 내 눈앞에서 그려지는 것처럼 보인다.
	 */
	const int32 MaxStrokeCells = FMath::Max(1, ChargeThresholds.Num());
	if (CellRevealInterval <= 0.f || PendingRevealCells.Num() > MaxStrokeCells)
	{
		for (const int32 CellIndex : PendingRevealCells)
		{
			RevealCell(CellIndex);
		}

		PendingRevealCells.Reset();
		RefreshPaintedInstances();
		return;
	}

	if (bWasIdle)
	{
		// 첫 칸은 기다리지 않는다. 손을 뗀 프레임에 붓이 닿아야 한다.
		NextRevealTime = GetWorld()->GetTimeSeconds();
	}
}

void ACWWorkZone_BorderSketch::SyncRevealQueueToFlags()
{
	if (RevealedCellFlags.Num() != Cells.Num())
	{
		RevealedCellFlags.SetNumZeroed(Cells.Num());
	}

	// 서버가 예측을 되돌렸으면 켜둔 칸을 즉시 끈다. 여기는 연출을 넣지 않는다.
	// 잘못 켰던 것을 무르는 자리라, 천천히 꺼지면 오히려 눈에 더 띈다.
	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		if (!IsCellPainted(CellIndex))
		{
			RevealedCellFlags[CellIndex] = 0;
		}
	}

	PendingRevealCells.RemoveAll([this](int32 CellIndex)
	{
		return !IsCellPainted(CellIndex);
	});

	// 아직 켜지도 대기하지도 않은 칸을 모은다. 남이 칠했거나 늦게 들어와 새로 알게 된 칸이다.
	TArray<int32> NewCells;
	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		if (IsCellPainted(CellIndex) && RevealedCellFlags[CellIndex] == 0
			&& !PendingRevealCells.Contains(CellIndex))
		{
			NewCells.Add(CellIndex);
		}
	}

	EnqueueRevealCells(NewCells);
}

void ACWWorkZone_BorderSketch::RevealCell(int32 CellIndex)
{
	if (!RevealedCellFlags.IsValidIndex(CellIndex) || RevealedCellFlags[CellIndex] != 0)
	{
		return;
	}

	RevealedCellFlags[CellIndex] = 1;
}

FVector ACWWorkZone_BorderSketch::GetCellWorldLocation(int32 CellIndex) const
{
	if (!Cells.IsValidIndex(CellIndex))
	{
		return GetActorLocation();
	}

	FVector LocalLocation = Cells[CellIndex].Center;
	LocalLocation.Z += GuideHeightOffset;

	return GetActorTransform().TransformPosition(LocalLocation);
}

FTransform ACWWorkZone_BorderSketch::MakeCellTransform(const FCWBorderCell& Cell) const
{
	// /Engine/BasicShapes/Plane 은 100x100 이라 크기를 100 으로 나누면 그대로 배율이 된다.
	//
	// 세로 변 칸은 스케일 축을 바꾸지 않고 90도 돌려 놓는다. 그래야 모든 칸에서 메시의 X(= UV 의 U)가
	// 선을 따라가고, 머티리얼이 U 하나로 점선을 그릴 수 있다. (BorderMeshes 주석 참고)
	const FVector Scale(Cell.Length / 100.f, CellThickness / 100.f, 1.f);
	const FRotator Rotation = Cell.bAlongX ? FRotator::ZeroRotator : FRotator(0.f, 90.f, 0.f);

	// 칸이 한 번만 그려지므로 높이도 하나다. 겹쳐 덮던 시절의 층 구분이 필요 없다.
	FVector Location = Cell.Center;
	Location.Z += GuideHeightOffset;

	return FTransform(Rotation, Location, Scale);
}

FTransform ACWWorkZone_BorderSketch::MakeBandTransform(const FCWBorderCell& Cell) const
{
	// 길이는 칸과 똑같이 둔다. 그래야 옆 칸과 딱 붙어서 한 줄로 보인다.
	// 폭만 넓혀서 테두리 위로 비어져 나오게 한다.
	const float BandThickness = CellThickness * PreviewBandWidthScale;

	const FVector Scale = Cell.bAlongX
		? FVector(Cell.Length / 100.f, BandThickness / 100.f, 1.f)
		: FVector(BandThickness / 100.f, Cell.Length / 100.f, 1.f);

	FVector Location = Cell.Center;
	Location.Z += GuideHeightOffset + PreviewBoxHeightOffset + PreviewBandHeightOffset;

	return FTransform(FRotator::ZeroRotator, Location, Scale);
}

void ACWWorkZone_BorderSketch::SetupMaterials()
{
	// 테두리 MID는 롤러 안내를 처음 켤 때 만든다. 칸 상태는 custom data를 그대로 쓴다.
	PreviewBoxMaterial = PreviewBox->CreateAndSetMaterialInstanceDynamic(0);
	PreviewBandMaterial = PreviewBandMeshes->CreateAndSetMaterialInstanceDynamic(0);
}

void ACWWorkZone_BorderSketch::OnRep_PaintedCells()
{
	// 예측이 빗나간 칸은 여기서 꺼지고, 남이 칠한 칸은 점등 대기열로 들어간다.
	SyncRevealQueueToFlags();

	RefreshPaintedInstances();
}
