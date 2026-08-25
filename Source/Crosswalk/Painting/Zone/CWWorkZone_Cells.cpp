#include "Painting/Zone/CWWorkZone_Cells.h"

#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Painting/CWPaintingSubsystem.h"
#include "Painting/Components/CWWorkerComponent.h"
#include "Traffic/CWBaseVehicle.h"
#include "Traffic/CWTrafficSubsystem.h"

namespace
{
	// 런타임 오염 구역 판정 박스의 높이 절반(cm).
	// 오염은 평면으로만 판정하지만(ContainsGridPoint), 박스가 너무 납작하면
	// 에디터에서 눈에 안 띄고 경사면에서 캐릭터가 존을 못 잡는다.
	constexpr float SpillZoneAreaHalfHeight = 50.f;
}

ACWWorkZone_Cells::ACWWorkZone_Cells()
{
	// 남은 칸 디버그를 켰을 때만 BeginPlay 에서 실제로 돌린다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	CellMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("CellMeshes"));
	CellMeshes->SetupAttachment(Area);
	CellMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CellMeshes->SetCastShadow(false);

	// 인스턴스를 런타임에 추가/삭제하므로 Movable 이어야 한다.
	// 레벨에 배치한 액터는 루트가 Static 으로 잡히는데, 그대로 두면 칸을 칠해도 화면이 갱신되지 않는다.
	// (Static 부모 밑에 Movable 자식은 허용된다)
	CellMeshes->SetMobility(EComponentMobility::Movable);

	// 플레이스홀더. 전용 페인트 머티리얼이 나오기 전에도 칠한 자리가 눈에 보이게 한다.
	// Plane 은 100x100 이라 칸 크기를 100 으로 나누면 그대로 배율이 된다.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		CellMeshes->SetStaticMesh(PlaneMesh.Object);
	}

	AreaBorderMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("AreaBorderMeshes"));
	AreaBorderMeshes->SetupAttachment(Area);
	AreaBorderMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AreaBorderMeshes->SetCastShadow(false);
	AreaBorderMeshes->SetMobility(EComponentMobility::Movable);

	if (PlaneMesh.Succeeded())
	{
		AreaBorderMeshes->SetStaticMesh(PlaneMesh.Object);
	}

	// 칸마다 머티리얼이 읽을 값을 두 개 실어 보낸다. 0번은 칠해졌는지, 1번은 점수 영역인지.
	//
	// 격자는 BuildGrid 에서 한 번 전부 깔고, 이후에는 인스턴스를 추가하거나 지우지 않는다.
	// 붓질마다 ClearInstances + AddInstances 를 하면 칸 하나가 바뀌어도 인스턴스 버퍼를
	// 통째로 다시 만들어 올리게 되고, 많이 칠할수록 느려진다. 지금은 바뀐 칸만 건드린다.
	//
	// 화면에서 빈 칸을 감추는 것은 스케일 0 이 담당한다. (GetCellInstanceTransform)
	// 커스텀 데이터를 읽는 머티리얼이 아직 없어도 그림이 깨지지 않게 하기 위해서다.
	CellMeshes->NumCustomDataFloats = 2;
}

void ACWWorkZone_Cells::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 에디터에서 박스나 격자 수를 바꾸면 칸이 바로 보이게 한다.
	// 진행도는 여기서 건드리지 않는다. 에디터에서 값이 바뀌면 레벨이 더럽혀진다.
	ApplyCellMaterial();
	ResetCells();
	RebuildAllCellInstances();
	RefreshAreaBorder();

	// 디테일 패널에 칸 크기와 칸 수를 띄운다. 박스를 끌면 실시간으로 따라온다.
	// (ResetCells 안의 RebuildGridLayout 이 방금 다시 계산해 둔 값이다)
	CellSizePreview = CachedCellSize;
	CellCountPreview = CellFilled.Num();
}

void ACWWorkZone_Cells::RefreshAreaBorder()
{
	if (AreaBorderMeshes == nullptr)
	{
		return;
	}

	if (AreaBorderMaterial != nullptr)
	{
		AreaBorderMeshes->SetMaterial(0, AreaBorderMaterial);
	}

	AreaBorderMeshes->ClearInstances();

	if (!bShowAreaBorder)
	{
		return;
	}

	// 경계선의 기준은 Area 박스다. 이 선 안쪽이 진행도로 세는 영역이고, 바깥은 삐져나간 영역이다.
	// 선의 중심이 경계에 놓여 안팎으로 절반씩 걸친다. 칸 상태는 이 선과 무관하게
	// Area 박스 그대로 판정한다. 선은 눈으로 보는 기준일 뿐이다.
	const FVector AreaExtent = Area->GetUnscaledBoxExtent();

	// 네 변을 각각 얇고 긴 판으로 만든다. 모서리에서 겹치도록 길이에 두께를 더해 둔다.
	// 안 그러면 네 귀퉁이에 두께만큼 구멍이 뚫려 선이 끊겨 보인다.
	const FVector2D LongSideSize(AreaBorderThickness, AreaExtent.Y * 2.f + AreaBorderThickness);
	const FVector2D ShortSideSize(AreaExtent.X * 2.f + AreaBorderThickness, AreaBorderThickness);

	const TArray<TPair<FVector2D, FVector2D>> Edges =
	{
		{ FVector2D(AreaExtent.X, 0.f), LongSideSize },
		{ FVector2D(-AreaExtent.X, 0.f), LongSideSize },
		{ FVector2D(0.f, AreaExtent.Y), ShortSideSize },
		{ FVector2D(0.f, -AreaExtent.Y), ShortSideSize }
	};

	for (const TPair<FVector2D, FVector2D>& Edge : Edges)
	{
		FTransform InstanceTransform;
		InstanceTransform.SetLocation(FVector(Edge.Key.X, Edge.Key.Y, AreaBorderHeightOffset));

		// Plane 은 100x100 이라 원하는 크기를 100 으로 나누면 그대로 배율이 된다.
		InstanceTransform.SetScale3D(FVector(Edge.Value.X / 100.f, Edge.Value.Y / 100.f, 1.f));

		AreaBorderMeshes->AddInstance(InstanceTransform);
	}
}

void ACWWorkZone_Cells::ApplyCellMaterial()
{
	if (CellMaterial != nullptr)
	{
		CellMeshes->SetMaterial(0, CellMaterial);
	}
}

void ACWWorkZone_Cells::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACWWorkZone_Cells, CellSnapshot);
}

void ACWWorkZone_Cells::BeginPlay()
{
	Super::BeginPlay();

	ApplyCellMaterial();
	BuildGrid();
	RefreshAreaBorder();

	// 도중에 들어온 플레이어라면 격자를 만들기 전에 스냅샷이 먼저 도착해 있을 수 있다.
	// 그때는 OnRep 이 아무것도 못 하고 지나갔으므로 여기서 다시 시도한다.
	TryApplyCellSnapshot();

	// 틱은 할 일이 있을 때만 돈다. 시작 시점에 켤 이유는 디버그 표시뿐이다.
	RefreshTickEnabled();

	// 마스킹은 등록된 존 목록을 봐야 해서 다음 틱에 돈다.
	// 액터의 BeginPlay 순서는 보장되지 않아 지금 훑으면 아직 등록 전인 스트라이프를 놓친다.
	// 서버만이 아니라 모든 머신에서 한다. 각자 같은 격자를 그려야 화면이 맞는다.
	if (bPaintSpillLayer)
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ACWWorkZone_Cells::RefreshSpillMask);
	}

	// 페인트를 밟고 지나가는 차량 추적은 서버가 한다. 클라이언트가 칸을 늘리면 그림이 갈라진다.
	if (!HasAuthority() || !bStainsVehicles)
	{
		return;
	}

	if (VehicleStainZone == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] bStainsVehicles 가 켜져 있지만 VehicleStainZone 이 비어 있습니다."), *GetName());
		return;
	}

	// 채우기 모드 존에는 바깥 고리에만 찍는다. 고리가 없으면 자국이 남을 자리가 없다.
	if (VehicleStainZone->IsFillMode() && VehicleStainZone->GetOverspillMargin() <= 0.f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] 자국 대상 '%s' 는 채우기 모드인데 OverspillMargin 이 0 입니다. 자국이 남을 자리가 없습니다."),
			*GetName(), *VehicleStainZone->GetName());
		return;
	}

	SetVehicleStainTimerEnabled(true);
}

