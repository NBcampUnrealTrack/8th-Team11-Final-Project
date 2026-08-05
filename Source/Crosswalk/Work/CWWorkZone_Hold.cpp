#include "Work/CWWorkZone_Hold.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Work/CWWorkerComponent.h"

ACWWorkZone_Hold::ACWWorkZone_Hold()
{
	// 횡단보도 테두리 한 줄 정도의 기본 크기. 레벨에서 조절한다.
	Area->SetBoxExtent(FVector(400.f, 60.f, 20.f));

	// 게이지 방식은 100% 까지 채워야 자연스럽다.
	// 부모 기본값(0.98)은 격자 칸을 채우는 존을 위한 값이라 여기서는 맞지 않는다.
	// 이 값을 넘으면 CanWorkNow 가 false 가 되어 그 지점에서 게이지가 멈춘다.
	CompleteThreshold = 1.f;

	GuideMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GuideMesh"));
	GuideMesh->SetupAttachment(Area);
	GuideMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GuideMesh->SetCastShadow(false);

	// 플레이스홀더. 전용 가이드라인 머티리얼이 나오기 전에도 위치가 눈에 보이게 한다.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		GuideMesh->SetStaticMesh(PlaneMesh.Object);
	}
}

void ACWWorkZone_Hold::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 에디터에서 박스를 조절하면 가이드 메쉬가 바로 따라오게 한다.
	FitGuideMeshToArea();
}

void ACWWorkZone_Hold::BeginPlay()
{
	Super::BeginPlay();

	GuideMaterial = GuideMesh->CreateAndSetMaterialInstanceDynamic(0);
	OnProgressChanged(GetProgress());
}

void ACWWorkZone_Hold::ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds)
{
	if (Worker == nullptr || !CanWorkNow(Worker->GetHeldTool()))
	{
		return;
	}

	SetProgress(GetProgress() + DeltaSeconds / SecondsToComplete);
}

void ACWWorkZone_Hold::OnProgressChanged(float NewProgress)
{
	Super::OnProgressChanged(NewProgress);

	if (GuideMaterial != nullptr)
	{
		GuideMaterial->SetScalarParameterValue(ProgressParameterName, NewProgress);
	}
}

void ACWWorkZone_Hold::FitGuideMeshToArea()
{
	const FVector AreaExtent = Area->GetUnscaledBoxExtent();

	// /Engine/BasicShapes/Plane 은 100x100 이라 크기를 100 으로 나누면 그대로 배율이 된다.
	GuideMesh->SetRelativeScale3D(FVector((AreaExtent.X * 2.f) / 100.f, (AreaExtent.Y * 2.f) / 100.f, 1.f));
	GuideMesh->SetRelativeLocation(FVector(0.f, 0.f, GuideHeightOffset));
}
