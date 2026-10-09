#include "Painting/Zone/CWWorkZone_BorderSketch.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"
#include "Painting/Components/CWWorkerComponent.h"
#include "Painting/CWPaintingSubsystem.h"
#include "Painting/Zone/CWWorkZone_Cells.h"
#include "Painting/Tools/CWBaseTool.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// BorderMeshes 의 per-instance custom data 칸 수.
	// [0] 도색 정도, [1] 대시 개수, [2] 쓸림 시작 시각, [3] 쓸림 시간, [4] 쓸림 방향.
	constexpr int32 BorderCustomDataCount = 5;

	// 연출 없이 칠해진 칸에 넣는 쓸림 시작 시각. 쓸림이 한참 전에 끝난 것으로 읽히게 한다.
	constexpr float InstantRevealStartTime = -1000.f;

	// 쓸림 시간의 하한. 머티리얼이 (Time - 시작) / 시간 으로 나누므로 0 이면 안 된다.
	constexpr float MinSweepDuration = 0.001f;

	// M_BorderCell 의 SweepRagged 파라미터와 같은 값이어야 한다. 마른 붓 가장자리는 칸 안에서 이만큼
	// 더 늦게 완성되므로, 선두가 칸 끝에 닿는 시각은 칸 쓸림 시간의 1 / (1 + 이 값) 지점이다.
	constexpr float SweepRaggedness = 0.3f;

	// RailMeshes 의 per-instance custom data 칸 수.
	// [0] 솟기 시작 시각, [1] 단계, [2] 무너지기 시작 시각, [3] 물마루 여부, [4] 예약.
	constexpr int32 RailCustomDataCount = 5;

	// 무너지기가 아직 정해지지 않은 레일에 넣는 시각. 머티리얼이 "아직 먼 미래" 로 읽는다.
	constexpr float RailNeverCollapse = 100000.f;

	// 조준 프리뷰 레일의 단계 값. 머티리얼이 0.5 미만이면 낮게 깔린 반투명 프리뷰로 그린다.
	// 쓸리는 방향이 U 의 반대일 때는 AimRailReverseOffset 을 더한다. (0.3 을 넘으면 반대로 읽는다)
	constexpr float AimRailTier = 0.25f;
	constexpr float AimRailReverseOffset = 0.1f;
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

	PreviewBandMeshes =CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PreviewBandMeshes"));
	PreviewBandMeshes->SetupAttachment(Area);
	PreviewBandMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewBandMeshes->SetCastShadow(false);

	// 솟는 3D 레일. 상자 하나를 조각마다 배치하고 높이는 머티리얼이 그린다. (RailMeshes 주석 참고)
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	RailMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("RailMeshes"));
	RailMeshes->SetupAttachment(Area);
	RailMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RailMeshes->SetCastShadow(false);
	RailMeshes->NumCustomDataFloats = RailCustomDataCount;
	if (CubeMesh.Succeeded())
	{
		RailMeshes->SetStaticMesh(CubeMesh.Object);
	}

	if (PlaneMesh.Succeeded())
	{
		BorderMeshes->SetStaticMesh(PlaneMesh.Object);
		PreviewBandMeshes->SetStaticMesh(PlaneMesh.Object);
	}

	if (BasicMaterial.Succeeded())
	{
		BorderMeshes->SetMaterial(0, BasicMaterial.Object);
		PreviewBandMeshes->SetMaterial(0, BasicMaterial.Object);
		RailMeshes->SetMaterial(0, BasicMaterial.Object);
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
	TickReleaseAnimation();

	UpdatePreview();

	// 무너지는 레일이 다 내려가면 감춘다. 그전에 감추면 칠해지기 직전에 레일이 뚝 사라진다.
	if (RailLingerUntil > 0.f && GetWorld()->GetTimeSeconds() > RailLingerUntil)
	{
		HideRail();
	}
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

	TickChargeAnimation();

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

	CollectCellRun(ChargeAnchorCell, bChargeForward, GetChargeRunCount(Level), OutCells);
}

int32 ACWWorkZone_BorderSketch::GetChargeRunCount(int32 Level) const
{
	if (Level <= 0 || ChargeRunCellCounts.Num() == 0)
	{
		return 0;
	}

	// 단계가 배열보다 많으면 마지막 값을 계속 쓴다.
	return FMath::Max(ChargeRunCellCounts[FMath::Clamp(Level - 1, 0, ChargeRunCellCounts.Num() - 1)], 1);
}

void ACWWorkZone_BorderSketch::CollectCellRun(int32 AnchorCell, bool bForward, int32 Count,
	TArray<int32>& OutCells) const
{
	OutCells.Reset();

	if (Count <= 0 || !Cells.IsValidIndex(AnchorCell))
	{
		return;
	}

	// 칸이 단계보다 적은 아주 작은 존이면 한 바퀴가 전부다.
	// 여기서 자르지 않으면 고리를 넘어가 같은 칸이 두 번 들어온다.
	const int32 TargetCount = FMath::Min(Count, Cells.Num());
	const int32 Step = bForward ? 1 : -1;

	// 시작 칸에서 테두리 고리를 따라 한 줄로 이어 간다. 캐릭터에서 가까운 순으로 쌓이므로
	// 페인트가 모자라 끊겨도 앞에서부터 칠해지고, 붓질이 지나가는 순서도 그대로다.
	for (int32 Offset = 0; Offset < TargetCount; ++Offset)
	{
		OutCells.Add(WrapCellIndex(AnchorCell + Step * Offset));
	}
}

bool ACWWorkZone_BorderSketch::ComputeChargeForward(const UCWWorkerComponent* Worker, int32 AnchorCell) const
{
	const AActor* WorkerActor = (Worker != nullptr) ? Worker->GetOwner() : nullptr;
	if (WorkerActor == nullptr || !Cells.IsValidIndex(AnchorCell))
	{
		return true;
	}

	FVector LocalForward = GetActorTransform().InverseTransformVector(WorkerActor->GetActorForwardVector());
	LocalForward.Z = 0.f;

	// 접선과 거의 수직이면 내적이 0 근처라 부호가 애매하다. 그때는 인덱스가 늘어나는 쪽으로 둔다.
	return FVector::DotProduct(LocalForward, GetCellTangent(AnchorCell)) >= 0.f;
}

int32 ACWWorkZone_BorderSketch::FindNearestCell(const FVector& WorldLocation) const
{
	if (Cells.Num() == 0)
	{
		return INDEX_NONE;
	}

	const FVector LocalLocation = GetActorTransform().InverseTransformPosition(WorldLocation);

	// 높이는 보지 않는다. 캐릭터 발밑이든 허리 높이든 같은 칸이어야 한다.
	int32 NearestIndex = INDEX_NONE;
	float NearestDistanceSquared = TNumericLimits<float>::Max();

	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		const FVector Offset = Cells[CellIndex].Center - LocalLocation;
		const float DistanceSquared = Offset.X * Offset.X + Offset.Y * Offset.Y;
		if (DistanceSquared < NearestDistanceSquared)
		{
			NearestDistanceSquared = DistanceSquared;
			NearestIndex = CellIndex;
		}
	}

	return NearestIndex;
}