void ACWWorkZone_Cells::UpdateVehicleStains()
{
	UCWTrafficSubsystem* TrafficSubsystem = UCWTrafficSubsystem::Get(this);
	if (TrafficSubsystem == nullptr || VehicleStainZone == nullptr)
	{
		return;
	}

	// 1. 젖은 페인트를 밟고 있는 차량에 페인트를 묻힌다.
	//    이미 묻은 차가 다시 밟으면 시간이 갱신되어 자국이 더 길게 이어진다.
	//
	//    말랐으면 새로 묻지 않는다. 다만 이미 바퀴에 묻어 있던 차량은 아래에서
	//    남은 시간만큼 자국을 마저 남긴다. 마르는 순간 자국이 뚝 끊기면 어색하다.
	if (!bPaintDry)
	{
		TArray<ACWBaseVehicle*> NearbyVehicles;
		TrafficSubsystem->GetVehiclesNearPoint(GetActorLocation(), GetVehicleSearchRadius(), NearbyVehicles);

		for (ACWBaseVehicle* Vehicle : NearbyVehicles)
		{
			if (Vehicle != nullptr && IsFilledAt(Vehicle->GetActorLocation()))
			{
				StainedVehicles.Add(Vehicle, VehicleStainDuration);
			}
		}
	}

	// 2. 묻은 차량이 지나는 자리에 자국을 남긴다. 존 밖으로 나가도 시간이 남아 있으면 계속 남는다.
	//
	// 대상이 채우기 모드면 바깥 고리에만 찍는다. 점수 영역까지 채우면 차가 지나갈 때마다
	// 도색이 저절로 완성된다. 지우기 모드(정리 존)는 채워진 칸이 곧 치울 대상이라 그대로 찍는다.
	const bool bSpillOnly = VehicleStainZone->IsFillMode();

	for (TMap<TWeakObjectPtr<AActor>, float>::TIterator It(StainedVehicles); It; ++It)
	{
		const AActor* Vehicle = It.Key().Get();
		if (Vehicle == nullptr)
		{
			It.RemoveCurrent();
			continue;
		}

		VehicleStainZone->AddCellsAt(Vehicle->GetActorLocation(), VehicleStainRadius, bSpillOnly);

		It.Value() -= VehicleStainInterval;
		if (It.Value() <= 0.f)
		{
			It.RemoveCurrent();
		}
	}

	// 말랐고 마지막 자국까지 끝났으면 더 확인할 것이 없다. 타이머를 멈춘다.
	if (bPaintDry && StainedVehicles.Num() == 0)
	{
		SetVehicleStainTimerEnabled(false);
	}
}

void ACWWorkZone_Cells::SetVehicleStainTimerEnabled(bool bEnabled)
{
	if (!bEnabled)
	{
		GetWorldTimerManager().ClearTimer(VehicleStainTimerHandle);
		return;
	}

	if (VehicleStainTimerHandle.IsValid())
	{
		return;
	}

	GetWorldTimerManager().SetTimer(
		VehicleStainTimerHandle, this, &ACWWorkZone_Cells::UpdateVehicleStains, VehicleStainInterval, true);
}

void ACWWorkZone_Cells::OnProgressChanged(float NewProgress)
{
	Super::OnProgressChanged(NewProgress);

	RefreshPaintDryState();
}

void ACWWorkZone_Cells::RefreshPaintDryState()
{
	// 마름 판정은 서버만 한다. 자국을 찍는 쪽도 서버뿐이라 클라이언트는 알 필요가 없다.
	// 지우기 모드 존(청소/정리)에는 마를 페인트가 없다.
	if (!HasAuthority() || !IsFillMode())
	{
		return;
	}

	if (!IsCompleted())
	{
		// 도색이 다시 열렸다. 마름 대기를 취소하고 젖은 상태로 돌린다.
		GetWorldTimerManager().ClearTimer(PaintDryTimerHandle);

		if (bPaintDry)
		{
			bPaintDry = false;
			SetVehicleStainTimerEnabled(bStainsVehicles && VehicleStainZone != nullptr);
		}

		return;
	}

	// 이미 말랐거나 마르는 중이면 대기 시간을 다시 재지 않는다.
	if (bPaintDry || PaintDryTimerHandle.IsValid())
	{
		return;
	}

	if (PaintDryDelay <= 0.f)
	{
		HandlePaintDried();
		return;
	}

	GetWorldTimerManager().SetTimer(
		PaintDryTimerHandle, this, &ACWWorkZone_Cells::HandlePaintDried, PaintDryDelay, false);
}

void ACWWorkZone_Cells::HandlePaintDried()
{
	bPaintDry = true;
	PaintDryTimerHandle.Invalidate();

	UE_LOG(LogTemp, Log, TEXT("[%s] 페인트가 말랐습니다. 차량이 밟아도 더 이상 번지지 않습니다."), *GetName());
}

void ACWWorkZone_Cells::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 이번 프레임에 바뀐 칸을 한 번에 인스턴스에 반영한다.
	FlushCellMeshUpdates();

	// 모아둔 붓질을 내보낸다. 간격이 안 됐으면 다음 프레임으로 넘어간다.
	FlushOutgoingStrokes(false);

	// 늦게 들어올 플레이어에게 넘길 칸 상태를 갱신한다. 서버에서만 유효하다.
	RebuildCellSnapshotIfDue();

	ShowDebugCells();

	// 할 일이 다 끝났으면 다음 프레임부터는 틱을 돌지 않는다.
	RefreshTickEnabled();
}

void ACWWorkZone_Cells::ShowDebugCells() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr || CellFilled.Num() == 0)
	{
		return;
	}

	const FVector2D CellSize = GetCellSize();
	const FVector BoxExtent(CellSize.X * 0.5f, CellSize.Y * 0.5f, 2.f);

	for (int32 CellY = 0; CellY < FullGridSize.Y; ++CellY)
	{
		for (int32 CellX = 0; CellX < FullGridSize.X; ++CellX)
		{
			const bool bFilled = CellFilled[GetCellIndex(CellX, CellY)];

			if (IsCountedCell(CellX, CellY))
			{
				// 점수 영역: 아직 안 칠한 칸이 남은 작업이다.
				if (bShowDebugUnfilledCells && !bFilled)
				{
					DrawDebugBox(World, GetCellWorldLocation(CellX, CellY), BoxExtent, FColor::Red, false, -1.f, 0, 2.f);
				}

				continue;
			}

			// 바깥 고리: 칠해진 칸이 지워야 할 페인트다. 점수 영역과 반대다.
			if (bShowDebugSpillCells && bFilled)
			{
				DrawDebugBox(World, GetCellWorldLocation(CellX, CellY), BoxExtent, FColor::Orange, false, -1.f, 0, 2.f);
			}
		}
	}
}

FString ACWWorkZone_Cells::GetDebugDetailText() const
{
	// 남은 칸 수를 같이 찍는다. 퍼센트만으로는 몇 칸이 비었는지 알 수 없어서
	// "다 칠한 것 같은데 왜 완료가 안 되지" 를 확인할 방법이 없다.
	const int32 CountedCellCount = GridSize.X * GridSize.Y;
	const int32 SpillCellCount = GetSpillCellCount();

	FString Detail = FString::Printf(TEXT(" [%d/%d칸]"), CountedFilledCount, CountedCellCount);

	if (SpillCellCount > 0)
	{
		Detail += FString::Printf(TEXT(" 삐져나옴 %d칸"), SpillCellCount);
	}

	if (bPaintDry)
	{
		Detail += TEXT(" 마름");
	}

	// 격자 구성을 같이 찍는다.
	//
	// 서버와 클라이언트가 같은 붓질을 적용하는데도 칸 상태가 서로 다르면, 붓질이 아니라
	// 격자가 어긋난 것이다. 격자가 다르면 같은 지점이 한쪽에서는 점수 영역, 다른 쪽에서는
	// 바깥 고리로 잡혀서 "한쪽에만 있는 칸" 이 양방향으로 생긴다.
	// 두 화면의 이 값이 같은지부터 봐야 원인을 좁힐 수 있다.
	if (bShowDebugGridLayout)
	{
		const FVector2D CellSize = GetCellSize();
		Detail += FString::Printf(TEXT(" | 격자 %dx%d 원점(%d,%d) 칸크기 %.2fx%.2f"),
			FullGridSize.X, FullGridSize.Y,
			CountedOrigin.X, CountedOrigin.Y,
			CellSize.X, CellSize.Y);
	}

	return Detail;
}

