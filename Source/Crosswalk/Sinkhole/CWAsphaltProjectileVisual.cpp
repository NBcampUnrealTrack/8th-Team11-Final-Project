// CWAsphaltProjectileVisual.cpp

#include "Sinkhole/CWAsphaltProjectileVisual.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ACWAsphaltProjectileVisual::ACWAsphaltProjectileVisual()
{
	PrimaryActorTick.bCanEverTick = true;

	// 순수 연출이라 복제하지 않는다. 각 머신이 자기 것을 스폰해 자기 시계로 돌린다.
	bReplicates = false;

	ProjectileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileMesh"));
	SetRootComponent(ProjectileMesh);

	// 콜리전은 절대 켜지 않는다. 켜면 판정과 연출을 분리한 설계가 무너진다.
	ProjectileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProjectileMesh->SetCastShadow(false);

	// 애셋을 만들기 전에도 눈으로 확인할 수 있도록 엔진 기본 구체를 물려 둔다.
	// BP 자식에서 실제 메시로 갈아끼우면 된다.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaceholderMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (PlaceholderMesh.Succeeded())
	{
		ProjectileMesh->SetStaticMesh(PlaceholderMesh.Object);
		ProjectileMesh->SetRelativeScale3D(FVector(0.3f));
	}
}

void ACWAsphaltProjectileVisual::Launch(const FVector& InStartLocation, const FVector& InImpactLocation, float InFlightTime)
{
	StartLocation = InStartLocation;
	ImpactLocation = InImpactLocation;
	FlightTime = InFlightTime;
	ElapsedTime = 0.f;

	// 아치 높이는 평면 거리에서 뽑는다. 총구 높이 때문에 궤적이 달라질 이유가 없다.
	ArcHeight = FVector::Dist2D(StartLocation, ImpactLocation) * ArcHeightRatio;

	SetActorLocation(StartLocation);

	// 비행 시간이 0 이면 그릴 것이 없다. 착탄 연출만 내고 사라진다.
	if (FlightTime <= 0.f)
	{
		OnImpactReached(ImpactLocation);
		Destroy();
	}
}

void ACWAsphaltProjectileVisual::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (FlightTime <= 0.f)
	{
		return;
	}

	ElapsedTime += DeltaTime;

	const float Alpha = FMath::Clamp(ElapsedTime / FlightTime, 0.f, 1.f);

	// 4t(1-t) 는 양 끝에서 0, 가운데서 1 이다. 그래서 시작점과 착탄점에 정확히 붙는다.
	const float ArcOffset = ArcHeight * 4.f * Alpha * (1.f - Alpha);

	FVector NewLocation = FMath::Lerp(StartLocation, ImpactLocation, Alpha);
	NewLocation.Z += ArcOffset;

	SetActorLocation(NewLocation);

	if (Alpha >= 1.f)
	{
		OnImpactReached(ImpactLocation);
		Destroy();
	}
}