int32 ACWWorkZone_BorderSketch::FindSnappedAnchor(int32 AnchorCell, bool bForward) const
{
	if (!Cells.IsValidIndex(AnchorCell))
	{
		return AnchorCell;
	}

	// 시작 칸이 이미 칠해져 있으면 진행 방향으로 조금 훑어 빈 칸을 찾는다.
	// 훑어본 범위가 전부 칠해져 있으면 원래 칸을 그대로 둔다. 그 릴리즈는 헛손질이 된다.
	const int32 Step = bForward ? 1 : -1;
	const int32 SearchCount = FMath::Min(AnchorSnapCells, Cells.Num() - 1);

	for (int32 Offset = 0; Offset <= SearchCount; ++Offset)
	{
		const int32 CandidateCell = WrapCellIndex(AnchorCell + Step * Offset);
		if (!IsCellPainted(CandidateCell))
		{
			return CandidateCell;
		}
	}

	return AnchorCell;
}

FVector ACWWorkZone_BorderSketch::GetCellTangent(int32 CellIndex) const
{
	if (Cells.Num() < 2 || !Cells.IsValidIndex(CellIndex))
	{
		return FVector::ZeroVector;
	}

	// 다음 칸을 향하는 방향. 모서리 칸에서는 대각선이 되지만 부호는 여전히 맞다.
	FVector Tangent = Cells[WrapCellIndex(CellIndex + 1)].Center - Cells[CellIndex].Center;
	Tangent.Z = 0.f;

	return Tangent.GetSafeNormal();
}

int32 ACWWorkZone_BorderSketch::WrapCellIndex(int32 CellIndex) const
{
	if (Cells.Num() == 0)
	{
		return INDEX_NONE;
	}

	// % 는 음수를 음수로 내므로 한 번 더 더해서 올린다.
	return ((CellIndex % Cells.Num()) + Cells.Num()) % Cells.Num();
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

	// 시작 칸과 진행 방향은 차징이 시작될 때 한 번만 정하고 중간에 다시 보지 않는다.
	// 매 프레임 다시 정하면 접선과 거의 수직으로 서 있을 때 부호가 계속 뒤집혀 칠할 칸이 좌우로 튄다.
	//
	// 캐릭터 발밑에서 가장 가까운 칸이 시작점이고, 캐릭터가 바라보는 쪽으로 테두리를 따라
	// 한 줄로 이어 간다. 마주 보는 두 변에 동시에 걸치지 않는다.
	const AActor* const WorkerActor = Worker->GetOwner();
	const int32 NearestCell = FindNearestCell(WorkerActor != nullptr ? WorkerActor->GetActorLocation() : GetActorLocation());
	bChargeForward = ComputeChargeForward(Worker, NearestCell);
	ChargeAnchorCell = FindSnappedAnchor(NearestCell, bChargeForward);

	// 조준 프리뷰나 직전 붓질의 레일이 남아 있을 수 있다. 새 차징이 그 위에 겹치지 않게 먼저 걷는다.
	AimCells.Reset();
	HideRail();

	if (GetNetMode() != NM_DedicatedServer)
	{
		StartChargeAnimation(Worker);
		PlayLocalSound(ChargeStartSound);
		OnChargeStarted(Worker->GetHeldToolActor());
	}
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

	// 붓질 모션이 끝날 때까지 이동을 잠근다. 패링은 이 동안에도 된다.
	// 아래 애니메이션 처리가 실제 모션 길이에 맞춰 늘리거나 줄이므로 먼저 기본값을 넣어 둔다.
	ReleaseEndTime = GetWorld()->GetTimeSeconds() + ReleaseSeconds;

	// 칠할 것이 있으면 와인드업에서 풀어 휘두르고, 허탕이면 휘두르지 않고 자세를 거둔다.
	// 허탕에도 휘두르면 아무것도 안 칠해지는데 붓질 모션만 나가서 고장 난 것처럼 보인다.
	// 거둘 때는 몽타주를 바로 끊지 않고 당긴 길을 되감는다. 끊으면 반쯤 당긴 자세가 0.15초 만에
	// 제자리로 튀어서, 짧게 눌렀을 때만 모션이 고장 난 것처럼 보였다.
	if (Level > 0 && TargetCells.Num() > 0)
	{
		ReleaseChargeAnimation();
	}
	else
	{
		RewindChargeAnimation();
	}

	// 최소 홀드 미달(Level 0)은 소리를 내지 않는다. 눌렀다 바로 뗀 것까지 실패음을 내면 거슬린다.
	// 예측이 빗나가 서버가 뒤집을 수 있어도, 놓은 순간의 손맛이라 로컬 판단으로 바로 낸다.
	if (Level > 0)
	{
		PlayLocalSound(PaintedCount > 0 ? ReleaseSwingSound : DryReleaseSound);
	}

	ResetChargeState();

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

	StopChargeAnimation();
	ResetChargeState();

	// 릴리즈 모션을 걸지 않는다. 차에 치여 날아가는 중에 붓질 모션이 나오면 안 된다.
	ShowChargeDebug(TEXT("중단"), 0, 0);
}

void ACWWorkZone_BorderSketch::ResetChargeState()
{
	const bool bWasCharging = bCharging;

	bCharging = false;
	ChargeSeconds = 0.f;
	FullChargeElapsed = 0.f;
	LastReportedChargeLevel = 0;
	LastFxChargeLevel = 0;
	LastFxAffordableCount = 0;
	bHasChargeTip = false;
	ChargingWorker = nullptr;
	ChargeAnimInstance.Reset();
	ChargeMontage.Reset();
	bChargeAnimHeld = false;

	// 칠해지는 칸의 레일은 BeginRevealCells 가 이미 물마루로 내리게 했다. 남은 것(범위에는 들었지만
	// 칠해지지 않는 칸, 중단된 차징)만 조용히 내린다. 순서를 바꾸면 물마루가 지워진다.
	FinishRail();

	// 루프 이펙트를 끌 수 있게 놓았든 중단됐든 알린다.
	if (bWasCharging && GetNetMode() != NM_DedicatedServer)
	{
		OnChargeEnded();
	}
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

	PreviewBandMeshes->ClearInstances();
	PreviewBandCells.Reset();

	// 레일도 같은 이유로 버린다. 칸 번호가 바뀌면 서 있던 레일이 엉뚱한 자리를 가리킨다.
	RailMeshes->ClearInstances();
	RailCellActive.Reset();
	RailCellCollapsing.Reset();
	RailLingerUntil = -1.f;
	if (RailMeshes->NumCustomDataFloats != RailCustomDataCount)
	{
		RailMeshes->SetNumCustomDataFloats(RailCustomDataCount);
	}

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

		// 쓸림은 기본이 "이미 끝남" 이다. 붓질이 닿을 때만 BeginRevealCells 가 시각을 덮어쓴다.
		BorderMeshes->SetCustomDataValue(InstanceIndex, 2, InstantRevealStartTime, false);
		BorderMeshes->SetCustomDataValue(InstanceIndex, 3, MinSweepDuration, false);
		BorderMeshes->SetCustomDataValue(InstanceIndex, 4, Cell.RingDirectionSign, false);
	}

	BorderMeshes->MarkRenderStateDirty();

	// 레일 조각은 칸마다 미리 다 만들어 두고 전부 감춘 채로 시작한다.
	// 인스턴스를 차징 중에 더하고 빼면 매번 렌더 상태를 다시 올려야 하고, 값만 바꾸는 쪽이 가볍다.
	const int32 SliceCount = FMath::Max(RailSlicesPerCell, 1);
	RailCellActive.Init(0, Cells.Num());
	RailCellCollapsing.Init(0, Cells.Num());
	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		for (int32 SliceIndex = 0; SliceIndex < SliceCount; ++SliceIndex)
		{
			RailMeshes->AddInstance(MakeRailTransform(Cells[CellIndex], SliceIndex));
			SetRailSlice(CellIndex, SliceIndex, InstantRevealStartTime, 0.f, RailNeverCollapse, 0.f);
		}
	}

	RailMeshes->MarkRenderStateDirty();
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

	// 메시 U 는 X 축 변이면 존 +X, 세로 변이면 존 +Y 를 가리킨다. (MakeCellTransform)
	// 변이 그 반대로 진행하면 둘레를 도는 방향과 U 가 거꾸로다.
	const float RingDirectionSign = (bAlongX ? Direction.X : Direction.Y) >= 0.f ? 1.f : -1.f;

	for (int32 Index = 0; Index < CellCount; ++Index)
	{
		FCWBorderCell& Cell = Cells.AddDefaulted_GetRef();
		Cell.Center = Start + Direction * (ActualLength * (Index + 0.5f));
		Cell.Length = ActualLength;
		Cell.bAlongX = bAlongX;
		Cell.RingDirectionSign = RingDirectionSign;
	}
}

