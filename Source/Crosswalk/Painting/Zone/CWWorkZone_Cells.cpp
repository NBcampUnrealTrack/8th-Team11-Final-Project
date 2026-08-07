#include "Painting/Zone/CWWorkZone_Cells.h"

#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Painting/CWWorkerComponent.h"
#include "Traffic/BaseVehicle.h"
#include "Traffic/TrafficSubsystem.h"

ACWWorkZone_Cells::ACWWorkZone_Cells()
{
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
}

void ACWWorkZone_Cells::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 에디터에서 박스나 격자 수를 바꾸면 칸이 바로 보이게 한다.
	// 진행도는 여기서 건드리지 않는다. 에디터에서 값이 바뀌면 레벨이 더럽혀진다.
	ApplyCellMaterial();
	ResetCells();
	RefreshCellMeshes();
}

void ACWWorkZone_Cells::ApplyCellMaterial()
{
	if (CellMaterial != nullptr)
	{
		CellMeshes->SetMaterial(0, CellMaterial);
	}
}

void ACWWorkZone_Cells::BeginPlay()
{
	Super::BeginPlay();

	ApplyCellMaterial();
	BuildGrid();

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
	UTrafficSubsystem* TrafficSubsystem = UTrafficSubsystem::Get(this);
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
		TArray<ABaseVehicle*> NearbyVehicles;
		TrafficSubsystem->GetVehiclesNearPoint(GetActorLocation(), GetVehicleSearchRadius(), NearbyVehicles);

		for (ABaseVehicle* Vehicle : NearbyVehicles)
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
	if (CellFilled.Num() == 0)
	{
		return false;
	}

	const FVector2D CellSize = GetCellSize();
	if (CellSize.X <= KINDA_SMALL_NUMBER || CellSize.Y <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// GetCellWorldLocation 의 역산이다. 그쪽이 바뀌면 여기도 같이 바꿔야 한다.
	const FVector AreaSize = Area->GetUnscaledBoxExtent() * 2.f;
	const FVector LocalLocation = Area->GetComponentTransform().InverseTransformPosition(WorldLocation);

	const int32 CellX = CountedOrigin.X + FMath::FloorToInt((LocalLocation.X + AreaSize.X * 0.5f) / CellSize.X);
	const int32 CellY = CountedOrigin.Y + FMath::FloorToInt((LocalLocation.Y + AreaSize.Y * 0.5f) / CellSize.Y);

	if (CellX < 0 || CellX >= FullGridSize.X || CellY < 0 || CellY >= FullGridSize.Y)
	{
		return false;
	}

	return CellFilled[GetCellIndex(CellX, CellY)];
}

void ACWWorkZone_Cells::ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds)
{
	if (Worker == nullptr || !CanWorkNow(Worker->GetHeldTool()))
	{
		return;
	}

	// 검은색 롤러는 지우는 쪽이고, 그것도 Area 바깥에 삐져나간 페인트만 지운다.
	const bool bCleanup = IsCleanupTool(Worker->GetHeldTool());

	// 채우기 모드면 칸을 켜고, 지우기 모드면 칸을 끈다.
	const bool bNewFilled = !bCleanup && (WorkMode == ECWCellWorkMode::FillCells);

	if (HasAuthority() && !Worker->IsLocalWorker())
	{
		// 원격 플레이어의 붓질은 서버가 재현하지 않는다.
		// 서버가 아는 커서 좌표는 초당 10번짜리 샘플이라 그걸로 다시 칠하면 클라이언트 화면과
		// 어긋난 채로 굳는다. 그 플레이어의 붓질은 ReceiveBrushStroke 로 들어온다.
		return;
	}

	// 붓질을 확정하는 것은 언제나 그 플레이어의 기계다. 서버 왕복을 기다리면 붓이 손에서 밀린다.
	if (!ApplyBrush(WorldLocation, BrushRadius, bNewFilled, bCleanup))
	{
		// 바뀐 칸이 없으면 알릴 것도 없다. 붓을 대고 가만히 있는 동안 패킷이 나가지 않는다.
		return;
	}

	if (HasAuthority())
	{
		// 리슨 서버의 호스트. 같은 붓질을 전원에게 뿌려 그림을 맞춘다.
		Multicast_ApplyBrush(WorldLocation, BrushRadius, bNewFilled, bCleanup);
		return;
	}

	// 클라이언트. 방금 자기 화면에 확정한 붓질을 그대로 서버에 올린다.
	// 진행도는 여기서 오르지 않는다. 서버가 올린 값이 리플리케이트되어 덮는다.
	Worker->SubmitBrushStroke(this, WorldLocation, BrushRadius, bNewFilled, bCleanup);
}

