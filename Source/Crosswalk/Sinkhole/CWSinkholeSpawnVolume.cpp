// CWSinkholeSpawnVolume.cpp

#include "Sinkhole/CWSinkholeSpawnVolume.h"

#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Sinkhole/CWSinkhole.h"

ACWSinkholeSpawnVolume::ACWSinkholeSpawnVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	SpawnBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnBounds"));
	SetRootComponent(SpawnBounds);
	SpawnBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpawnBounds->SetBoxExtent(FVector(1000.f, 1000.f, 200.f));

	// 범위를 눈으로 보고 배치해야 해서 에디터에서만 그린다.
	SpawnBounds->SetHiddenInGame(true);
	SpawnBounds->ShapeColor = FColor::Orange;
}

void ACWSinkholeSpawnVolume::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && bAutoStart)
	{
		SetSpawningEnabled(true);
	}
}

void ACWSinkholeSpawnVolume::SetSpawningEnabled(bool bEnabled)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!bEnabled)
	{
		GetWorldTimerManager().ClearTimer(SpawnTimerHandle);
		return;
	}

	GetWorldTimerManager().SetTimer(
		SpawnTimerHandle,
		this,
		&ACWSinkholeSpawnVolume::HandleSpawnTimer,
		SpawnInterval,
		true,
		InitialSpawnDelay);
}

void ACWSinkholeSpawnVolume::HandleSpawnTimer()
{
	// 동시 개수가 차 있으면 이번 주기는 건너뛴다.
	// 플레이어가 하나도 안 메꾸면 더 늘지 않으므로 회복 가능한 선이 유지된다.
	if (CountActiveSinkholes() >= MaxConcurrentSinkholes)
	{
		return;
	}

	SpawnSinkhole();
}

bool ACWSinkholeSpawnVolume::SpawnSinkhole()
{
	if (!HasAuthority() || SinkholeClass == nullptr)
	{
		return false;
	}

	FVector SpawnLocation;
	if (!FindSpawnLocation(SpawnLocation))
	{
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	return GetWorld()->SpawnActor<ACWSinkhole>(SinkholeClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams) != nullptr;
}

bool ACWSinkholeSpawnVolume::FindSpawnLocation(FVector& OutLocation) const
{
	const FVector Origin = SpawnBounds->GetComponentLocation();
	const FVector Extent = SpawnBounds->GetScaledBoxExtent();

	for (int32 Attempt = 0; Attempt < MaxPlacementAttempts; ++Attempt)
	{
		const FVector Candidate(
			FMath::FRandRange(Origin.X - Extent.X, Origin.X + Extent.X),
			FMath::FRandRange(Origin.Y - Extent.Y, Origin.Y + Extent.Y),
			Origin.Z + Extent.Z);

		// 도로 높이를 찾는다. 볼륨을 대충 띄워 놔도 데칼이 지면에 붙는다.
		FHitResult GroundHit;
		const FVector TraceEnd = Candidate - FVector(0.f, 0.f, GroundTraceDistance);

		if (!GetWorld()->LineTraceSingleByChannel(GroundHit, Candidate, TraceEnd, ECC_Visibility))
		{
			continue;
		}

		// 기존 싱크홀과 너무 가까우면 다시 뽑는다. 겹치면 몇 발을 쏴야 하는지 읽히지 않는다.
		bool bTooClose = false;
		for (TActorIterator<ACWSinkhole> It(GetWorld()); It; ++It)
		{
			if (*It == nullptr)
			{
				continue;
			}

			if (FVector::Dist2D(It->GetActorLocation(), GroundHit.ImpactPoint) < MinDistanceBetweenSinkholes)
			{
				bTooClose = true;
				break;
			}
		}

		if (bTooClose)
		{
			continue;
		}

		OutLocation = GroundHit.ImpactPoint;
		return true;
	}

	// 시도 횟수를 다 썼는데 자리를 못 찾았다. 볼륨이 좁거나 이미 꽉 찬 상황이다.
	return false;
}

int32 ACWSinkholeSpawnVolume::CountActiveSinkholes() const
{
	int32 Count = 0;

	for (TActorIterator<ACWSinkhole> It(GetWorld()); It; ++It)
	{
		if (*It != nullptr)
		{
			++Count;
		}
	}

	return Count;
}