void ACWWorkZone_BorderSketch::UpdatePreview()
{
	UCWWorkerComponent* const Worker = FindLocalWorker();

	if (!bCharging)
	{
		bHasChargeTip = false;

		// 띠도 같이 비운다. 이걸 빼먹으면 띠가 마지막 모습 그대로 남아 방금 칠한 칸들이 덮여 보인다.
		UpdatePreviewBand(TArray<int32>(), 0);

		// 누르기 전에는 어느 칸부터 칠해지는지만 낮게 보여 준다.
		UpdateAimPreview(Worker);
		return;
	}

	if (!ShouldShowPreview(Worker))
	{
		bHasChargeTip = false;
		UpdatePreviewBand(TArray<int32>(), 0);
		return;
	}

	const int32 Level = GetChargeLevel();

	TArray<int32> Run;
	CollectCellRun(ChargeAnchorCell, bChargeForward, GetChargeRunCount(Level), Run);

	// 실제 릴리즈와 같은 함수로 거른다. 그래야 화면과 실제 결과가 갈리지 않는다.
	TArray<int32> AffordableCells;
	bool bPaintShort = false;
	FilterAffordableCells(Worker, Run, AffordableCells, bPaintShort);

	UpdatePreviewBand(Level > 0 ? AffordableCells : TArray<int32>(), Level);

	UpdateChargeFX(Level, AffordableCells);
}