void ACWWorkZone_Cells::GetExtraObjectives(TArray<FCWStageObjective>& OutObjectives) const
{
	// 이름을 비워두면 줄을 내지 않는다. 청소/테두리 존은 그대로 한 줄만 낸다.
	if (CleanupObjectiveName.IsEmpty())
	{
		return;
	}

	FCWStageObjective& Objective = OutObjectives.AddDefaulted_GetRef();
	Objective.DisplayName = CleanupObjectiveName;

	// 정리는 언제나 자기 도색 바로 다음 줄이다. (기획서 3.6 도색 작업 → 주변 페인트 정리)
	Objective.WorkOrder = GetWorkOrder() + 1;

	// 바깥 고리가 깨끗하면 완료. 존 하나가 한 줄을 내므로 분모는 1 이다.
	// 존이 여러 개면 GameMode 가 이름으로 묶어서 "페인트 정리 (4/5)" 로 합쳐준다.
	Objective.TotalCount = 1;
	Objective.CompletedCount = (GetSpillCellCount() == 0) ? 1 : 0;
}

float ACWWorkZone_Cells::GetVehicleSearchRadius() const
{
	// 차체 표면 기준 거리라, 존 경계에 걸친 차량까지 잡으려면 존 반경에 여유를 더한다.
	return Area->Bounds.SphereRadius + VehicleStainRadius;
}

bool ACWWorkZone_Cells::IsFilledAt(const FVector& WorldLocation) const
{
	const int32 CellIndex = FindCellIndexAt(WorldLocation);

	return CellFilled.IsValidIndex(CellIndex) && CellFilled[CellIndex];
}

FVector ACWWorkZone_Cells::ToLocalLocation(const FVector& WorldLocation) const
{
	return Area->GetComponentTransform().InverseTransformPosition(WorldLocation);
}

int32 ACWWorkZone_Cells::FindCellIndexAt(const FVector& WorldLocation) const
{
	if (CellFilled.Num() == 0 || CachedCellSize.X <= KINDA_SMALL_NUMBER || CachedCellSize.Y <= KINDA_SMALL_NUMBER)
	{
		return INDEX_NONE;
	}

	// GetCellLocalLocation 의 역산이다. 그쪽이 바뀌면 여기도 같이 바꿔야 한다.
	const FVector LocalLocation = ToLocalLocation(WorldLocation);
	const FVector2D FirstCellCenter = GetFirstCellCenter();

	// 칸 중심이 FirstCellCenter + 인덱스 * 칸크기 이므로, 반 칸을 보정하고 나누면 인덱스가 나온다.
	const int32 CellX = FMath::FloorToInt((LocalLocation.X - FirstCellCenter.X) / CachedCellSize.X + 0.5f);
	const int32 CellY = FMath::FloorToInt((LocalLocation.Y - FirstCellCenter.Y) / CachedCellSize.Y + 0.5f);

	if (CellX < 0 || CellX >= FullGridSize.X || CellY < 0 || CellY >= FullGridSize.Y)
	{
		return INDEX_NONE;
	}

	return GetCellIndex(CellX, CellY);
}

void ACWWorkZone_Cells::ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds)
{
	if (Worker == nullptr || !CanWorkNow(Worker->GetHeldTool()))
	{
		return;
	}

	// 검은색 롤러는 지우는 쪽이고, 그것도 Area 바깥에 삐져나간 페인트만 지운다.
	const bool bCleanup = IsCleanupTool(Worker->GetHeldTool());

	// 도구가 칸을 채우는지 지우는지 정한다. FillTools/CleanupTools 가 비면 예전처럼 모드가 정한다.
	const bool bNewFilled = ResolveFillForTool(Worker->GetHeldTool());

	if (HasAuthority() && !Worker->IsLocalWorker())
	{
		// 원격 플레이어의 붓질은 서버가 재현하지 않는다.
		// 서버가 아는 커서 좌표는 초당 10번짜리 샘플이라 그걸로 다시 칠하면 클라이언트 화면과
		// 어긋난 채로 굳는다. 그 플레이어의 붓질은 ReceiveBrushStrokes 로 들어온다.
		return;
	}

	// 반경은 아래 세 경로에 모두 같은 값이 들어가야 한다. 하나라도 다른 값을 쓰면
	// 머신마다 칠해진 범위가 달라져 그림이 어긋난다. 그래서 함수로 뺐다.
	//
	// 붓 중심은 커서가 가리키는 지점이다. 흰색 롤러든 검은색 롤러든 같다.
	// 캐릭터에서 얼마나 멀리까지 찍을 수 있는지는 UCWWorkerComponent::MaxBrushDistance 가
	// 이미 잘라서 넘겨준다. 여기서 도구별로 다르게 잡으면 조작 감각만 갈린다.
	const float EffectiveRadius = GetEffectiveBrushRadius(Worker);

	FCWBrushStroke Stroke;

	// 로컬 적용도 네트워크로 나갈 좌표로 한다. 이유는 QuantizeStrokeLocation 주석 참고.
	Stroke.Center = QuantizeStrokeLocation(WorldLocation);
	Stroke.Radius = EffectiveRadius;
	Stroke.bFilled = bNewFilled;
	// 고리가 있는 존이면 정리 도구가 고리만 지운다. 고리가 없으면 격자 전체가 정리 대상이다.
	Stroke.bSpillOnly = bCleanup && HasOverspillRing();

	// 스냅샷이 이 붓질을 덮지 않도록 시각을 남긴다. (TryApplyCellSnapshot)
	if (const UWorld* World = GetWorld())
	{
		LastLocalStrokeTime = World->GetTimeSeconds();
	}

	// 붓질을 확정하는 것은 언제나 그 플레이어의 기계다. 서버 왕복을 기다리면 붓이 손에서 밀린다.
	// 로컬 반영은 매 프레임 그대로 하고, 네트워크로 나가는 것만 묶는다.
	const int32 ChangedCellCount = ApplyStroke(Stroke);

	// 같은 붓질을 겹치는 다른 존에게도 넘긴다. 경계에서 이음새가 생기지 않게 하는 장치다.
	//
	// 자기 칸이 하나도 안 바뀌었어도 넘긴다. 다 칠한 스트라이프 위에서 붓이 밖으로 나가는
	// 순간이 정확히 그 경우인데, 여기서 조기 반환하면 그 바깥이 영영 안 칠해진다.
	//
	// 받는 쪽이 각자 서버에 자기 붓질을 올리므로 여기서 따로 보낼 필요는 없다.
	// (ReceiveBrushStrokes 에서는 전달하지 않는다. 그러면 같은 붓질이 두 번 적용된다)
	if (UCWPaintingSubsystem* PaintingSubsystem = UCWPaintingSubsystem::Get(this))
	{
		PaintingSubsystem->BroadcastBrushStroke(this, Worker, Stroke.Center, Stroke.Radius);
	}

	if (ChangedCellCount == 0)
	{
		// 바뀐 칸이 없으면 알릴 것도 없다. 붓을 대고 가만히 있는 동안 패킷이 나가지 않는다.
		return;
	}

	// 칠한 칸 수만큼 페인트를 쓴다. (기획서 5-3)
	// 잔량도 붓질과 같은 모델이다. 칠한 본인이 자기 기계에서 바로 깎고,
	// 서버는 올라온 붓질을 ReceiveBrushStrokes 에서 다시 적용하며 똑같이 깎는다.
	ConsumePaintForCells(Worker, ChangedCellCount);

	if (HasAuthority())
	{
		// 리슨 서버의 호스트. 같은 붓질을 전원에게 뿌려 그림을 맞춘다.
		QueueOutgoingStroke(nullptr, Stroke);
		return;
	}

	// 클라이언트. 방금 자기 화면에 확정한 붓질을 그대로 서버에 올린다.
	// 진행도는 여기서 오르지 않는다. 서버가 올린 값이 리플리케이트되어 덮는다.
	QueueOutgoingStroke(Worker, Stroke);
}

