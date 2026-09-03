#include "Painting/Zone/CWWorkZone_Dust.h"

#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Painting/CWPaintingSubsystem.h"
#include "Painting/Components/CWWorkerComponent.h"

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

	DustDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("DustDecal"));
	DustDecal->SetupAttachment(Area);

	// 데칼은 바닥을 향해 투영해야 해서 아래를 보도록 돌려 둔다. 싱크홀 데칼과 같은 규약이다.
	DustDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));

	/**
	 * 화면에서 작아졌을 때 사라지는 최적화를 끈다.
	 *
	 * 먼지 존은 "여기 아직 할 일이 남았다" 를 알리는 연출이라 멀리서도 보여야 한다.
	 * 기본값(0.01)이면 쿼터뷰에서 화면 구석의 존이 조용히 사라져, 목록에는 남아 있는데
	 * 화면에는 없는 상태가 된다.
	 */
	DustDecal->SetFadeScreenSize(0.f);
}

void ACWWorkZone_Dust::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	FitDecalToArea();
}

void ACWWorkZone_Dust::BeginPlay()
{
	Super::BeginPlay();

	// 런타임에 스폰된 존도 OnConstruction 을 거치지만, 층 순서와 투영 범위는 BP 에셋에
	// 값이 남아 있으면 덮일 수 있다. 여기서 한 번 더 맞춰 둔다. (#109 와 같은 함정)
	FitDecalToArea();

	if (DustMaterial != nullptr)
	{
		DustDecal->SetDecalMaterial(DustMaterial);
	}

	// 컴포넌트에 직접 지정해 둔 경우도 여기서 함께 감싸진다. 지정된 것이 없으면 nullptr 이다.
	DustDynMaterial = DustDecal->CreateDynamicMaterialInstance();

	// 파라미터 이름이 안 맞으면 값만 조용히 버려진다. 그러면 "게이지는 오르는데 겉모습이 안 바뀐다".
	float ExistingValue = 0.f;
	if (DustDynMaterial == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] 먼지 데칼 머티리얼이 없습니다. Dust Material 프로퍼티나 DustDecal 의 Decal Material 을 확인하세요. "
				 "머티리얼 도메인이 Deferred Decal 이 아니면 지정해도 무시됩니다."),
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

	const UCWPaintingSubsystem* PaintingSubsystem = UCWPaintingSubsystem::Get(this);
	if (PaintingSubsystem == nullptr)
	{
		return;
	}

	// 지금 빗자루를 들고 쓸고 있는 캐릭터를 찾는다.
	// 도구와 작업 상태가 모두 리플리케이트되므로 클라이언트도 서버와 같은 판단을 할 수 있다.
	//
	// 등록된 작업자만 돈다. 예전에는 TActorIterator<APawn> 으로 훑었는데, 그러면 레벨 액터
	// 전체를 타입 테스트한 뒤 폰마다 컴포넌트 배열을 또 훑는 이중 스캔이 매 프레임,
	// 먼지 존 개수만큼, 모든 머신에서 돌았다. 실제로 찾는 대상은 플레이어 4명뿐이다.
	for (const TWeakObjectPtr<UCWWorkerComponent>& WeakWorker : PaintingSubsystem->GetWorkers())
	{
		const UCWWorkerComponent* Worker = WeakWorker.Get();
		if (Worker == nullptr || !Worker->IsWorking() || !CanUseTool(Worker->GetHeldTool()))
		{
			continue;
		}

		const AActor* WorkerActor = Worker->GetOwner();
		if (WorkerActor == nullptr)
		{
			continue;
		}

		PaintAt(WorkerActor->GetActorLocation());
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

	// 존 영역 안인지는 축 규약과 무관하게 판정해야 한다. 아래 뒤집기를 먼저 적용하면
	// 범위 검사 결과가 토글에 따라 달라진다.
	const float AlongX = 0.5f + LocalOffset.X / (2.f * AreaExtent.X);
	const float AlongY = 0.5f + LocalOffset.Y / (2.f * AreaExtent.Y);

	const bool bInsideArea = AlongX >= 0.f && AlongX <= 1.f && AlongY >= 0.f && AlongY <= 1.f;

	// 데칼 UV 축을 맞춘다. 자세한 사정은 헤더의 bSwapCoverageUV 주석 참고.
	float U = bSwapCoverageUV ? AlongY : AlongX;
	float V = bSwapCoverageUV ? AlongX : AlongY;

	if (bFlipCoverageU)
	{
		U = 1.f - U;
	}

	if (bFlipCoverageV)
	{
		V = 1.f - V;
	}

	OutUV = FVector2D(U, V);

	return bInsideArea;
}

void ACWWorkZone_Dust::ApplyProgressToMaterial()
{
	if (DustDynMaterial != nullptr)
	{
		DustDynMaterial->SetScalarParameterValue(ClearedParameterName, GetProgress());
	}
}

void ACWWorkZone_Dust::FitDecalToArea()
{
	if (DustDecal == nullptr)
	{
		return;
	}

	const FVector AreaExtent = Area->GetUnscaledBoxExtent();

	/**
	 * DecalSize 는 반쪽 크기(half extent)라 Area 의 익스텐트를 그대로 넣으면 된다.
	 * 평면 메쉬처럼 100 으로 나눌 필요가 없다.
	 *
	 * 축이 한 번 돌아간다. 데칼을 -90 도 눕히면 로컬 X 가 아래(투영 방향)를 보고,
	 * 로컬 Y 는 존의 Y 그대로, 로컬 Z 가 존의 X 를 향한다. 그래서 Y/Z 에 넣는 값이
	 * 존 익스텐트의 Y/X 순서가 된다. 이걸 뒤집으면 존이 가로세로 바뀐 모양으로 찍힌다.
	 */
	DustDecal->DecalSize = FVector(DecalProjectionDepth, AreaExtent.Y, AreaExtent.X);

	// 높이 보정은 필요 없다. 투영 깊이가 도로면을 감싸고, 데칼은 Z-파이팅을 만들지 않는다.
	DustDecal->SetRelativeLocation(FVector::ZeroVector);

	DustDecal->SetSortOrder(DecalSortOrder);

	DustDecal->MarkRenderStateDirty();
}