void ACWWorkZone_BorderSketch::UpdatePreviewBand(const TArray<int32>& AffordableCells, int32 Level)
{
	// 목록이 그대로면 인스턴스를 다시 만들지 않는다. 바뀌는 것은 플레이어가 움직이거나
	// 단계가 오를 때뿐인데 이 함수는 매 프레임 불린다.
	if (PreviewBandCells == AffordableCells)
	{
		return;
	}

	PreviewBandCells = AffordableCells;

	// 차징 중에만 레일을 솟게 한다. 붓을 놓은 뒤에는 목록이 비지만 그때 레일은 무너지는 중이라
	// 여기서 건드리면 안 된다. (정리는 FinishRail / HideRail 이 맡는다)
	if (bCharging && Level > 0)
	{
		UpdateRail(static_cast<float>(Level), AffordableCells);
	}

	PreviewBandMeshes->ClearInstances();
	for (const int32 CellIndex : PreviewBandCells)
	{
		if (Cells.IsValidIndex(CellIndex))
		{
			PreviewBandMeshes->AddInstance(MakeBandTransform(Cells[CellIndex]));
		}
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

		// 붓이 지나간 순서 그대로 쓴다. 여기서 순서를 잃으면 쓸림이 붓질로 안 읽힌다.
		BeginRevealCells(AffordableCells);

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

	BeginRevealCells(AffordableCells);

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

void ACWWorkZone_BorderSketch::BeginRevealCells(const TArray<int32>& OrderedCells)
{
	if (RevealedCellFlags.Num() != Cells.Num())
	{
		RevealedCellFlags.SetNumZeroed(Cells.Num());
	}

	// 아직 켜지지 않은 칠해진 칸만 추린다. 순서는 붓이 지나간 순서 그대로 둔다.
	TArray<int32> NewCells;
	for (const int32 CellIndex : OrderedCells)
	{
		if (!Cells.IsValidIndex(CellIndex) || !IsCellPainted(CellIndex))
		{
			continue;
		}

		if (RevealedCellFlags[CellIndex] != 0)
		{
			continue;
		}

		NewCells.AddUnique(CellIndex);
	}

	if (NewCells.Num() == 0)
	{
		return;
	}

	/**
	 * 붓질 한 번으로 나올 수 없는 양이면 연출 없이 즉시 칠한다.
	 *
	 * 늦게 들어와 존 전체를 한꺼번에 받는 경우가 이쪽이다. 그걸 쓸어 칠하면 남이 이미
	 * 오래전에 칠해둔 선이 내 눈앞에서 그려지는 것처럼 보인다.
	 *
	 * 한도는 최대 단계가 잇는 칸 수다. 붓질은 테두리를 따라 한 줄로 이어지므로 그것이 곧 한 번의
	 * 최대치이고, 예측과 서버 확정이 같은 칸을 한 번씩 더 걸칠 수 있어 약간 여유를 둔다.
	 */
	int32 MaxRunCells = 1;
	for (const int32 RunCells : ChargeRunCellCounts)
	{
		MaxRunCells = FMath::Max(MaxRunCells, RunCells);
	}

	const int32 MaxStrokeCells = MaxRunCells + 2;
	const bool bSweep = StrokeSweepSpeed > 0.f && NewCells.Num() <= MaxStrokeCells;

	const float Now = GetWorld()->GetTimeSeconds();

	// 시작 시각은 붓이 지나온 **선을 따라간 거리**로 정한다. 직선거리로 정하면 모서리를 돌아 반대편
	// 긴 변까지 이어진 줄(U자)에서 위아래 줄이 거의 같은 시점에 동시에 칠해져, 한 줄로 이어진 선이
	// 아니라 두 줄이 한꺼번에 차오르는 것처럼 보인다.
	float PathDistance = 0.f;
	float SweepElapsed = 0.f;

	TArray<FVector> StrokeLocations;
	TArray<float> StartDelays;
	StrokeLocations.Reserve(NewCells.Num());
	StartDelays.Reserve(NewCells.Num());

	for (int32 OrderIndex = 0; OrderIndex < NewCells.Num(); ++OrderIndex)
	{
		const int32 CellIndex = NewCells[OrderIndex];
		RevealedCellFlags[CellIndex] = 1;

		if (!bSweep)
		{
			// 이전 붓질이 남긴 시각이 아직 최근일 수 있으니 "이미 끝남" 으로 되돌려 둔다.
			BorderMeshes->SetCustomDataValue(CellIndex, 2, InstantRevealStartTime, false);
			CollapseRailCell(CellIndex, Now, MinSweepDuration, 1.f, false);
			continue;
		}

		// 칸 하나를 선두가 지나가는 시간. 다음 칸은 정확히 이 시각에 시작한다.
		const float Duration = FMath::Max(Cells[CellIndex].Length / StrokeSweepSpeed, MinSweepDuration);

		// 머티리얼에는 이보다 (1 + SweepRaggedness) 배 긴 시간을 준다. 마른 붓 끝이 칸 안에서 그만큼
		// 늦게 완성되기 때문에, 같은 시간을 주면 선두가 칸 끝에 닿은 뒤에도 한참 기다렸다가 다음 칸이
		// 시작해서 칸마다 끊겨 보인다. 이렇게 맞추면 선두가 칸 끝에 닿는 순간 다음 칸이 이어받는다.
		const float SweepDuration = Duration * (1.f + SweepRaggedness);

		// 놓은 순간이 아니라 휘두르는 모션이 앞으로 나가는 순간에 페인트가 닿아야 한다.
		if (OrderIndex > 0)
		{
			PathDistance += FVector::Dist(Cells[CellIndex].Center, Cells[NewCells[OrderIndex - 1]].Center);
		}

		const float StartOffset = PathDistance / StrokeSweepSpeed + ReleaseSweepDelay;
		const float SweepSign = ComputeSweepSign(NewCells, OrderIndex);

		BorderMeshes->SetCustomDataValue(CellIndex, 2, Now + StartOffset, false);
		BorderMeshes->SetCustomDataValue(CellIndex, 3, SweepDuration, false);
		BorderMeshes->SetCustomDataValue(CellIndex, 4, SweepSign, false);

		// 레일은 붓이 닿는 순간 물마루가 되어 한 번 솟았다가 내려앉는다.
		CollapseRailCell(CellIndex, Now + StartOffset, Duration, SweepSign, true);

		SweepElapsed = FMath::Max(SweepElapsed, StartOffset + SweepDuration);
		StrokeLocations.Add(GetCellWorldLocation(CellIndex));
		StartDelays.Add(StartOffset);
	}

	RefreshPaintedInstances();

	// 무너지는 모션이 끝날 때까지 레일을 살려 둔다.
	RailLingerUntil = FMath::Max(RailLingerUntil, Now + SweepElapsed + RailCollapseSeconds + 0.3f);
	RailMeshes->MarkRenderStateDirty();

	// 데디케이티드 서버는 화면이 없으니 이펙트 훅을 부르지 않는다.
	if (bSweep && GetNetMode() != NM_DedicatedServer)
	{
		if (StrokeSweepSound != nullptr && StrokeLocations.Num() > 0)
		{
			// 쓸림이 끝나는 순간에 맞춰 잦아들게 한다. 한 칸짜리 붓질에 긴 소리가 다 나오면 결과보다 과하게 들린다.
			// 시간은 이 기계가 받은 칸 목록으로 계산하므로 서버와 다른 클라이언트에서도 같은 길이로 끝난다.
			UAudioComponent* const SweepAudio = UGameplayStatics::SpawnSoundAtLocation(this, StrokeSweepSound, StrokeLocations[0]);
			if (SweepAudio != nullptr)
			{
				FTimerHandle FadeHandle;
				const float FadeSeconds = StrokeSweepFadeOutSeconds;
				GetWorldTimerManager().SetTimer(FadeHandle, FTimerDelegate::CreateWeakLambda(SweepAudio, [SweepAudio, FadeSeconds]()
				{
					SweepAudio->FadeOut(FadeSeconds, 0.f);
				}), FMath::Max(SweepElapsed, 0.01f), false);
			}
		}

		// 칸마다 쓸림이 시작하는 순간 그 자리에서 한 번씩 낸다. 도미노가 넘어가듯 칸 수만큼 이어진다.
		if (StrokeCellSound != nullptr)
		{
			for (int32 OrderIndex = 0; OrderIndex < StrokeLocations.Num(); ++OrderIndex)
			{
				const FVector CellLocation = StrokeLocations[OrderIndex];
				const float Pitch = 1.f + StrokeCellPitchStep * OrderIndex;
				FTimerHandle CellHandle;
				GetWorldTimerManager().SetTimer(CellHandle, FTimerDelegate::CreateWeakLambda(this, [this, CellLocation, Pitch]()
				{
					UGameplayStatics::PlaySoundAtLocation(this, StrokeCellSound, CellLocation, StrokeCellVolume, Pitch);
				}), FMath::Max(StartDelays[OrderIndex], 0.001f), false);
			}
		}

		// StartDelays 는 머티리얼 쓸림과 같은 값이다. 이펙트도 이 시차로 터뜨려야 선과 어긋나지 않는다.
		OnStrokeSwept(StrokeLocations, StartDelays, SweepElapsed);
	}
}

void ACWWorkZone_BorderSketch::SyncRevealToFlags()
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

	// 아직 켜지지 않은 칸을 모은다. 남이 칠했거나 늦게 들어와 새로 알게 된 칸이다.
	TArray<int32> NewCells;
	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		if (IsCellPainted(CellIndex) && RevealedCellFlags[CellIndex] == 0)
		{
			NewCells.Add(CellIndex);
		}
	}

	BeginRevealCells(NewCells);
}

float ACWWorkZone_BorderSketch::ComputeSweepSign(const TArray<int32>& OrderedCells, int32 OrderIndex) const
{
	const FCWBorderCell& Cell = Cells[OrderedCells[OrderIndex]];

	// 붓이 고리를 따라 칸 인덱스가 늘어나는 쪽으로 가는가. 두 칸 이상이면 칸 번호가 이어지는 방향으로
	// 알 수 있다. 한 칸뿐이면 이동한 방향이 없으므로, 차징을 시작할 때 정해 둔 진행 방향을 쓴다.
	//
	// 한 칸짜리를 변의 기본 방향(RingDirectionSign)으로만 정하면, 바라보는 쪽이 그 반대일 때
	// 첫 칸이 오른쪽에서 왼쪽으로 거꾸로 솟고 쓸린다.
	bool bRunForward = bChargeForward;
	if (OrderedCells.Num() >= 2)
	{
		bRunForward = (OrderedCells[1] == WrapCellIndex(OrderedCells[0] + 1));
	}

	// 칸 인덱스가 늘어나는 쪽으로 가면 U 가 RingDirectionSign 방향으로 진행한다. 반대로 가면 거꾸로다.
	// 모든 칸이 같은 규칙이라 모서리를 돌아도 방향이 끊기지 않고 이어진다.
	return bRunForward ? Cell.RingDirectionSign : -Cell.RingDirectionSign;
}