void ACWWorkZone_Cells::ReceiveBrushStrokes(UCWWorkerComponent* Worker, const TArray<FCWBrushStroke>& Strokes)
{
	if (!HasAuthority() || IsLocked())
	{
		return;
	}

	// 서버에서 진행도가 오르고, 같은 붓질이 전원에게 나간다.
	// 보낸 클라이언트에도 되돌아오지만 이미 칠해진 칸이라 아무 일도 일어나지 않는다.
	for (const FCWBrushStroke& Stroke : Strokes)
	{
		const int32 ChangedCellCount = ApplyStroke(Stroke);
		if (ChangedCellCount == 0)
		{
			continue;
		}

		// 보낸 클라이언트가 자기 기계에서 깎은 것과 같은 양을 서버에서도 깎는다.
		// 값이 어긋나더라도 도구의 리플리케이션이 서버 값으로 덮는다.
		ConsumePaintForCells(Worker, ChangedCellCount);

		QueueOutgoingStroke(nullptr, Stroke);
	}
}

int32 ACWWorkZone_Cells::AddCellsAt(const FVector& WorldLocation, float Radius, bool bSpillOnly)
{
	// 차량이 페인트를 밟는 판정은 서버가 한다. 클라이언트가 멋대로 칸을 늘리면 그림이 갈라진다.
	if (!HasAuthority())
	{
		return 0;
	}

	FCWBrushStroke Stroke;

	// 여기도 같은 이유로 먼저 반올림한다. 서버만 정확한 좌표로 찍으면 차량 자국이 기계마다 달라진다.
	Stroke.Center = QuantizeStrokeLocation(WorldLocation);
	Stroke.Radius = Radius;
	Stroke.bFilled = true;
	Stroke.bSpillOnly = bSpillOnly;

	// 여기서는 페인트를 소모하지 않는다. 차량 자국도 터진 통도 누구의 도구에서 나온 것이 아니다.
	const int32 ChangedCellCount = ApplyStroke(Stroke);
	if (ChangedCellCount > 0)
	{
		QueueOutgoingStroke(nullptr, Stroke);
	}

	// 오염이 이 존에 실제로 얹혔는지 호출부가 알아야 한다.
	// 하나도 안 얹혔으면 근처에 받아줄 존이 없다는 뜻이라, 런타임 오염 구역을 새로 만들어야 한다.
	return ChangedCellCount;
}

FVector ACWWorkZone_Cells::QuantizeStrokeLocation(const FVector& WorldLocation)
{
	// FVector_NetQuantize 는 1cm 단위로 반올림해서 보낸다. 같은 규칙으로 미리 맞춘다.
	return FVector(
		FMath::RoundToFloat(WorldLocation.X),
		FMath::RoundToFloat(WorldLocation.Y),
		FMath::RoundToFloat(WorldLocation.Z));
}

bool ACWWorkZone_Cells::IsCleanupTool(ECWToolType ToolType) const
{
	return ToolType != ECWToolType::None && CleanupTools.Contains(ToolType);
}

bool ACWWorkZone_Cells::IsFillTool(ECWToolType ToolType) const
{
	return ToolType != ECWToolType::None && FillTools.Contains(ToolType);
}

bool ACWWorkZone_Cells::ResolveFillForTool(ECWToolType ToolType) const
{
	// 지우는 도구가 우선이다. 같은 도구를 양쪽에 넣는 설정 실수가 있어도 지우기로 굳는다.
	if (IsCleanupTool(ToolType))
	{
		return false;
	}

	if (IsFillTool(ToolType))
	{
		return true;
	}

	// 지정이 없으면 예전처럼 모드가 정한다. 기존 존들은 FillTools 를 비워 두면 동작이 그대로다.
	return WorkMode == ECWCellWorkMode::FillCells;
}

bool ACWWorkZone_Cells::HasOverspillRing() const
{
	return CountedOrigin.X > 0 || CountedOrigin.Y > 0;
}

int32 ACWWorkZone_Cells::GetCleanableCellCount() const
{
	// 고리가 있으면 고리에 묻은 것만 지울 수 있다. 완성된 횡단보도까지 지워지면 안 된다.
	// 고리가 없으면 격자 전체가 정리 대상이다. (오염 레이어)
	return HasOverspillRing() ? GetSpillCellCount() : FilledCellCount;
}

bool ACWWorkZone_Cells::CanUseTool(ECWToolType ToolType) const
{
	// 정리 도구와 채우기 도구는 AcceptedTools 에 없어도 이 존에 들어올 수 있다.
	// AcceptedTools 에 같이 넣어버리면 검은색 롤러로도 도색 진행도가 올라가서 안 된다.
	return IsCleanupTool(ToolType) || IsFillTool(ToolType) || Super::CanUseTool(ToolType);
}

bool ACWWorkZone_Cells::CanWorkNow(ECWToolType ToolType) const
{
	if (IsCleanupTool(ToolType))
	{
		// 도색이 끝난 뒤에도 삐져나간 페인트는 남는다. 그래서 bCompleted 를 보지 않는다.
		// 대신 지울 게 없으면 대상에서 빠져야 한다. 안 그러면 다 치운 존이 계속 붓을 붙잡는다.
		return !IsLocked() && GetCleanableCellCount() > 0;
	}

	if (IsFillTool(ToolType))
	{
		// 완료 여부를 보지 않는다. 오염 레이어는 "깨끗함" 이 완료라서, bCompleted 를 보면
		// 깨끗한 레이어에 낙서를 시작할 수가 없다. 잠금만 본다.
		//
		// 다 칠한 스트라이프가 후보에 남는 문제는 생기지 않는다. 뒤집을 칸이 없으면
		// HasWorkAt 이 false 라 우선순위에서 밀리고, 붓질이 가도 바뀌는 칸이 0 이다.
		return !IsLocked();
	}

	return Super::CanWorkNow(ToolType);
}

void ACWWorkZone_Cells::Multicast_ApplyBrushes_Implementation(const TArray<FCWBrushStroke>& Strokes)
{
	// 서버 자신에게도 돌아오지만 이미 칠해진 칸이라 아무 일도 일어나지 않는다.
	for (const FCWBrushStroke& Stroke : Strokes)
	{
		ApplyStroke(Stroke);
	}
}

int32 ACWWorkZone_Cells::ApplyStroke(const FCWBrushStroke& Stroke)
{
	const float PreviousProgress = GetProgress();
	const int32 PreviousSpillCount = GetSpillCellCount();

	const int32 ChangedCellCount = SetCellsInRadius(Stroke.Center, Stroke.Radius, Stroke.bFilled, Stroke.bSpillOnly);
	if (ChangedCellCount == 0)
	{
		// 바뀐 칸이 없으면 더 할 일도 없다. 덧칠은 페인트도 쓰지 않는다. (기획서 5-3)
		return 0;
	}

	// 메쉬는 여기서 건드리지 않는다. SetCellsInRadius 가 바뀐 칸을 표시해 뒀고,
	// 실제 반영은 프레임 끝에 FlushCellMeshUpdates 가 한 번에 한다.

	// SetProgress 는 서버에서만 실제로 먹는다. 클라이언트에서는 조용히 무시된다.
	RefreshProgress();

	// 바깥 고리는 진행도에 안 잡힌다. 삐져나온 페인트를 지워도 Progress 가 그대로라
	// SetProgress 가 조기 반환하고, 체크리스트의 "페인트 정리" 줄이 갱신되지 않는다.
	// 진행도가 안 움직였는데 고리가 바뀐 경우에만 따로 알린다. (양쪽 다 바뀌면 이미 알렸다)
	if (FMath::IsNearlyEqual(GetProgress(), PreviousProgress) && GetSpillCellCount() != PreviousSpillCount)
	{
		NotifyStateChanged();
	}

	// 늦게 들어올 플레이어에게 넘길 밑그림도 낡았다.
	MarkCellSnapshotDirty();

	// 위에서 모아둔 것들(메쉬 갱신 / 전송 / 스냅샷)을 처리하려면 틱이 돌아야 한다.
	RefreshTickEnabled();

	return ChangedCellCount;
}

