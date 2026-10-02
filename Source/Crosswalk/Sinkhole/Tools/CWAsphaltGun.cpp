// CWAsphaltGun.cpp

#include "Sinkhole/Tools/CWAsphaltGun.h"

#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Sinkhole/CWAsphaltProjectileVisual.h"
#include "Sinkhole/CWAsphaltSplat.h"
#include "Sinkhole/CWSinkhole.h"
#include "Sinkhole/CWSinkholeAudioSubsystem.h"

ACWAsphaltGun::ACWAsphaltGun()
{
	// 페인트를 쓰지 않는다. 잔량 개념 자체가 없다.
	bUsesPaint = false;

	ToolType = ECWToolType::AsphaltPatcher;

	// BP 애셋 없이도 궤적이 보이도록 기본 클래스를 물려 둔다.
	ProjectileVisualClass = ACWAsphaltProjectileVisual::StaticClass();
}

void ACWAsphaltGun::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACWAsphaltGun, CurrentAmmo);
	DOREPLIFETIME(ACWAsphaltGun, bReloading);
}

void ACWAsphaltGun::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		CurrentAmmo = MagazineCapacity;
		OnRep_AmmoState();
	}
}

bool ACWAsphaltGun::CanFire() const
{
	if (bReloading || CurrentAmmo <= 0)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	return World->GetTimeSeconds() - LastFireTime >= FireInterval;
}

bool ACWAsphaltGun::TryFire(const FVector& AimLocation)
{
	if (!HasAuthority() || !CanFire())
	{
		return false;
	}

	LastFireTime = GetWorld()->GetTimeSeconds();

	CurrentAmmo = FMath::Max(CurrentAmmo - 1, 0);

	const FVector MuzzleLocation = GetMuzzleLocation();
	const FVector ImpactLocation = ResolveImpactLocation(MuzzleLocation, AimLocation);

	// 연출은 전원에게, 판정은 서버에서만. 궤적이 조금 달라 보여도 판정은 어긋나지 않는다.
	Multicast_PlayProjectile(MuzzleLocation, ImpactLocation, ProjectileFlightTime);

	// 착탄 판정을 비행시간만큼 미뤄서 연출과 맞춘다.
	// 날아가는 중인데 이미 구멍이 메꿔져 있으면 어색하다.
	if (ProjectileFlightTime > 0.f)
	{
		FTimerHandle ImpactTimerHandle;
		FTimerDelegate ImpactDelegate;
		ImpactDelegate.BindUObject(this, &ACWAsphaltGun::ResolveImpact, ImpactLocation);

		GetWorldTimerManager().SetTimer(ImpactTimerHandle, ImpactDelegate, ProjectileFlightTime, false);
	}
	else
	{
		ResolveImpact(ImpactLocation);
	}

	OnRep_AmmoState();

	// 탄창이 비면 자동으로 재장전에 들어간다.
	if (CurrentAmmo <= 0)
	{
		TryReload();
	}

	return true;
}

bool ACWAsphaltGun::TryReload()
{
	if (!HasAuthority() || bReloading)
	{
		return false;
	}

	// 가득 차 있으면 재장전할 이유가 없다. 수동 재장전은 한 발이라도 썼을 때만 들어간다.
	if (CurrentAmmo >= MagazineCapacity)
	{
		return false;
	}

	bReloading = true;
	OnRep_AmmoState();

	GetWorldTimerManager().SetTimer(
		ReloadTimerHandle,
		this,
		&ACWAsphaltGun::FinishReload,
		ReloadDuration,
		false);

	return true;
}

void ACWAsphaltGun::FinishReload()
{
	CurrentAmmo = MagazineCapacity;
	bReloading = false;

	OnRep_AmmoState();
}

void ACWAsphaltGun::OnRep_AmmoState()
{
	/**
	 * 탄 수와 재장전 여부가 같은 OnRep 을 쓴다. 그래서 한 발 쏠 때마다(탄 수만 바뀌어도) 여기로 온다.
	 * 재장전 연출은 bReloading 이 실제로 뒤집힌 순간에만 낸다. 안 그러면 쏠 때마다 "재장전 완료" 가 난다.
	 */
	if (bReloading != bWasReloading)
	{
		bWasReloading = bReloading;

		// 초기 OnRep 은 BeginPlay 보다 먼저 온다. 재장전 중인 총을 늦게 받은 클라이언트는 울리지 않는다.
		if (HasAuthority() || HasActorBegunPlay())
		{
			// 들고 이동하면서 재장전하므로 총에 붙여 따라가게 한다.
			UCWSinkholeAudioSubsystem::PlayCue(
				this,
				bReloading ? ECWSinkholeSoundCue::GunReloadStarted : ECWSinkholeSoundCue::GunReloadFinished,
				GetActorLocation(),
				GetRootComponent(),
				GetHolder());
		}

		if (bReloading)
		{
			OnReloadStarted(ReloadDuration);
		}
		else
		{
			OnReloadFinished();
		}
	}

	OnAmmoChanged.Broadcast(this, CurrentAmmo, bReloading);
}