void ACWWorkZone_BorderSketch::UpdateChargeFX(int32 Level, const TArray<int32>& AffordableCells)
{
	if (!bCharging || Level <= 0)
	{
		bHasChargeTip = false;
		return;
	}

	// 칠할 칸이 있으면 가장 먼 칸이 선두다. 칸은 캐릭터에서 가까운 순으로 정렬돼 있어 마지막이 그것이다.
	// 칠할 칸이 없으면(헛스윙) 시작 칸을 선두로 삼아 어디를 겨누는지는 계속 보여준다.
	if (AffordableCells.Num() > 0)
	{
		ChargeTipLocation = GetCellWorldLocation(AffordableCells.Last());
	}
	else
	{
		ChargeTipLocation = GetCellWorldLocation(ChargeAnchorCell);
	}

	bHasChargeTip = true;

	// 단계가 오른 프레임에만 알린다. 데디케이티드 서버는 화면이 없으니 부르지 않는다.
	if (Level <= LastFxChargeLevel)
	{
		return;
	}

	LastFxChargeLevel = Level;

	if (GetNetMode() != NM_DedicatedServer)
	{
		// 차징은 이 기계의 플레이어만 하므로 여기서 내는 소리는 본인에게만 들린다.
		// 실제로 칠할 칸이 늘어난 단계에서만 낸다. 페인트가 모자라거나 앞이 이미 칠해져 있으면
		// 단계가 올라도 결과가 그대로인데, 소리만 계속 올라가면 더 칠해지는 것처럼 들린다.
		if (AffordableCells.Num() > LastFxAffordableCount)
		{
			LastFxAffordableCount = AffordableCells.Num();
			PlayChargeLevelSound(Level);
		}

		OnChargeLevelReached(Level, ChargeThresholds.Num(), ChargeTipLocation, AffordableCells.Num() > 0);
	}
}

float ACWWorkZone_BorderSketch::GetChargeAlpha() const
{
	if (!bCharging || ChargeThresholds.Num() == 0)
	{
		return 0.f;
	}

	return FMath::Clamp(ChargeSeconds / FMath::Max(ChargeThresholds.Last(), KINDA_SMALL_NUMBER), 0.f, 1.f);
}

bool ACWWorkZone_BorderSketch::GetChargeTipLocation(FVector& OutTipLocation) const
{
	OutTipLocation = ChargeTipLocation;
	return bHasChargeTip;
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
	PreviewBandMaterial = PreviewBandMeshes->CreateAndSetMaterialInstanceDynamic(0);
}

void ACWWorkZone_BorderSketch::OnRep_PaintedCells()
{
	// 예측이 빗나간 칸은 여기서 꺼지고, 남이 칠한 칸은 쓸려 칠해진다.
	SyncRevealToFlags();

	RefreshPaintedInstances();
}

FTransform ACWWorkZone_BorderSketch::MakeRailTransform(const FCWBorderCell& Cell, int32 SliceIndex) const
{
	const int32 SliceCount = FMath::Max(RailSlicesPerCell, 1);
	const float SliceLength = Cell.Length / SliceCount;

	// 조각 중심은 칸 중심에서 칸이 누운 방향으로 어긋난다. 칸 안에서 조각이 빈틈없이 이어진다.
	const float Offset = ((SliceIndex + 0.5f) / SliceCount - 0.5f) * Cell.Length;
	FVector Location = Cell.Center + (Cell.bAlongX ? FVector(Offset, 0.f, 0.f) : FVector(0.f, Offset, 0.f));

	// 큐브 원점이 가운데라서 높이의 절반만큼 올려야 밑면이 바닥에 닿는다.
	// 머티리얼은 윗면만 끌어내려 높이를 줄이므로 밑면은 늘 이 자리에 있다.
	Location.Z += GuideHeightOffset + PreviewBoxHeightOffset + RailHeight * 0.5f;

	// 세로 변 칸은 칸 메시와 같이 90도 돌린다. 그래야 모든 조각에서 메시의 X 가 선을 따라간다.
	const FRotator Rotation = Cell.bAlongX ? FRotator::ZeroRotator : FRotator(0.f, 90.f, 0.f);

	// 조각 사이에 틈이 생기지 않도록 길이를 2% 겹친다.
	const float Width = FMath::Max(CellThickness * RailWidthScale, 1.f);
	const FVector Scale(SliceLength * 1.02f / 100.f, Width / 100.f, RailHeight / 100.f);

	return FTransform(Rotation, Location, Scale);
}

void ACWWorkZone_BorderSketch::SetRailSlice(int32 CellIndex, int32 SliceIndex, float RiseStart, float Tier,
	float CollapseStart, float Crest)
{
	const int32 InstanceIndex = CellIndex * FMath::Max(RailSlicesPerCell, 1) + SliceIndex;
	if (InstanceIndex < 0 || InstanceIndex >= RailMeshes->GetInstanceCount())
	{
		return;
	}

	// 마지막 인자가 false 인 이유: 조각마다 렌더 상태를 더럽히면 한 번의 갱신에 수십 번 다시 올린다.
	// 호출한 쪽이 끝난 뒤 MarkRenderStateDirty 를 한 번 부른다.
	RailMeshes->SetCustomDataValue(InstanceIndex, 0, RiseStart, false);
	RailMeshes->SetCustomDataValue(InstanceIndex, 1, Tier, false);
	RailMeshes->SetCustomDataValue(InstanceIndex, 2, CollapseStart, false);
	RailMeshes->SetCustomDataValue(InstanceIndex, 3, Crest, false);

	// 머티리얼이 높이를 줄일 때 쓰는 기준. 큐브 스케일에 이미 들어 있는 값이라 따로 맞출 필요가 없다.
	RailMeshes->SetCustomDataValue(InstanceIndex, 4, RailHeight, false);
}