bool ACWWorkZone_Cells::BlocksPaintSpill() const
{
	// 도색 스트라이프는 켜지 않아도 자동으로 막는다. 오염이 쌓이면 도색과 정리가 같은 칸을 두고 다툰다.
	// 오염 레이어 자신은 당연히 예외다. 자기 칸을 자기가 가리면 격자가 통째로 비어버린다.
	return Super::BlocksPaintSpill() || (IsFillMode() && !bPaintSpillLayer);
}

void ACWWorkZone_Cells::RefreshSpillMask()
{
	if (!bPaintSpillLayer || CellFilled.Num() == 0)
	{
		return;
	}

	const UCWPaintingSubsystem* PaintingSubsystem = UCWPaintingSubsystem::Get(this);
	if (PaintingSubsystem == nullptr)
	{
		return;
	}

	// 가릴 대상은 존 타입이 아니라 "오염을 막는 영역인가" 로 고른다. (BlocksPaintSpill)
	//
	// 타입으로 고르면 칸 존이 아닌 것을 놓친다. 테두리 가이드라인 존이 그랬다.
	// 마스킹에서 빠져 있어서 그려둔 선 위로 페인트가 덮여 선이 안 보였다.
	//
	// 1단계 청소 존은 켜지 않는다. 도로 청소용이라 영역이 넓은데, 가려버리면
	// 그 위에는 낙서도 삐져나감도 아예 못 하게 된다.
	TArray<const ACWBaseWorkZone*> MaskingZones;
	for (const TWeakObjectPtr<ACWBaseWorkZone>& WeakZone : PaintingSubsystem->GetZones())
	{
		const ACWBaseWorkZone* Zone = WeakZone.Get();
		if (Zone != nullptr && Zone != this && Zone->BlocksPaintSpill())
		{
			MaskingZones.Add(Zone);
		}
	}

	CellMasked.Init(false, CellFilled.Num());

	if (MaskingZones.Num() == 0)
	{
		return;
	}

	// 페인트 단가를 스트라이프에서 물려받는다.
	//
	// 레이어가 자기 면적으로 자동 계산하면 도로 전체가 분모라 단가가 바닥까지 떨어져서
	// 낙서가 사실상 공짜가 된다. 삐져나간 페인트든 낙서든 같은 롤러로 칠하는 같은 페인트이므로
	// 단가도 같아야 한다. 레벨에서 두 값을 맞춰 넣는 실수를 없애려고 여기서 가져온다.
	//
	// 가리는 존 중 스트라이프(칸 존)에서만 가져온다. 테두리 가이드라인 존은 칸이 없어서 단가도 없다.
	if (PaintCostPerArea <= 0.f)
	{
		for (const ACWBaseWorkZone* MaskingZone : MaskingZones)
		{
			if (const ACWWorkZone_Cells* CellsZone = Cast<ACWWorkZone_Cells>(MaskingZone))
			{
				PaintCostPerArea = CellsZone->GetPaintCostPerArea();
				break;
			}
		}
	}

	int32 MaskedCount = 0;

	for (int32 CellY = 0; CellY < FullGridSize.Y; ++CellY)
	{
		for (int32 CellX = 0; CellX < FullGridSize.X; ++CellX)
		{
			const FVector CellLocation = GetCellWorldLocation(CellX, CellY);

			for (const ACWBaseWorkZone* MaskingZone : MaskingZones)
			{
				if (!MaskingZone->ContainsAreaPoint(CellLocation))
				{
					continue;
				}

				const int32 CellIndex = GetCellIndex(CellX, CellY);
				CellMasked[CellIndex] = true;
				++MaskedCount;

				// 이미 칠해져 있던 칸이면 집계에서도 빼야 한다.
				// 안 그러면 영영 지울 수 없는 칸이 정리 목표에 남는다.
				if (CellFilled[CellIndex])
				{
					CellFilled[CellIndex] = false;
					--FilledCellCount;
					--CountedFilledCount;
					QueueCellMeshUpdate(CellIndex);
				}

				break;
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[%s] 오염 레이어 마스킹: 가리는 존 %d개, 가려진 칸 %d / %d"),
		*GetName(), MaskingZones.Num(), MaskedCount, CellFilled.Num());

	FlushCellMeshUpdates();
	RefreshProgress();
}

float ACWWorkZone_Cells::GetPaintCostPerArea() const
{
	// 직접 지정한 값이 있으면 그대로 쓴다. (오염 레이어)
	if (PaintCostPerArea > 0.f)
	{
		return PaintCostPerArea;
	}

	// 없으면 "이 구역을 정확히 다 칠하면 PaintCostFull 이 든다" 에서 역산한다. (기획서 5-3)
	//
	// 전체 격자가 아니라 점수 영역 면적으로 나누는 것이 핵심이다. 그래야 안쪽만 정확히
	// 칠했을 때 소모량이 정확히 PaintCostFull 이 되고, 밖으로 나간 만큼이 초과 비용이 된다.
	const float CountedArea = CachedAreaSize.X * CachedAreaSize.Y;

	return CountedArea > 0.f ? PaintCostFull / CountedArea : 0.f;
}

void ACWWorkZone_Cells::ConsumePaintForCells(UCWWorkerComponent* Worker, int32 ChangedCellCount) const
{
	if (Worker == nullptr || ChangedCellCount <= 0)
	{
		return;
	}

	// 칸 수가 아니라 칸 "면적" 으로 센다.
	//
	// 스트라이프와 오염 레이어는 칸 크기가 다르다. 칸 수로 세면 같은 붓질인데도
	// 어느 격자에 찍혔느냐에 따라 소모량이 몇 배씩 달라진다. 면적으로 재면 격자 해상도와
	// 무관하게 "얼마나 넓게 칠했는가" 하나로 정해진다.
	const float ChangedArea = ChangedCellCount * CachedCellSize.X * CachedCellSize.Y;

	Worker->ConsumePaint(ChangedArea * GetPaintCostPerArea());
}

void ACWWorkZone_Cells::ReceiveForwardedBrush(UCWWorkerComponent* Worker, const FVector& Center, float Radius)
{
	if (Worker == nullptr)
	{
		return;
	}

	const ECWToolType ToolType = Worker->GetHeldTool();

	// 받는 쪽이 자기 규칙으로 다시 판단한다. 잠긴 스트라이프는 여기서 걸러지고,
	// 같은 도구라도 존에 따라 채우기가 되기도 지우기가 되기도 한다.
	if (!CanWorkNow(ToolType) || !IsWorkerInRange(Worker, Center))
	{
		return;
	}

	FCWBrushStroke Stroke;
	Stroke.Center = Center;
	Stroke.Radius = Radius;
	Stroke.bFilled = ResolveFillForTool(ToolType);
	Stroke.bSpillOnly = IsCleanupTool(ToolType) && HasOverspillRing();

	const int32 ChangedCellCount = ApplyStroke(Stroke);
	if (ChangedCellCount == 0)
	{
		return;
	}

	ConsumePaintForCells(Worker, ChangedCellCount);

	// 전달받은 붓질도 자기 몫으로 네트워크에 올린다.
	// 원본 존의 붓질과 별개로 나가므로 서버에서도 각자 같은 양이 적용된다.
	QueueOutgoingStroke(HasAuthority() ? nullptr : Worker, Stroke);
}

void ACWWorkZone_Cells::RebuildGridLayout()
{
	GridSize.X = FMath::Max(GridSize.X, 1);
	GridSize.Y = FMath::Max(GridSize.Y, 1);

	// Area 크기와 칸 크기는 붓질 한 번에 수백 번 쓰인다. 매번 컴포넌트에 물어보고 나눗셈을
	// 하는 대신 여기서 한 번만 잡아 둔다. 격자가 바뀌는 자리는 여기 하나뿐이다.
	CachedAreaSize = Area->GetUnscaledBoxExtent() * 2.f;
	CachedCellSize = FVector2D(CachedAreaSize.X / GridSize.X, CachedAreaSize.Y / GridSize.Y);

	// 바깥 고리도 안쪽과 같은 크기의 칸을 쓴다. 칸 크기가 달라지면 경계에서 티가 난다.
	CountedOrigin.X = (CachedCellSize.X > 0.f) ? FMath::CeilToInt(OverspillMargin / CachedCellSize.X) : 0;
	CountedOrigin.Y = (CachedCellSize.Y > 0.f) ? FMath::CeilToInt(OverspillMargin / CachedCellSize.Y) : 0;

	FullGridSize.X = GridSize.X + CountedOrigin.X * 2;
	FullGridSize.Y = GridSize.Y + CountedOrigin.Y * 2;
}

void ACWWorkZone_Cells::ResetCells()
{
	RebuildGridLayout();

	const int32 CellCount = FullGridSize.X * FullGridSize.Y;

	CellFilled.Init(false, CellCount);
	FilledCellCount = 0;
	CountedFilledCount = 0;

	// 격자가 새로 만들어지면 이전 인덱스로 모아둔 갱신은 의미가 없다.
	PendingCellMeshUpdates.Reset();
	PendingCellMeshUpdateMask.Init(false, CellCount);

	if (bStartFilled)
	{
		// 점수 영역만 채운다. 바깥 고리는 bStartSpillFilled 가 따로 본다.
		for (int32 CellY = CountedOrigin.Y; CellY < CountedOrigin.Y + GridSize.Y; ++CellY)
		{
			for (int32 CellX = CountedOrigin.X; CellX < CountedOrigin.X + GridSize.X; ++CellX)
			{
				CellFilled[GetCellIndex(CellX, CellY)] = true;
			}
		}

		FilledCellCount = GridSize.X * GridSize.Y;
		CountedFilledCount = FilledCellCount;
	}

	if (bStartSpillFilled)
	{
		// 고리만 채운다. 점수 영역 밖이라 진행도(CountedFilledCount)는 그대로 두고,
		// 정리 대상 수(GetSpillCellCount)만 올라간다.
		for (int32 CellY = 0; CellY < FullGridSize.Y; ++CellY)
		{
			for (int32 CellX = 0; CellX < FullGridSize.X; ++CellX)
			{
				if (IsCountedCell(CellX, CellY))
				{
					continue;
				}

				CellFilled[GetCellIndex(CellX, CellY)] = true;
				++FilledCellCount;
			}
		}
	}

	PeakCountedFilledCount = CountedFilledCount;
}

void ACWWorkZone_Cells::BuildGrid()
{
	ResetCells();
	// 격자가 너무 잘면 칸 수가 그대로 렌더·메모리 비용이 된다. 넓은 존을 깔 때 실수하기 쉽다.
	// 차량 자국은 타이어 폭만큼 거칠어도 되므로, 넓은 정리 존은 칸을 25~30cm 로 잡는 편이 낫다.
	// (칸 크기 = Area 크기 / GridSize 다. 존을 넓히면 GridSize 도 같이 키워야 칸 크기가 유지된다)
	constexpr int32 CellCountWarnThreshold = 20000;
	if (CellFilled.Num() > CellCountWarnThreshold)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] 칸이 %d개입니다. (격자 %dx%d, 칸 크기 %.1fx%.1fcm) 프레임에 영향이 올 수 있습니다. GridSize 를 줄여 칸을 키우세요."),
			*GetName(), CellFilled.Num(), FullGridSize.X, FullGridSize.Y, CachedCellSize.X, CachedCellSize.Y);
	}

	RebuildAllCellInstances();

	// 시작 진행도는 서버가 정한다. 클라이언트가 계산해서 넣으면
	// 이미 리플리케이트되어 온 진행도(도중 참가)를 0 으로 덮어쓴다.
	if (HasAuthority())
	{
		// 시작 진행도는 델리게이트를 쏘지 않고 조용히 넣는다.
		InitializeProgress(CalculateProgress());

		// 도중에 들어올 사람을 위해 시작 상태부터 밑그림을 만들어 둔다.
		// (1단계 청소처럼 처음부터 칸이 차 있는 존이 있다)
		MarkCellSnapshotDirty();
	}
}