void ACWWorkZone_Cells::ReceiveBrushStroke(const FVector& WorldLocation, float Radius, bool bFilled, bool bSpillOnly)
{
	if (!HasAuthority() || IsLocked())
	{
		return;
	}

	// 서버에서 진행도가 오르고, 같은 붓질이 전원에게 나간다.
	// 보낸 클라이언트에도 되돌아오지만 이미 칠해진 칸이라 아무 일도 일어나지 않는다.
	if (ApplyBrush(WorldLocation, Radius, bFilled, bSpillOnly))
	{
		Multicast_ApplyBrush(WorldLocation, Radius, bFilled, bSpillOnly);
	}
}

void ACWWorkZone_Cells::AddCellsAt(const FVector& WorldLocation, float Radius, bool bSpillOnly)
{
	// 차량이 페인트를 밟는 판정은 서버가 한다. 클라이언트가 멋대로 칸을 늘리면 그림이 갈라진다.
	if (!HasAuthority())
	{
		return;
	}

	if (ApplyBrush(WorldLocation, Radius, true, bSpillOnly))
	{
		Multicast_ApplyBrush(WorldLocation, Radius, true, bSpillOnly);
	}
}

bool ACWWorkZone_Cells::IsCleanupTool(ECWToolType ToolType) const
{
	return ToolType != ECWToolType::None && CleanupTools.Contains(ToolType);
}

bool ACWWorkZone_Cells::CanUseTool(ECWToolType ToolType) const
{
	// 정리 도구는 AcceptedTools 에 없어도 이 존에 들어올 수 있다.
	// AcceptedTools 에 같이 넣어버리면 검은색 롤러로도 도색이 진행돼서 안 된다.
	return IsCleanupTool(ToolType) || Super::CanUseTool(ToolType);
}

bool ACWWorkZone_Cells::CanWorkNow(ECWToolType ToolType) const
{
	if (IsCleanupTool(ToolType))
	{
		// 도색이 끝난 뒤에도 삐져나간 페인트는 남는다. 그래서 bCompleted 를 보지 않는다.
		// 대신 지울 게 없으면 대상에서 빠져야 한다. 안 그러면 다 치운 존이 계속 붓을 붙잡는다.
		return !IsLocked() && GetSpillCellCount() > 0;
	}

	return Super::CanWorkNow(ToolType);
}

void ACWWorkZone_Cells::Multicast_ApplyBrush_Implementation(FVector_NetQuantize WorldCenter, float Radius, bool bNewFilled, bool bSpillOnly)
{
	// 서버 자신에게도 돌아오지만 이미 칠해진 칸이라 아무 일도 일어나지 않는다.
	ApplyBrush(WorldCenter, Radius, bNewFilled, bSpillOnly);
}

bool ACWWorkZone_Cells::ApplyBrush(const FVector& WorldCenter, float Radius, bool bNewFilled, bool bSpillOnly)
{
	const float PreviousProgress = GetProgress();
	const int32 PreviousSpillCount = GetSpillCellCount();

	if (!SetCellsInRadius(WorldCenter, Radius, bNewFilled, bSpillOnly))
	{
		// 바뀐 칸이 없으면 메쉬를 다시 만들 이유도 없다.
		return false;
	}

	RefreshCellMeshes();

	// SetProgress 는 서버에서만 실제로 먹는다. 클라이언트에서는 조용히 무시된다.
	RefreshProgress();

	// 바깥 고리는 진행도에 안 잡힌다. 삐져나온 페인트를 지워도 Progress 가 그대로라
	// SetProgress 가 조기 반환하고, 체크리스트의 "페인트 정리" 줄이 갱신되지 않는다.
	// 진행도가 안 움직였는데 고리가 바뀐 경우에만 따로 알린다. (양쪽 다 바뀌면 이미 알렸다)
	if (FMath::IsNearlyEqual(GetProgress(), PreviousProgress) && GetSpillCellCount() != PreviousSpillCount)
	{
		NotifyStateChanged();
	}

	return true;
}