void ACWWorkZone_BorderSketch::UpdateRail(float Tier, const TArray<int32>& TargetCells)
{
	if (RailCellActive.Num() != Cells.Num())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	const int32 SliceCount = FMath::Max(RailSlicesPerCell, 1);
	const TSet<int32> TargetSet(TargetCells);

	// 새로 닿은 칸만 솟게 한다. 이미 서 있는 칸은 그대로 두므로 단계가 오를 때 새 구간만 솟는다.
	//
	// 솟기 시작하는 시각은 선을 따라간 거리로 정한다. 새로 닿은 칸이 전부 같은 시각에 시작하면
	// 1번칸, 2번칸이 각자 왼쪽 끝에서 동시에 솟아서 한 줄로 이어진 선이 아니라 토막이 따로 자라는
	// 것처럼 보인다. 이렇게 하면 칸 안에서도, 칸과 칸 사이에서도 한 방향으로 쭉 이어서 솟는다.
	float PathDistance = 0.f;
	int32 PreviousNewCell = INDEX_NONE;
	for (int32 Order = 0; Order < TargetCells.Num(); ++Order)
	{
		const int32 CellIndex = TargetCells[Order];
		if (!RailCellActive.IsValidIndex(CellIndex) || RailCellActive[CellIndex] != 0)
		{
			continue;
		}

		if (PreviousNewCell != INDEX_NONE)
		{
			PathDistance += FVector::Dist(Cells[CellIndex].Center, Cells[PreviousNewCell].Center);
		}

		PreviousNewCell = CellIndex;

		const float SweepSign = ComputeSweepSign(TargetCells, Order);
		const float SliceLength = Cells[CellIndex].Length / SliceCount;

		for (int32 SliceIndex = 0; SliceIndex < SliceCount; ++SliceIndex)
		{
			// 붓이 들어오는 쪽 조각부터 솟는다.
			const int32 SliceOrder = SweepSign >= 0.f ? SliceIndex : SliceCount - 1 - SliceIndex;
			const float RiseStart = Now + (PathDistance + SliceOrder * SliceLength) / FMath::Max(RailRiseSpeed, 1.f);

			// 조준 프리뷰는 칸마다 방향을 단계 값에 실어 보낸다. 흐르는 줄무늬가 칠해지는 방향을 가리키게 하려는 것이다.
			const float CellTier = (Tier < 0.5f && SweepSign < 0.f) ? Tier + AimRailReverseOffset : Tier;

			SetRailSlice(CellIndex, SliceIndex, RiseStart, CellTier, RailNeverCollapse, 0.f);
		}

		RailCellActive[CellIndex] = 1;
		RailCellCollapsing[CellIndex] = 0;
	}

	// 범위에서 빠진 칸은 감춘다. 차징 중에는 범위가 얼어 있어 거의 없는 경우다.
	for (int32 CellIndex = 0; CellIndex < RailCellActive.Num(); ++CellIndex)
	{
		if (RailCellActive[CellIndex] == 0 || RailCellCollapsing[CellIndex] != 0 || TargetSet.Contains(CellIndex))
		{
			continue;
		}

		for (int32 SliceIndex = 0; SliceIndex < SliceCount; ++SliceIndex)
		{
			SetRailSlice(CellIndex, SliceIndex, InstantRevealStartTime, 0.f, RailNeverCollapse, 0.f);
		}

		RailCellActive[CellIndex] = 0;
	}

	RailLingerUntil = -1.f;
	RailMeshes->MarkRenderStateDirty();
}

void ACWWorkZone_BorderSketch::CollapseRailCell(int32 CellIndex, float StartTime, float Duration, float SweepSign,
	bool bCrest)
{
	if (!RailCellActive.IsValidIndex(CellIndex) || RailCellActive[CellIndex] == 0 || RailCellCollapsing[CellIndex] != 0)
	{
		return;
	}

	const int32 SliceCount = FMath::Max(RailSlicesPerCell, 1);
	for (int32 SliceIndex = 0; SliceIndex < SliceCount; ++SliceIndex)
	{
		// 붓이 지나가는 쪽 조각부터 무너진다. 칸 안에서도 물마루가 이어져 달려간다.
		const float Order = SweepSign >= 0.f ? static_cast<float>(SliceIndex) : static_cast<float>(SliceCount - 1 - SliceIndex);
		const float SliceStart = StartTime + Duration * Order / SliceCount;

		const int32 InstanceIndex = CellIndex * SliceCount + SliceIndex;
		RailMeshes->SetCustomDataValue(InstanceIndex, 2, SliceStart, false);
		RailMeshes->SetCustomDataValue(InstanceIndex, 3, bCrest ? 1.f : 0.f, false);
	}

	RailCellCollapsing[CellIndex] = 1;
}

void ACWWorkZone_BorderSketch::FinishRail()
{
	if (RailCellActive.Num() != Cells.Num())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	bool bAny = false;

	for (int32 CellIndex = 0; CellIndex < RailCellActive.Num(); ++CellIndex)
	{
		if (RailCellActive[CellIndex] != 0 && RailCellCollapsing[CellIndex] == 0)
		{
			CollapseRailCell(CellIndex, Now, 0.f, 1.f, false);
			bAny = true;
		}
	}

	if (bAny)
	{
		RailLingerUntil = FMath::Max(RailLingerUntil, Now + RailCollapseSeconds + 0.2f);
		RailMeshes->MarkRenderStateDirty();
	}
}

void ACWWorkZone_BorderSketch::HideRail()
{
	RailLingerUntil = -1.f;

	const int32 SliceCount = FMath::Max(RailSlicesPerCell, 1);
	bool bAny = false;

	for (int32 CellIndex = 0; CellIndex < RailCellActive.Num(); ++CellIndex)
	{
		if (RailCellActive[CellIndex] == 0)
		{
			continue;
		}

		for (int32 SliceIndex = 0; SliceIndex < SliceCount; ++SliceIndex)
		{
			SetRailSlice(CellIndex, SliceIndex, InstantRevealStartTime, 0.f, RailNeverCollapse, 0.f);
		}

		RailCellActive[CellIndex] = 0;
		RailCellCollapsing[CellIndex] = 0;
		bAny = true;
	}

	if (bAny)
	{
		RailMeshes->MarkRenderStateDirty();
	}
}

void ACWWorkZone_BorderSketch::StartChargeAnimation(UCWWorkerComponent* Worker)
{
	if (ChargeAnimation == nullptr || Worker == nullptr)
	{
		return;
	}

	const ACharacter* const Character = Cast<ACharacter>(Worker->GetOwner());
	USkeletalMeshComponent* const Mesh = Character != nullptr ? Character->GetMesh() : nullptr;
	UAnimInstance* const AnimInstance = Mesh != nullptr ? Mesh->GetAnimInstance() : nullptr;
	if (AnimInstance == nullptr)
	{
		return;
	}

	const float WindupRate = GetChargeWindupRate();

	// 블렌드 아웃 트리거를 0(끝에 닿은 뒤)으로 둔다. 기본값(-1)이면 끝까지 남은 실제 재생 시간이 블렌드 아웃
	// 시간(0.15초) 아래로 내려가는 순간 풀리기 시작하는데, 빠른 와인드업 배율에서는 홀드 자세에 닿기 전에,
	// 놓은 뒤 빠른 스윙에서는 휘두르자마자 풀려 버린다. 풀리면 Montage_IsPlaying 이 false 라 홀드·해제도 안 먹는다.
	UAnimMontage* const Montage = AnimInstance->PlaySlotAnimationAsDynamicMontage(
		ChargeAnimation, ChargeAnimSlot, 0.1f, 0.15f, WindupRate, 1, 0.f, 0.f);

	ChargeAnimInstance = AnimInstance;
	ChargeMontage = Montage;
	bChargeAnimHeld = false;

	// 직전 붓질의 휘두르기·되감기는 새 몽타주가 덮었으니 버린다.
	ClearReleaseAnimation();
}

float ACWWorkZone_BorderSketch::GetChargeWindupRate() const
{
	// 원본 속도로 두면 자세가 잡히는 데 1초 가까이 걸려서, 최소 홀드(첫 단계)에 닿을 때까지 몸이 아직 움직이는 중이다.
	return ChargeAnimHoldTime / FMath::Max(ChargeAnimWindupSeconds, 0.05f);
}