bool ACWWorkZone_Cells::IsWorkerInRange(const UCWWorkerComponent* Worker, const FVector& WorkLocation) const
{
	const AActor* WorkerActor = (Worker != nullptr) ? Worker->GetOwner() : nullptr;
	if (WorkerActor == nullptr)
	{
		return false;
	}

	// 평면 거리는 보지 않는다. Super 를 부르지 않는 것이 이 함수의 요점이다.
	//
	// "존 박스 안에 들어와야 작업할 수 있다" 가 아니라 "붓이 닿는 곳을 칠한다" 가 이 존의 제약이다.
	// 박스 거리로 재면 두 가지가 어긋난다.
	//   하나, 격자는 Area 박스보다 바깥 고리만큼 넓다. 고리에 묻은 페인트를 지우러 서면
	//         박스에서 그만큼 떨어지는데, 그러면 존이 안 잡혀서 지울 수가 없다.
	//   둘, 박스가 큰 존은 반대편 끝에 서 있어도 거리가 0 이다. 제약이 되지 않는다.
	//
	// 실제 범위 제약은 붓이 닿는 거리 하나다.
	// 붓 중심은 이미 캐릭터 주변으로 묶여서 들어온다. (UCWWorkerComponent::MaxBrushDistance)
	// 고리가 있는 존이면 정리 도구는 고리 칸만 본다. ApplyWork 와 같은 기준이어야 한다.
	const bool bSpillOnly = IsCleanupTool(Worker->GetHeldTool()) && HasOverspillRing();

	// 높이만 확인한다. 칸 판정이 높이를 무시하므로 이걸 안 보면 다리 위에서도 아래가 칠해진다.
	const FVector LocalWorker = ToLocalLocation(WorkerActor->GetActorLocation());
	if (FMath::Abs(LocalWorker.Z) > WorkerHeightTolerance)
	{
		return false;
	}

	// 붓이 닿는 곳에 이 존의 칸이 있으면 대상이다.
	//
	// 칸 상태는 보지 않는다. 이미 다 칠한 자리에 서 있어도 이 존이 작업 대상인 것은 맞다.
	// "지금 뒤집을 칸이 있는가" 는 HasWorkAt 이 따로 본다. 둘을 합치면 다 칠한 자리를
	// 지나갈 때마다 대상이 풀려서 HUD 표시가 깜빡인다.
	return HasAnyCellInRadius(WorkLocation, GetEffectiveBrushRadius(Worker), bSpillOnly);
}

bool ACWWorkZone_Cells::HasWorkAt(const UCWWorkerComponent* Worker, const FVector& WorldLocation) const
{
	if (Worker == nullptr)
	{
		return true;
	}

	const bool bSpillOnly = IsCleanupTool(Worker->GetHeldTool()) && HasOverspillRing();
	const bool bNewFilled = ResolveFillForTool(Worker->GetHeldTool());

	// 이 붓질이 실제로 뒤집을 칸이 하나라도 있는지 본다.
	// ApplyWork 와 똑같은 중심·반경·판정을 써야 "잡히는데 안 칠해지는" 상태가 안 생긴다.
	return HasChangeableCellInRadius(WorldLocation, GetEffectiveBrushRadius(Worker), bNewFilled, bSpillOnly);
}

float ACWWorkZone_Cells::GetEffectiveBrushRadius(const UCWWorkerComponent* Worker) const
{
	if (!bScaleBrushByWorkSpeed || Worker == nullptr)
	{
		return BrushRadius;
	}

	return BrushRadius * Worker->GetWorkSpeedFactor();
}

bool ACWWorkZone_Cells::HasAnyCellInRadius(const FVector& WorldCenter, float Radius, bool bSpillOnly) const
{
	bool bFound = false;

	ForEachCellInRadius(WorldCenter, Radius, bSpillOnly,
		[&bFound](int32 /*CellIndex*/, bool /*bCounted*/)
		{
			// 하나만 찾으면 된다.
			bFound = true;
			return false;
		});

	return bFound;
}

