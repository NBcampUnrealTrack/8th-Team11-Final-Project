// CWAsphaltSplat.cpp

#include "Sinkhole/CWAsphaltSplat.h"

#include "Components/DecalComponent.h"
#include "Components/SphereComponent.h"
#include "Painting/Components/CWTemperatureComponent.h"

ACWAsphaltSplat::ACWAsphaltSplat()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;

	SplatTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("SplatTrigger"));
	SetRootComponent(SplatTrigger);
	SplatTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SplatTrigger->SetCollisionObjectType(ECC_WorldDynamic);
	SplatTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	SplatTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SplatTrigger->SetGenerateOverlapEvents(true);

	SplatDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("SplatDecal"));
	SplatDecal->SetupAttachment(SplatTrigger);
	SplatDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
}

void ACWAsphaltSplat::BeginPlay()
{
	Super::BeginPlay();

	SplatTrigger->SetSphereRadius(SplatRadius);

	// 자국마다 조금씩 다르게 보이도록 돌린다. 순수 외형이라 동기화하지 않는다.
	SplatDecal->SetRelativeRotation(FRotator(-90.f, FMath::FRandRange(0.f, 360.f), 0.f));

	OnSoakStarted(LifeSpanSeconds);

	// 스스로 사라진다. 서버가 파괴하면 리플리케이션으로 클라이언트에서도 사라진다.
	if (HasAuthority())
	{
		SetLifeSpan(LifeSpanSeconds);

		GetWorldTimerManager().SetTimer(
			SlowRefreshTimerHandle,
			this,
			&ACWAsphaltSplat::RefreshOverlappingSlow,
			SlowRefreshInterval,
			true);
	}
}

void ACWAsphaltSplat::RefreshOverlappingSlow()
{
	TArray<AActor*> OverlappingActors;
	SplatTrigger->GetOverlappingActors(OverlappingActors, APawn::StaticClass());

	for (AActor* OverlappingActor : OverlappingActors)
	{
		if (OverlappingActor == nullptr)
		{
			continue;
		}

		UCWTemperatureComponent* Temperature = OverlappingActor->FindComponentByClass<UCWTemperatureComponent>();
		if (Temperature == nullptr)
		{
			continue;
		}

		// 최저 이속은 체온이 최대일 때의 이동속도와 같은 값으로 정의되어 있다. (기획서 7-9)
		// 이미 슬로우 중이면 이 호출은 무시된다.
		Temperature->ApplyExternalSlow(Temperature->GetMinMoveSpeedFactor(), SlowDuration);
	}
}
