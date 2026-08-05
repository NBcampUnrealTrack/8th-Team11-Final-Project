#include "Painting/Zones/CWWorkZone_Cells.h"

#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Painting/CWWorkerComponent.h"

ACWWorkZone_Cells::ACWWorkZone_Cells()
{
	CellMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("CellMeshes"));
	CellMeshes->SetupAttachment(Area);
	CellMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CellMeshes->SetCastShadow(false);
}

void ACWWorkZone_Cells::BeginPlay()
{
	Super::BeginPlay();

	BuildGrid();
}

void ACWWorkZone_Cells::ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds)
{
	if (Worker == nullptr || !CanWorkNow(Worker->GetHeldTool()))
	{
		return;
	}

	// 채우기 모드면 칸을 켜고, 지우기 모드면 칸을 끈다.
	const bool bNewFilled = (WorkMode == ECWCellWorkMode::FillCells);
	if (!SetCellsInRadius(WorldLocation, BrushRadius, bNewFilled))
	{
		// 바뀐 칸이 없으면 메쉬를 다시 만들 이유도 없다.
		return;
	}

	RefreshCellMeshes();
	RefreshProgress();
}

void ACWWorkZone_Cells::AddCellsAt(const FVector& WorldLocation, float Radius)
{
	if (!SetCellsInRadius(WorldLocation, Radius, true))
	{
		return;
	}

	RefreshCellMeshes();
	RefreshProgress();
}

void ACWWorkZone_Cells::BuildGrid()
{
	GridSize.X = FMath::Max(GridSize.X, 1);
	GridSize.Y = FMath::Max(GridSize.Y, 1);

	CellFilled.Init(bStartFilled, GridSize.X * GridSize.Y);
	FilledCellCount = bStartFilled ? CellFilled.Num() : 0;
	PeakFilledCellCount = FilledCellCount;

	RefreshCellMeshes();

	// 시작 진행도는 델리게이트를 쏘지 않고 조용히 넣는다.
	InitializeProgress(CalculateProgress());
}

bool ACWWorkZone_Cells::SetCellsInRadius(const FVector& WorldCenter, float Radius, bool bNewFilled)
{
	if (CellFilled.Num() == 0)
	{
		return false;
	}

	const float RadiusSquared = FMath::Square(Radius);
	bool bAnyChanged = false;

	// 칸 수가 많지 않으므로(보통 200~600) 전체를 훑어도 충분히 빠르다.
	for (int32 CellY = 0; CellY < GridSize.Y; ++CellY)
	{
		for (int32 CellX = 0; CellX < GridSize.X; ++CellX)
		{
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
			FilledCellCount += bNewFilled ? 1 : -1;
			bAnyChanged = true;
		}
	}

	if (bAnyChanged)
	{
		PeakFilledCellCount = FMath::Max(PeakFilledCellCount, FilledCellCount);
	}

	return bAnyChanged;
}

void ACWWorkZone_Cells::RefreshCellMeshes()
{
	const FVector AreaSize = Area->GetUnscaledBoxExtent() * 2.f;
	const float CellSizeX = AreaSize.X / GridSize.X;
	const float CellSizeY = AreaSize.Y / GridSize.Y;
	const FVector CellScale(
		(CellSizeX / 100.f) * (1.f - CellPadding),
		(CellSizeY / 100.f) * (1.f - CellPadding),
		1.f);

	// 칸이 몇백 개 수준이라 매번 전부 다시 만드는 게 가장 단순하고 버그가 없다.
	CellMeshes->ClearInstances();

	TArray<FTransform> InstanceTransforms;
	InstanceTransforms.Reserve(FilledCellCount);

	for (int32 CellY = 0; CellY < GridSize.Y; ++CellY)
	{
		for (int32 CellX = 0; CellX < GridSize.X; ++CellX)
		{
			if (!CellFilled[GetCellIndex(CellX, CellY)])
			{
				continue;
			}

			// 인스턴스 트랜스폼은 컴포넌트 로컬 기준이다.
			const FVector LocalLocation(
				-AreaSize.X * 0.5f + (CellX + 0.5f) * CellSizeX,
				-AreaSize.Y * 0.5f + (CellY + 0.5f) * CellSizeY,
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
	const int32 TotalCellCount = CellFilled.Num();
	if (TotalCellCount == 0)
	{
		return 0.f;
	}

	if (WorkMode == ECWCellWorkMode::FillCells)
	{
		// 채운 칸 비율이 그대로 진행도.
		return static_cast<float>(FilledCellCount) / static_cast<float>(TotalCellCount);
	}

	// 지우기 모드.
	// 청소처럼 처음부터 다 채워져 있으면 전체 칸 수를 분모로,
	// 페인트 정리처럼 도중에 생기는 경우는 "지금까지 최대로 생겼던 칸 수"를 분모로 쓴다.
	const int32 Denominator = bStartFilled ? TotalCellCount : PeakFilledCellCount;
	if (Denominator <= 0)
	{
		// 아직 지울 게 생기지도 않았으면 할 일이 없는 상태 = 완료로 본다.
		return 1.f;
	}

	return static_cast<float>(Denominator - FilledCellCount) / static_cast<float>(Denominator);
}

FVector ACWWorkZone_Cells::GetCellWorldLocation(int32 CellX, int32 CellY) const
{
	const FVector AreaSize = Area->GetUnscaledBoxExtent() * 2.f;
	const FVector LocalLocation(
		-AreaSize.X * 0.5f + (CellX + 0.5f) * (AreaSize.X / GridSize.X),
		-AreaSize.Y * 0.5f + (CellY + 0.5f) * (AreaSize.Y / GridSize.Y),
		0.f);

	return Area->GetComponentTransform().TransformPosition(LocalLocation);
}