bool ACWWorkZone_Cells::HasChangeableCellInRadius(const FVector& WorldCenter, float Radius, bool bNewFilled, bool bSpillOnly) const
{
	bool bFound = false;

	ForEachCellInRadius(WorldCenter, Radius, bSpillOnly,
		[this, bNewFilled, &bFound](int32 CellIndex, bool /*bCounted*/)
		{
			if (CellFilled[CellIndex] == bNewFilled)
			{
				return true;
			}

			// 하나만 찾으면 된다. 나머지는 볼 이유가 없다.
			bFound = true;
			return false;
		});

	return bFound;
}

int32 ACWWorkZone_Cells::SetCellsInRadius(const FVector& WorldCenter, float Radius, bool bNewFilled, bool bSpillOnly)
{
	int32 ChangedCellCount = 0;

	ForEachCellInRadius(WorldCenter, Radius, bSpillOnly,
		[this, bNewFilled, &ChangedCellCount](int32 CellIndex, bool bCounted)
		{
			if (CellFilled[CellIndex] == bNewFilled)
			{
				return true;
			}

			CellFilled[CellIndex] = bNewFilled;
			QueueCellMeshUpdate(CellIndex);

			const int32 Delta = bNewFilled ? 1 : -1;
			FilledCellCount += Delta;
			if (bCounted)
			{
				CountedFilledCount += Delta;
			}

			++ChangedCellCount;
			return true;
		});

	if (ChangedCellCount > 0)
	{
		PeakCountedFilledCount = FMath::Max(PeakCountedFilledCount, CountedFilledCount);
	}

	// 개수를 세는 이유: 페인트 소모가 "칠한 양" 에 비례하기 때문이다. (기획서 5-3)
	// 점수 영역 안팎을 가리지 않고 세므로, 바깥 고리에 삐져나간 칸도 그대로 비용이 된다.
	return ChangedCellCount;
}

bool ACWWorkZone_Cells::ShouldBuildCellMeshes() const
{
	// 데디케이티드 서버는 화면을 그리지 않는다. 칸이 수만 개인 정리 존에서 특히 크게 아낀다.
	return CellMeshes != nullptr && GetNetMode() != NM_DedicatedServer;
}

FTransform ACWWorkZone_Cells::GetCellInstanceTransform(int32 CellX, int32 CellY, bool bFilled) const
{
	const FVector2D CellLocation = GetCellLocalLocation(CellX, CellY);

	// 빈 칸은 스케일 0 으로 눕혀 둔다. 인스턴스를 지우지 않고 숨기는 방법이다.
	// 머티리얼이 커스텀 데이터 0번을 읽어 스스로 감출 수 있게 되면 이 분기는 없어도 된다.
	const FVector CellScale = bFilled
		? FVector(
			(CachedCellSize.X / 100.f) * (1.f - CellPadding),
			(CachedCellSize.Y / 100.f) * (1.f - CellPadding),
			1.f)
		: FVector::ZeroVector;

	return FTransform(FRotator::ZeroRotator, FVector(CellLocation.X, CellLocation.Y, CellHeightOffset), CellScale);
}

void ACWWorkZone_Cells::RebuildAllCellInstances()
{
	if (!ShouldBuildCellMeshes())
	{
		return;
	}

	CellMeshes->ClearInstances();
	PendingCellMeshUpdates.Reset();
	PendingCellMeshUpdateMask.Init(false, CellFilled.Num());

	if (CellFilled.Num() == 0)
	{
		return;
	}

	// 격자 전체를 한 번에 깐다. 인스턴스 인덱스 = 칸 인덱스라서 나중에 뒤져 찾을 필요가 없다.
	TArray<FTransform> InstanceTransforms;
	InstanceTransforms.Reserve(CellFilled.Num());

	for (int32 CellY = 0; CellY < FullGridSize.Y; ++CellY)
	{
		for (int32 CellX = 0; CellX < FullGridSize.X; ++CellX)
		{
			InstanceTransforms.Add(GetCellInstanceTransform(CellX, CellY, CellFilled[GetCellIndex(CellX, CellY)]));
		}
	}

	// 마지막 인자가 bUpdateNavigation 이다. 칸 메쉬는 콜리전이 없어서 내비게이션과 무관하다.
	CellMeshes->AddInstances(InstanceTransforms, false, false, false);

	// 1번 채널(점수 영역인지)은 격자가 바뀌지 않는 한 그대로라 여기서 한 번만 넣는다.
	for (int32 CellY = 0; CellY < FullGridSize.Y; ++CellY)
	{
		for (int32 CellX = 0; CellX < FullGridSize.X; ++CellX)
		{
			const int32 CellIndex = GetCellIndex(CellX, CellY);

			CellMeshes->SetCustomDataValue(CellIndex, 0, CellFilled[CellIndex] ? 1.f : 0.f, false);
			CellMeshes->SetCustomDataValue(CellIndex, 1, IsCountedCell(CellX, CellY) ? 1.f : 0.f, false);
		}
	}

	CellMeshes->MarkRenderStateDirty();
}

void ACWWorkZone_Cells::QueueCellMeshUpdate(int32 CellIndex)
{
	// 같은 칸이 한 프레임에 여러 번 뒤집힐 수 있다. (여러 명이 겹쳐 칠하거나 차량 자국이 겹칠 때)
	// 비트 하나로 중복을 걸러서 목록이 부풀지 않게 한다.
	if (!PendingCellMeshUpdateMask.IsValidIndex(CellIndex) || PendingCellMeshUpdateMask[CellIndex])
	{
		return;
	}

	PendingCellMeshUpdateMask[CellIndex] = true;
	PendingCellMeshUpdates.Add(CellIndex);
}

void ACWWorkZone_Cells::FlushCellMeshUpdates()
{
	if (PendingCellMeshUpdates.Num() == 0)
	{
		return;
	}

	if (!ShouldBuildCellMeshes())
	{
		// 데디케이티드 서버. 칸 상태는 이미 맞춰져 있고 그릴 것만 없다.
		for (const int32 CellIndex : PendingCellMeshUpdates)
		{
			PendingCellMeshUpdateMask[CellIndex] = false;
		}

		PendingCellMeshUpdates.Reset();
		return;
	}

	for (const int32 CellIndex : PendingCellMeshUpdates)
	{
		PendingCellMeshUpdateMask[CellIndex] = false;

		if (!CellFilled.IsValidIndex(CellIndex))
		{
			continue;
		}

		const bool bFilled = CellFilled[CellIndex];
		const int32 CellX = CellIndex % FullGridSize.X;
		const int32 CellY = CellIndex / FullGridSize.X;

		// 렌더 상태는 여기서 더럽히지 않는다. 반복문이 끝난 뒤 한 번만 더럽힌다.
		CellMeshes->UpdateInstanceTransform(CellIndex, GetCellInstanceTransform(CellX, CellY, bFilled), false, false, true);
		CellMeshes->SetCustomDataValue(CellIndex, 0, bFilled ? 1.f : 0.f, false);
	}

	PendingCellMeshUpdates.Reset();

	CellMeshes->MarkRenderStateDirty();
}

void ACWWorkZone_Cells::RefreshProgress()
{
	SetProgress(CalculateProgress());
}

float ACWWorkZone_Cells::CalculateProgress() const
{
	// 분모도 분자도 점수 영역만 본다. 바깥으로 삐져나간 페인트는 진행도를 올리지도 내리지도 않는다.
	const int32 CountedCellCount = GridSize.X * GridSize.Y;
	if (CellFilled.Num() == 0 || CountedCellCount <= 0)
	{
		return 0.f;
	}

	if (WorkMode == ECWCellWorkMode::FillCells)
	{
		// 채운 칸 비율이 그대로 진행도.
		return static_cast<float>(CountedFilledCount) / static_cast<float>(CountedCellCount);
	}

	// 지우기 모드.
	// 청소처럼 처음부터 다 채워져 있으면 전체 칸 수를 분모로,
	// 페인트 정리처럼 도중에 생기는 경우는 "지금까지 최대로 생겼던 칸 수"를 분모로 쓴다.
	const int32 Denominator = bStartFilled ? CountedCellCount : PeakCountedFilledCount;
	if (Denominator <= 0)
	{
		// 아직 지울 게 생기지도 않았으면 할 일이 없는 상태 = 완료로 본다.
		return 1.f;
	}

	return static_cast<float>(Denominator - CountedFilledCount) / static_cast<float>(Denominator);
}