void ACWWorkZone_BorderSketch::TickChargeAnimation()
{
	UAnimInstance* const AnimInstance = ChargeAnimInstance.Get();
	UAnimMontage* const Montage = ChargeMontage.Get();
	if (AnimInstance == nullptr || Montage == nullptr || bChargeAnimHeld)
	{
		return;
	}

	// 맞은 뒤 몽타주가 끊겼으면 더 만질 것이 없다. 차징 자체는 IsChargingWorkerEligible 이 따로 끊는다.
	if (!AnimInstance->Montage_IsActive(Montage))
	{
		return;
	}

	// 와인드업 끝 자세에 닿으면 일시정지해 그 자세에서 얼린다. 놓을 때 여기서부터 풀린다.
	//
	// 배율 0 으로 얼리면 안 된다. 엔진은 재생 중인 몽타주의 흐른 시간을 배율과 상관없이 최대 10초까지
	// 쌓아 두었다가, 배율이 돌아오는 첫 프레임에 한꺼번에 소비한다. 그래서 홀드한 시간만큼 휘두르기가
	// 건너뛰어졌고, 남은 길이보다 오래 누르면(풀차징 자동 베기 포함) 놓자마자 몽타주가 끝나 피니시가 안 보였다.
	// 일시정지 중에는 시간이 쌓이지 않는다. (FAnimMontageInstance::Advance)
	if (AnimInstance->Montage_GetPosition(Montage) >= ChargeAnimHoldTime)
	{
		AnimInstance->Montage_SetPosition(Montage, ChargeAnimHoldTime);
		AnimInstance->Montage_Pause(Montage);
		bChargeAnimHeld = true;
	}
}

void ACWWorkZone_BorderSketch::ReleaseChargeAnimation()
{
	UAnimInstance* const AnimInstance = ChargeAnimInstance.Get();
	UAnimMontage* const Montage = ChargeMontage.Get();
	// 홀드 자세에서는 일시정지라 Montage_IsPlaying 이 false 다. 살아 있는지는 IsActive 로 본다.
	if (AnimInstance == nullptr || Montage == nullptr || !AnimInstance->Montage_IsActive(Montage))
	{
		return;
	}

	bChargeAnimHeld = true;
	ReleaseAnimInstance = AnimInstance;
	ReleaseMontage = Montage;

	// 와인드업 도중에 놓았으면 홀드 자세까지는 와인드업 배율로 마저 당긴 뒤 휘두른다.
	// 놓은 자리에서 바로 릴리즈 배율로 바꾸면 남은 당기기까지 릴리즈 배율로 재생돼서,
	// 짧게 누른 것과 길게 누른 것의 시작 자세와 길이가 매번 달라졌다.
	if (AnimInstance->Montage_GetPosition(Montage) < ChargeAnimHoldTime)
	{
		bReleaseSwingPending = true;
		return;
	}

	BeginReleaseSwing(AnimInstance, Montage);
}

void ACWWorkZone_BorderSketch::RewindChargeAnimation()
{
	UAnimInstance* const AnimInstance = ChargeAnimInstance.Get();
	UAnimMontage* const Montage = ChargeMontage.Get();
	if (AnimInstance == nullptr || Montage == nullptr || !AnimInstance->Montage_IsActive(Montage))
	{
		return;
	}

	bChargeAnimHeld = true;
	ReleaseAnimInstance = AnimInstance;
	ReleaseMontage = Montage;
	bReleaseRewinding = true;

	// 당길 때와 같은 속도로 거꾸로 돌린다. 시작 자세가 평소 자세라 다 돌면 그대로 이어진다.
	// 홀드에서 일시정지돼 있었을 수 있으니 먼저 푼다.
	const float WindupRate = GetChargeWindupRate();
	AnimInstance->Montage_Resume(Montage);
	AnimInstance->Montage_SetPlayRate(Montage, -WindupRate);

	// 거두는 동안만 잠근다. 휘두르지 않았는데 휘두르기 길이만큼 묶어 두면 답답하다.
	const float RewindSeconds = AnimInstance->Montage_GetPosition(Montage) / WindupRate;
	ReleaseEndTime = GetWorld()->GetTimeSeconds() + RewindSeconds;
}

void ACWWorkZone_BorderSketch::TickReleaseAnimation()
{
	UAnimInstance* const AnimInstance = ReleaseAnimInstance.Get();
	UAnimMontage* const Montage = ReleaseMontage.Get();
	if (AnimInstance == nullptr || Montage == nullptr)
	{
		return;
	}

	// 맞거나 다른 몽타주에 밀렸거나 끝까지 다 돌았으면 더 돌볼 것이 없다.
	if (!AnimInstance->Montage_IsActive(Montage))
	{
		ClearReleaseAnimation();
		return;
	}

	const float Position = AnimInstance->Montage_GetPosition(Montage);

	if (bReleaseSwingPending)
	{
		if (Position >= ChargeAnimHoldTime)
		{
			// 프레임 간격만큼 지나쳤을 수 있으니 홀드 자세로 맞춘다. 누른 시간과 상관없이 같은 자세에서 나가게 한다.
			AnimInstance->Montage_SetPosition(Montage, ChargeAnimHoldTime);
			bReleaseSwingPending = false;
			BeginReleaseSwing(AnimInstance, Montage);
		}

		return;
	}

	// 되감기가 처음 자세에 닿으면 몽타주를 놓는다. 거꾸로 돌 때 시작점에서 저절로 끝난다는 보장이 없다.
	if (bReleaseRewinding && Position <= KINDA_SMALL_NUMBER)
	{
		AnimInstance->Montage_Stop(0.1f, Montage);
		ClearReleaseAnimation();
		return;
	}

	// 잠금이 풀린 뒤 실제로 걷기 시작하면 피니시를 끊는다. 몽타주가 전신 슬롯이라 그대로 두면
	// 다리까지 피니시 자세로 굳은 채 미끄러진다. 입력이 아니라 속도를 보는 이유는, 커서가 존을 벗어나
	// 작업 대상이 바뀌어도 이동 입력이 이 존까지 오지 않기 때문이다.
	if (GetWorld()->GetTimeSeconds() < ReleaseEndTime)
	{
		return;
	}

	const AActor* const OwningActor = AnimInstance->GetOwningActor();
	if (OwningActor != nullptr && OwningActor->GetVelocity().SizeSquared2D() > FMath::Square(10.f))
	{
		AnimInstance->Montage_Stop(0.2f, Montage);
		ClearReleaseAnimation();
	}
}

void ACWWorkZone_BorderSketch::ClearReleaseAnimation()
{
	ReleaseAnimInstance.Reset();
	ReleaseMontage.Reset();
	bReleaseSwingPending = false;
	bReleaseRewinding = false;
}