void ACWWorkZone_Cells::RebuildGridLayout()
{
	GridSize.X = FMath::Max(GridSize.X, 1);
	GridSize.Y = FMath::Max(GridSize.Y, 1);

	// 바깥 고리도 안쪽과 같은 크기의 칸을 쓴다. 칸 크기가 달라지면 경계에서 티가 난다.
	const FVector2D CellSize = GetCellSize();
	CountedOrigin.X = (CellSize.X > 0.f) ? FMath::CeilToInt(OverspillMargin / CellSize.X) : 0;
	CountedOrigin.Y = (CellSize.Y > 0.f) ? FMath::CeilToInt(OverspillMargin / CellSize.Y) : 0;

	FullGridSize.X = GridSize.X + CountedOrigin.X * 2;
	FullGridSize.Y = GridSize.Y + CountedOrigin.Y * 2;
}

void ACWWorkZone_Cells::ResetCells()
{
	RebuildGridLayout();

	CellFilled.Init(false, FullGridSize.X * FullGridSize.Y);
	FilledCellCount = 0;
	CountedFilledCount = 0;

	if (bStartFilled)
	{
		// 시작부터 채워두는 건 점수 영역뿐이다. 바깥 고리는 작업 중에 삐져나가야 생긴다.
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

	PeakCountedFilledCount = CountedFilledCount;
}

void ACWWorkZone_Cells::BuildGrid()
{
	ResetCells();
	RefreshCellMeshes();

	// 시작 진행도는 서버가 정한다. 클라이언트가 계산해서 넣으면
	// 이미 리플리케이트되어 온 진행도(도중 참가)를 0 으로 덮어쓴다.
	if (HasAuthority())
	{
		// 시작 진행도는 델리게이트를 쏘지 않고 조용히 넣는다.
		InitializeProgress(CalculateProgress());
	}
}

bool ACWWorkZone_Cells::SetCellsInRadius(const FVector& WorldCenter, float Radius, bool bNewFilled, bool bSpillOnly)
{
	if (CellFilled.Num() == 0)
	{
		return false;
	}

	// 칸 중심만 보면 붓이 칸의 대부분을 덮고도 그 칸은 안 칠해진다.
	// 게다가 CellPadding 이 음수라 칸 메쉬가 격자보다 크게 그려져서, 이웃 칸의 메쉬가
	// 빈 칸을 덮어버린다. 그래서 화면은 빈틈 없이 칠해졌는데 진행도는 안 차는 상황이 생긴다.
	//
	// 칸 크기의 절반을 반경에 더해 "칸의 절반 이상이 덮이면 칠해진 것"으로 본다.
	// 눈에 보이는 것과 실제 칸 상태를 맞추는 게 목적이다.
	const FVector2D CellSize = GetCellSize();
	const float EffectiveRadius = Radius + FMath::Max(CellSize.X, CellSize.Y) * 0.5f;
	const float RadiusSquared = FMath::Square(EffectiveRadius);
	bool bAnyChanged = false;

	// 칸 수가 많지 않으므로(보통 200~600) 전체를 훑어도 충분히 빠르다.
	for (int32 CellY = 0; CellY < FullGridSize.Y; ++CellY)
	{
		for (int32 CellX = 0; CellX < FullGridSize.X; ++CellX)
		{
			const bool bCounted = IsCountedCell(CellX, CellY);

			// 검은색 롤러는 삐져나간 페인트만 지운다. 완성된 횡단보도 위를 지나가도 안전하다.
			if (bSpillOnly && bCounted)
			{
				continue;
			}

			const int32 CellIndex = GetCellIndex(CellX, CellY);
			if (CellFilled[CellIndex] == bNewFilled)
			{
				continue;
			}

			// 높이는 무시하고 평면 거리로만 판정한다. (탑뷰 클릭 감각 우선)
			const FVector CellLocation = GetCellWorldLocation(CellX, CellY);
			if (FVector::DistSquaredXY(CellLocation, WorldCenter) > RadiusSquared)
			{
				continue;
			}

			CellFilled[CellIndex] = bNewFilled;

			const int32 Delta = bNewFilled ? 1 : -1;
			FilledCellCount += Delta;
			if (bCounted)
			{
				CountedFilledCount += Delta;
			}

			bAnyChanged = true;
		}
	}

	if (bAnyChanged)
	{
		PeakCountedFilledCount = FMath::Max(PeakCountedFilledCount, CountedFilledCount);
	}

	return bAnyChanged;
}

void ACWWorkZone_Cells::RefreshCellMeshes()
{
	const FVector AreaSize = Area->GetUnscaledBoxExtent() * 2.f;
	const FVector2D CellSize = GetCellSize();
	const FVector CellScale(
		(CellSize.X / 100.f) * (1.f - CellPadding),
		(CellSize.Y / 100.f) * (1.f - CellPadding),
		1.f);

	// 격자의 (0,0) 칸 중심. 점수 영역이 Area 와 정확히 겹치도록 여백 칸 수만큼 밖에서 시작한다.
	const FVector2D FirstCellCenter(
		-AreaSize.X * 0.5f - (CountedOrigin.X - 0.5f) * CellSize.X,
		-AreaSize.Y * 0.5f - (CountedOrigin.Y - 0.5f) * CellSize.Y);

	// 칸이 몇백 개 수준이라 매번 전부 다시 만드는 게 가장 단순하고 버그가 없다.
	CellMeshes->ClearInstances();

	TArray<FTransform> InstanceTransforms;
	InstanceTransforms.Reserve(FilledCellCount);

	for (int32 CellY = 0; CellY < FullGridSize.Y; ++CellY)
	{
		for (int32 CellX = 0; CellX < FullGridSize.X; ++CellX)
		{
			if (!CellFilled[GetCellIndex(CellX, CellY)])
			{
				continue;
			}

			// 인스턴스 트랜스폼은 컴포넌트 로컬 기준이다.
			const FVector LocalLocation(
				FirstCellCenter.X + CellX * CellSize.X,
				FirstCellCenter.Y + CellY * CellSize.Y,
				CellHeightOffset);

			InstanceTransforms.Add(FTransform(FRotator::ZeroRotator, LocalLocation, CellScale));
		}
	}

	if (InstanceTransforms.Num() > 0)
	{
		CellMeshes->AddInstances(InstanceTransforms, false, false);
	}
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

FVector2D ACWWorkZone_Cells::GetCellSize() const
{
	const FVector AreaSize = Area->GetUnscaledBoxExtent() * 2.f;

	return FVector2D(AreaSize.X / FMath::Max(GridSize.X, 1), AreaSize.Y / FMath::Max(GridSize.Y, 1));
}

FVector ACWWorkZone_Cells::GetCellWorldLocation(int32 CellX, int32 CellY) const
{
	const FVector AreaSize = Area->GetUnscaledBoxExtent() * 2.f;
	const FVector2D CellSize = GetCellSize();

	// 칸 좌표는 바깥 고리를 포함한 전체 격자 기준이라 여백 칸 수만큼 원점을 밀어야 한다.
	const FVector LocalLocation(
		-AreaSize.X * 0.5f + (CellX - CountedOrigin.X + 0.5f) * CellSize.X,
		-AreaSize.Y * 0.5f + (CellY - CountedOrigin.Y + 0.5f) * CellSize.Y,
		0.f);

	return Area->GetComponentTransform().TransformPosition(LocalLocation);
}