bool ACWWorkZone_Cells::IsCountedCell(int32 CellX, int32 CellY) const
{
	return CellX >= CountedOrigin.X && CellX < CountedOrigin.X + GridSize.X
		&& CellY >= CountedOrigin.Y && CellY < CountedOrigin.Y + GridSize.Y;
}

FVector2D ACWWorkZone_Cells::GetFirstCellCenter() const
{
	return FVector2D(
		-CachedAreaSize.X * 0.5f - (CountedOrigin.X - 0.5f) * CachedCellSize.X,
		-CachedAreaSize.Y * 0.5f - (CountedOrigin.Y - 0.5f) * CachedCellSize.Y);
}

FVector2D ACWWorkZone_Cells::GetCellLocalLocation(int32 CellX, int32 CellY) const
{
	const FVector2D FirstCellCenter = GetFirstCellCenter();

	return FVector2D(
		FirstCellCenter.X + CellX * CachedCellSize.X,
		FirstCellCenter.Y + CellY * CachedCellSize.Y);
}

FVector ACWWorkZone_Cells::GetCellWorldLocation(int32 CellX, int32 CellY) const
{
	const FVector2D CellLocation = GetCellLocalLocation(CellX, CellY);

	return Area->GetComponentTransform().TransformPosition(FVector(CellLocation.X, CellLocation.Y, 0.f));
}

void ACWWorkZone_Cells::QueueOutgoingStroke(UCWWorkerComponent* Worker, const FCWBrushStroke& Stroke)
{
	// 클라이언트가 보내는 경우에만 작업자가 필요하다. 존은 소유자가 없어서 서버 RPC 를
	// 직접 보낼 수 없고, 폰에 붙은 작업 컴포넌트를 거쳐야 한다.
	if (Worker != nullptr)
	{
		PendingStrokeWorker = Worker;
	}

	PendingOutgoingStrokes.Add(Stroke);

	// 한 패킷에 담을 양을 넘으면 간격을 기다리지 않고 바로 내보낸다.
	// 차량 여러 대가 동시에 자국을 남길 때 목록이 무한정 자라는 것을 막는다.
	if (PendingOutgoingStrokes.Num() >= MaxStrokesPerPacket)
	{
		FlushOutgoingStrokes(true);
	}
}

void ACWWorkZone_Cells::FlushOutgoingStrokes(bool bForce)
{
	const UWorld* World = GetWorld();
	if (PendingOutgoingStrokes.Num() == 0 || World == nullptr)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (!bForce && Now - LastStrokeSendTime < StrokeSendInterval)
	{
		return;
	}

	LastStrokeSendTime = Now;

	if (HasAuthority())
	{
		Multicast_ApplyBrushes(PendingOutgoingStrokes);
	}
	else if (UCWWorkerComponent* Worker = PendingStrokeWorker.Get())
	{
		Worker->SubmitBrushStrokes(this, PendingOutgoingStrokes);
	}

	PendingOutgoingStrokes.Reset();
}

void ACWWorkZone_Cells::MarkCellSnapshotDirty()
{
	// 스냅샷은 서버만 만든다. 클라이언트는 받아서 적용만 한다.
	if (HasAuthority())
	{
		bCellSnapshotDirty = true;
	}
}

void ACWWorkZone_Cells::RebuildCellSnapshotIfDue()
{
	const UWorld* World = GetWorld();
	if (!bCellSnapshotDirty || !HasAuthority() || World == nullptr)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (Now - LastCellSnapshotBuildTime < CellSnapshotInterval)
	{
		return;
	}

	LastCellSnapshotBuildTime = Now;
	bCellSnapshotDirty = false;

	// 칸 하나를 비트 하나로 담는다. 1600칸이면 200바이트다.
	// 실시간 그림은 붓질 멀티캐스트가 이미 맞추고 있다. 이 값은 "도중에 들어온 사람이
	// 처음 화면을 그릴 때" 쓰는 밑그림이라 초당 한 번이면 충분하다.
	const int32 CellCount = CellFilled.Num();
	const int32 ByteCount = FMath::DivideAndRoundUp(CellCount, 8);

	CellSnapshot.Reset(ByteCount);
	CellSnapshot.AddZeroed(ByteCount);

	for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
	{
		if (CellFilled[CellIndex])
		{
			CellSnapshot[CellIndex / 8] |= static_cast<uint8>(1 << (CellIndex % 8));
		}
	}
}

void ACWWorkZone_Cells::OnRep_CellSnapshot()
{
	TryApplyCellSnapshot();
}

void ACWWorkZone_Cells::TryApplyCellSnapshot()
{
	// 스냅샷은 서버가 만든 밑그림이다. 클라이언트만 받아서 쓴다.
	const UWorld* World = GetWorld();
	if (HasAuthority() || World == nullptr)
	{
		return;
	}

	// 처음 받은 것은 무조건 적용한다.
	//
	// 이건 "도중에 들어온 플레이어가 이미 칠해진 그림을 못 받는" 구멍을 막기 위한 것이다.
	// 붓질 멀티캐스트는 그때 접속해 있던 사람에게만 가기 때문에, 늦게 들어온 사람은
	// 진행도만 70%로 보이고 화면은 백지인 상태가 된다.
	//
	// 그 뒤로 오는 것은 이 기계가 한동안 칠하지 않았을 때만 적용한다.
	// 존이 릴레번시 밖으로 나갔다 돌아오면 그동안의 붓질을 못 받는데, 그때 여기서 맞춰진다.
	// 반대로 칠하는 도중에 덮으면 방금 칠한 칸이 잠깐 사라졌다 돌아오는 되감기가 보인다.
	if (bCellSnapshotApplied && World->TimeSince(LastLocalStrokeTime) < SnapshotApplyIdleDelay)
	{
		return;
	}

	// 격자가 아직 없다. BeginPlay 에서 다시 부른다.
	const int32 CellCount = CellFilled.Num();
	if (CellCount == 0)
	{
		return;
	}

	// 격자 구성이 서로 다르면 비트 위치의 의미가 달라진다. 그럴 바에는 손대지 않는 편이 낫다.
	// (서버가 아직 스냅샷을 안 만들었을 때도 여기서 걸러진다. 다음 것을 기다린다)
	if (CellSnapshot.Num() != FMath::DivideAndRoundUp(CellCount, 8))
	{
		return;
	}

	bCellSnapshotApplied = true;

	FilledCellCount = 0;
	CountedFilledCount = 0;

	for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
	{
		const bool bFilled = (CellSnapshot[CellIndex / 8] & (1 << (CellIndex % 8))) != 0;

		// 달라진 칸만 메쉬 갱신 대상으로 넘긴다.
		// 스냅샷이 올 때마다 격자를 통째로 다시 만들면, 붓질에서 없앤 비용이 여기서 되살아난다.
		// 보통은 다른 사람이 방금 칠한 몇 칸뿐이다.
		if (CellFilled[CellIndex] != bFilled)
		{
			CellFilled[CellIndex] = bFilled;
			QueueCellMeshUpdate(CellIndex);
		}

		if (!bFilled)
		{
			continue;
		}

		++FilledCellCount;

		if (IsCountedCell(CellIndex % FullGridSize.X, CellIndex / FullGridSize.X))
		{
			++CountedFilledCount;
		}
	}

	PeakCountedFilledCount = FMath::Max(PeakCountedFilledCount, CountedFilledCount);

	// 위에서 모아둔 칸 갱신을 처리하려면 틱이 돌아야 한다.
	RefreshTickEnabled();
}

void ACWWorkZone_Cells::RefreshTickEnabled()
{
	// 평소에는 틱을 돌지 않는다. 할 일이 생기면 켜고, 없어지면 끈다.
	// 존이 레벨에 수십 개 깔려도 아무도 안 칠하는 동안에는 비용이 0 이다.
	const bool bNeedsTick = bShowDebugUnfilledCells
		|| bShowDebugSpillCells
		|| PendingCellMeshUpdates.Num() > 0
		|| PendingOutgoingStrokes.Num() > 0
		|| bCellSnapshotDirty;

	SetActorTickEnabled(bNeedsTick);
}