void ACWAsphaltGun::Multicast_PlayProjectile_Implementation(const FVector& StartLocation, const FVector& ImpactLocation, float FlightTime)
{
	UCWSinkholeAudioSubsystem::PlayCue(this, ECWSinkholeSoundCue::GunFired, StartLocation, nullptr, GetHolder());

	OnFired();

	// 데디케이티드 서버에는 볼 사람이 없다. 연출 액터를 만들 이유가 없다.
	if (GetNetMode() != NM_DedicatedServer && ProjectileVisualClass != nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ACWAsphaltProjectileVisual* Projectile = GetWorld()->SpawnActor<ACWAsphaltProjectileVisual>(
			ProjectileVisualClass,
			StartLocation,
			FRotator::ZeroRotator,
			SpawnParams);

		if (Projectile != nullptr)
		{
			Projectile->Launch(StartLocation, ImpactLocation, FlightTime);
		}
	}

	OnProjectileLaunched(StartLocation, ImpactLocation, FlightTime);
}

FVector ACWAsphaltGun::ResolveImpactLocation(const FVector& MuzzleLocation, const FVector& AimLocation) const
{
	// 평면 거리로 잰다. 총구 높이 때문에 산포가 커지는 것은 의미가 없다.
	const FVector Delta = AimLocation - MuzzleLocation;
	const float Distance = FVector(Delta.X, Delta.Y, 0.f).Size();

	if (Distance <= NoSpreadDistance || MaxSpreadRadius <= 0.f)
	{
		return AimLocation;
	}

	// 가까우면 정확하고 멀면 부정확하다. 안전한 만큼 부정확해지는 트레이드오프다.
	const float SpreadAlpha = FMath::Clamp(
		(Distance - NoSpreadDistance) / FMath::Max(MaxSpreadDistance - NoSpreadDistance, KINDA_SMALL_NUMBER),
		0.f,
		1.f);

	const float SpreadRadius = MaxSpreadRadius * SpreadAlpha;

	// 원 안에 고르게 뿌린다. 반지름을 그냥 균등난수로 뽑으면 중심에 몰린다.
	const float Angle = FMath::FRandRange(0.f, 2.f * PI);
	const float Offset = SpreadRadius * FMath::Sqrt(FMath::FRand());

	return AimLocation + FVector(FMath::Cos(Angle) * Offset, FMath::Sin(Angle) * Offset, 0.f);
}

void ACWAsphaltGun::ResolveImpact(FVector ImpactLocation)
{
	if (!HasAuthority())
	{
		return;
	}

	if (ACWSinkhole* Sinkhole = FindSinkholeAt(ImpactLocation))
	{
		Sinkhole->ApplyAsphaltHit();
		return;
	}

	// 빗나갔다. 바닥에 자국이 남고, 밟은 사람이 느려진다.
	if (SplatClass != nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		GetWorld()->SpawnActor<ACWAsphaltSplat>(SplatClass, ImpactLocation, FRotator::ZeroRotator, SpawnParams);
	}
}

ACWSinkhole* ACWAsphaltGun::FindSinkholeAt(const FVector& ImpactLocation) const
{
	// 레벨에 동시에 존재하는 싱크홀 수가 적어 순회로 충분하다.
	// 수가 크게 늘면 오버랩 질의로 바꾸면 된다.
	for (TActorIterator<ACWSinkhole> It(GetWorld()); It; ++It)
	{
		ACWSinkhole* Sinkhole = *It;
		if (Sinkhole == nullptr || !Sinkhole->IsFallActive())
		{
			continue;
		}

		if (Sinkhole->IsPointInHitRange(ImpactLocation))
		{
			return Sinkhole;
		}
	}

	return nullptr;
}

FVector ACWAsphaltGun::GetMuzzleLocation() const
{
	if (ToolMesh != nullptr && ToolMesh->DoesSocketExist(MuzzleSocketName))
	{
		return ToolMesh->GetSocketLocation(MuzzleSocketName);
	}

	return GetActorLocation();
}
