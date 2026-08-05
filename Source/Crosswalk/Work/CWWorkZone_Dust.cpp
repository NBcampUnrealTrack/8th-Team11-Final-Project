#include "Work/CWWorkZone_Dust.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Work/CWWorkerComponent.h"

ACWWorkZone_Dust::ACWWorkZone_Dust()
{
	// 붓 자취를 매 프레임 찍어야 하므로 틱이 필요하다.
	PrimaryActorTick.bCanEverTick = true;

	Area->SetBoxExtent(FVector(100.f, 50.f, 20.f));

	// 시간으로 차오르는 방식이라 100% 까지 채워야 자연스럽다.
	// 부모 기본값(0.98)은 격자 칸 존을 위한 값이다.
	CompleteThreshold = 1.f;

	// 이 존은 빗자루 전용이다. 기본값으로 넣어 두면 배치할 때 빼먹을 일이 없다.
	AcceptedTools.Add(ECWToolType::Broom);

	PatchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PatchMesh"));
	PatchMesh->SetupAttachment(Area);
	PatchMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PatchMesh->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		PatchMesh->SetStaticMesh(PlaneMesh.Object);
	}
}

void ACWWorkZone_Dust::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	FitPatchMeshToArea();
}

void ACWWorkZone_Dust::BeginPlay()
{
	Super::BeginPlay();

	if (DustMaterial != nullptr)
	{
		DustDynMaterial = UMaterialInstanceDynamic::Create(DustMaterial, this);
		PatchMesh->SetMaterial(0, DustDynMaterial);
	}
	else
	{
		// 컴포넌트의 Materials 슬롯에 직접 넣은 경우. 그 머티리얼로 다이나믹 인스턴스를 만든다.
		DustDynMaterial = PatchMesh->CreateAndSetMaterialInstanceDynamic(0);
	}

	// 파라미터 이름이 안 맞으면 값만 조용히 버려진다. 그러면 "게이지는 오르는데 겉모습이 안 바뀐다".
	float ExistingValue = 0.f;
	if (DustDynMaterial == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] 먼지 머티리얼이 없습니다. Dust Material 프로퍼티나 PatchMesh 의 Materials 슬롯을 확인하세요."),
			*GetName());
	}
	else if (!DustDynMaterial->GetScalarParameterValue(ClearedParameterName, ExistingValue))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] 머티리얼에 '%s' 스칼라 파라미터가 없어 청소 진행도가 겉모습에 반영되지 않습니다."),
			*GetName(), *ClearedParameterName.ToString());
	}

	ApplyProgressToMaterial();

	// 데디케이티드 서버는 렌더링을 하지 않으니 마스크가 필요 없다. 진행도만 있으면 된다.
	if (GetNetMode() == NM_DedicatedServer || BrushStampMaterial == nullptr)
	{
		return;
	}

	CoverageMask = UKismetRenderingLibrary::CreateRenderTarget2D(
		this, CoverageMaskSize.X, CoverageMaskSize.Y, RTF_RGBA8, FLinearColor::White);

	if (CoverageMask != nullptr)
	{
		// 흰색 = 아직 먼지. 붓이 지나가면 검은색(청소됨) 쪽으로 찍힌다.
		UKismetRenderingLibrary::ClearRenderTarget2D(this, CoverageMask, FLinearColor::White);

		if (DustDynMaterial != nullptr)
		{
			DustDynMaterial->SetTextureParameterValue(CoverageMaskParameterName, CoverageMask);
		}
	}

	BrushDynMaterial = UMaterialInstanceDynamic::Create(BrushStampMaterial, this);
	if (BrushDynMaterial != nullptr)
	{
		BrushDynMaterial->SetScalarParameterValue(TEXT("Radius"), BrushRadiusUV);
	}
}

void ACWWorkZone_Dust::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 여기서 하는 일은 붓 자취를 찍는 것뿐이다. 진행도는 ApplyWork 가 서버에서 올린다.
	if (IsCompleted() || CoverageMask == nullptr || BrushDynMaterial == nullptr)
	{
		return;
	}

	// 지금 빗자루를 들고 쓸고 있는 캐릭터를 찾는다.
	// 도구와 작업 상태가 모두 리플리케이트되므로 클라이언트도 서버와 같은 판단을 할 수 있다.
	for (TActorIterator<APawn> PawnIter(GetWorld()); PawnIter; ++PawnIter)
	{
		const APawn* Pawn = *PawnIter;
		if (Pawn == nullptr)
		{
			continue;
		}

		const UCWWorkerComponent* Worker = Pawn->FindComponentByClass<UCWWorkerComponent>();
		if (Worker == nullptr || !Worker->IsWorking() || !CanUseTool(Worker->GetHeldTool()))
		{
			continue;
		}

		PaintAt(Pawn->GetActorLocation());
	}
}

void ACWWorkZone_Dust::ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds)
{
	if (Worker == nullptr || !CanWorkNow(Worker->GetHeldTool()))
	{
		return;
	}

	// 여러 명이 같이 쓸면 각자 이 함수를 부르므로 인원수만큼 빨라진다.
	SetProgress(GetProgress() + DeltaSeconds / SecondsToClear);
}

void ACWWorkZone_Dust::OnProgressChanged(float NewProgress)
{
	Super::OnProgressChanged(NewProgress);

	// 부모가 서버와 클라이언트 양쪽에서 이걸 불러주므로 머티리얼 갱신을 여기 두면 된다.
	ApplyProgressToMaterial();
}

void ACWWorkZone_Dust::PaintAt(const FVector& WorldLocation)
{
	FVector2D UV;
	if (!ProjectToLocalUV(WorldLocation, UV))
	{
		return;
	}

	BrushDynMaterial->SetScalarParameterValue(TEXT("CenterU"), UV.X);
	BrushDynMaterial->SetScalarParameterValue(TEXT("CenterV"), UV.Y);

	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, CoverageMask, BrushDynMaterial);
}

bool ACWWorkZone_Dust::ProjectToLocalUV(const FVector& WorldLocation, FVector2D& OutUV) const
{
	const FVector AreaExtent = Area->GetUnscaledBoxExtent();
	if (AreaExtent.X <= 0.f || AreaExtent.Y <= 0.f)
	{
		return false;
	}

	const FVector LocalOffset = GetActorRotation().UnrotateVector(WorldLocation - GetActorLocation());

	const float U = 0.5f + LocalOffset.X / (2.f * AreaExtent.X);
	const float V = 0.5f + LocalOffset.Y / (2.f * AreaExtent.Y);
	OutUV = FVector2D(U, V);

	return U >= 0.f && U <= 1.f && V >= 0.f && V <= 1.f;
}

void ACWWorkZone_Dust::ApplyProgressToMaterial()
{
	if (DustDynMaterial != nullptr)
	{
		DustDynMaterial->SetScalarParameterValue(ClearedParameterName, GetProgress());
	}
}

void ACWWorkZone_Dust::FitPatchMeshToArea()
{
	const FVector AreaExtent = Area->GetUnscaledBoxExtent();

	// /Engine/BasicShapes/Plane 은 100x100 이라 크기를 100 으로 나누면 그대로 배율이 된다.
	PatchMesh->SetRelativeScale3D(FVector((AreaExtent.X * 2.f) / 100.f, (AreaExtent.Y * 2.f) / 100.f, 1.f));
	PatchMesh->SetRelativeLocation(FVector(0.f, 0.f, PatchHeightOffset));
}