void ACWWorkZone_BorderSketch::BeginReleaseSwing(UAnimInstance* AnimInstance, UAnimMontage* Montage)
{
	// 홀드에서 일시정지해 둔 몽타주를 푼다. 와인드업 도중에 놓아 미뤄진 경우는 이미 재생 중이라 그대로다.
	AnimInstance->Montage_Resume(Montage);
	AnimInstance->Montage_SetPlayRate(Montage, ReleaseAnimPlayRate);
	StartReleaseTrail(AnimInstance);

	// 이동 잠금은 휘두르기가 실제로 시작된 순간부터 센다. 와인드업 도중에 놓아 휘두르기가 미뤄졌으면
	// 놓은 순간부터 세던 잠금이 휘두르는 도중에 풀려 버린다.
	ReleaseEndTime = FMath::Max(ReleaseEndTime, GetWorld()->GetTimeSeconds() + ReleaseSeconds);
}

void ACWWorkZone_BorderSketch::StartReleaseTrail(UAnimInstance* AnimInstance)
{
	USkeletalMeshComponent* const Mesh = AnimInstance->GetSkelMeshComponent();
	if (ReleaseTrailSystem == nullptr || Mesh == nullptr)
	{
		return;
	}

	// 연달아 휘둘러 이전 궤적이 남아 있으면 먼저 끈다. 남은 리본은 수명대로 사라진다.
	if (UNiagaraComponent* const PreviousTrail = ReleaseTrail.Get())
	{
		PreviousTrail->Deactivate();
	}

	UNiagaraComponent* const Trail = UNiagaraFunctionLibrary::SpawnSystemAttached(
		ReleaseTrailSystem, Mesh, ReleaseTrailSocket, ReleaseTrailOffset, FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset, true);
	ReleaseTrail = Trail;

	// 휘두르는 구간이 끝나면 새 리본을 그만 낸다. 배율이 바뀌어도 같은 구간을 덮도록 실제 시간으로 바꾼다.
	const float TrailSeconds = ReleaseTrailClipSeconds / FMath::Max(ReleaseAnimPlayRate, 0.1f);
	GetWorldTimerManager().SetTimer(ReleaseTrailTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (UNiagaraComponent* const ActiveTrail = ReleaseTrail.Get())
		{
			ActiveTrail->Deactivate();
		}
	}), TrailSeconds, false);
}

void ACWWorkZone_BorderSketch::StopChargeAnimation()
{
	UAnimInstance* const AnimInstance = ChargeAnimInstance.Get();
	UAnimMontage* const Montage = ChargeMontage.Get();
	// 홀드 중(일시정지)에 맞아도 끊어야 하므로 IsPlaying 이 아니라 IsActive 로 본다.
	if (AnimInstance != nullptr && Montage != nullptr && AnimInstance->Montage_IsActive(Montage))
	{
		AnimInstance->Montage_Stop(0.15f, Montage);
	}
}

void ACWWorkZone_BorderSketch::UpdateAimPreview(const UCWWorkerComponent* Worker)
{
	// 직전 붓질의 레일이 무너지는 중이면 건드리지 않는다. 다 내려간 뒤에 다시 띄운다.
	if (RailLingerUntil >= 0.f)
	{
		return;
	}

	if (Worker == nullptr || !ShouldShowPreview(Worker))
	{
		if (AimCells.Num() > 0)
		{
			AimCells.Reset();
			HideRail();
		}

		return;
	}

	// 시작 칸과 방향은 차징을 시작할 때와 같은 규칙이다. 이 값은 BeginCharge 가 다시 덮어쓴다.
	const AActor* const WorkerActor = Worker->GetOwner();
	const int32 NearestCell = FindNearestCell(WorkerActor != nullptr ? WorkerActor->GetActorLocation() : GetActorLocation());
	bChargeForward = ComputeChargeForward(Worker, NearestCell);
	ChargeAnchorCell = FindSnappedAnchor(NearestCell, bChargeForward);

	// 1단계로 칠해질 칸만 보여 준다. 길이는 차징하면서 정해지므로 미리 길게 보여 주면 헷갈린다.
	TArray<int32> Run;
	CollectCellRun(ChargeAnchorCell, bChargeForward, GetChargeRunCount(1), Run);

	TArray<int32> AffordableCells;
	bool bPaintShort = false;
	FilterAffordableCells(Worker, Run, AffordableCells, bPaintShort);

	// 칸은 그대로인데 바라보는 방향만 바뀌는 경우도 다시 그려야 한다. 한 칸짜리 프리뷰에서는
	// 시작 칸이 같고 방향만 뒤집히므로, 칸 목록만 비교하면 꺾쇠가 영영 안 바뀐다.
	const bool bSameCells = (AffordableCells == AimCells);
	if (bSameCells && bChargeForward == bAimForward)
	{
		return;
	}

	bAimForward = bChargeForward;
	AimCells = AffordableCells;

	if (AimCells.Num() == 0)
	{
		HideRail();
		return;
	}

	if (bSameCells)
	{
		// 방향만 바뀌었다. 레일을 다시 솟게 하면 바라보는 방향이 흔들릴 때마다 깜빡이므로,
		// 서 있는 레일의 단계 값(방향이 실려 있다)만 그 자리에서 갈아 끼운다.
		const int32 SliceCount = FMath::Max(RailSlicesPerCell, 1);
		for (int32 Order = 0; Order < AimCells.Num(); ++Order)
		{
			const float SweepSign = ComputeSweepSign(AimCells, Order);
			const float CellTier = SweepSign < 0.f ? AimRailTier + AimRailReverseOffset : AimRailTier;

			for (int32 SliceIndex = 0; SliceIndex < SliceCount; ++SliceIndex)
			{
				RailMeshes->SetCustomDataValue(AimCells[Order] * SliceCount + SliceIndex, 1, CellTier, false);
			}
		}

		RailMeshes->MarkRenderStateDirty();
		return;
	}

	UpdateRail(AimRailTier, AimCells);
}

void ACWWorkZone_BorderSketch::PlayChargeLevelSound(int32 Level) const
{
	if (Level <= 0 || ChargeLevelSounds.Num() == 0)
	{
		return;
	}

	// 소리가 단계 수보다 적으면 마지막 것을 계속 쓴다. 하나만 넣고 피치로 음계를 올려도 된다.
	USoundBase* const Sound = ChargeLevelSounds[FMath::Min(Level - 1, ChargeLevelSounds.Num() - 1)];
	if (Sound == nullptr)
	{
		return;
	}

	const float Pitch = ChargeLevelPitches.IsValidIndex(Level - 1) ? ChargeLevelPitches[Level - 1] : 1.f;
	const float Volume = ChargeLevelVolumes.IsValidIndex(Level - 1) ? ChargeLevelVolumes[Level - 1] : 1.f;

	UGameplayStatics::PlaySound2D(this, Sound, Volume, Pitch);
}

void ACWWorkZone_BorderSketch::PlayLocalSound(USoundBase* Sound, float Volume) const
{
	if (Sound == nullptr || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 같은 소리가 반복돼도 기계적으로 들리지 않게 피치를 살짝 흔든다.
	UGameplayStatics::PlaySound2D(this, Sound, Volume, FMath::FRandRange(0.95f, 1.05f));
}
