// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseVehicle.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "TrafficLane.h"
#include "TrafficSubsystem.h"
#include "Data/Structs/CWVehicleHitContext.h"
#include "Interfaces/Interface_VehicleHitReceiver.h"

ABaseVehicle::ABaseVehicle()
{
	PrimaryActorTick.bCanEverTick = true;

	// 스폰/주행/판정은 서버 권위로 둔다.
	// 클라 표시는 일단 무브먼트 리플리케이션에 맡기고, 최종 복제 방식은 복제·판정 파트와 합의한다.
	bReplicates = true;
	SetReplicateMovement(true);

	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	SetRootComponent(CollisionBox);
	// 승용차 대략치. 플레이스홀더 메쉬 크기와 맞춰 조정한다.
	CollisionBox->SetBoxExtent(FVector(220.f, 90.f, 70.f));
	CollisionBox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	CollisionBox->SetGenerateOverlapEvents(true);

	VehicleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VehicleMesh"));
	VehicleMesh->SetupAttachment(CollisionBox);
	VehicleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ABaseVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 주행은 서버만 계산한다.
	if (!HasAuthority())
	{
		return;
	}

	// 패링당한 차량은 차선을 벗어나 튕겨나간 방향으로 직진한다.
	// 앞차와의 간격도 보지 않는다. 소멸은 패링 시 걸어둔 타이머가 맡는다.
	if (bParried)
	{
		AddActorWorldOffset(GetActorForwardVector() * GetEffectiveSpeed() * DeltaSeconds);
		return;
	}

	if (!CurrentLane)
	{
		return;
	}

	DistanceAlongLane += ResolveStepSpeed(DeltaSeconds) * DeltaSeconds;

	if (DistanceAlongLane >= CurrentLane->GetLaneLength())
	{
		OnReachedLaneEnd();
		return;
	}

	UpdateTransformOnLane();
}

float ABaseVehicle::GetVehicleLength() const
{
	return CollisionBox ? CollisionBox->GetUnscaledBoxExtent().X * 2.f : 0.f;
}

void ABaseVehicle::InitializeOnLane(ATrafficLane* InLane, const FTrafficVehicleSpawnParams& Params)
{
	CurrentLane = InLane;
	DistanceAlongLane = Params.StartDistance;
	LaneLateralOffset = Params.LateralOffset;
	bParried = false;

	// 0 이하가 들어오면 차량이 멈춰버리므로 하한을 둔다.
	SpeedMultiplier = FMath::Max(KINDA_SMALL_NUMBER, Params.SpeedMultiplier);

	// 풀링으로 재사용될 때 지난 주행의 피격 기록이 남아 있으면 안 된다.
	HitActors.Reset();

	UpdateTransformOnLane();
}

bool ABaseVehicle::PredictTimeToImpact(const AActor* Target, float& OutTimeToImpact) const
{
	OutTimeToImpact = 0.f;

	// 튕겨나가는 중인 차량도 예측 대상이다. 날아오는 차를 다시 패링할 수 있어야 한다.
	const float EffectiveSpeed = GetEffectiveSpeed();
	if (!Target || EffectiveSpeed <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector Forward = GetActorForwardVector();
	const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();

	// 진행 방향 성분이 0 이하면 이미 지나친 대상이다.
	const float ForwardDistance = FVector::DotProduct(ToTarget, Forward);
	if (ForwardDistance <= 0.f)
	{
		return false;
	}

	// 경로에서 좌우로 벗어난 양. 차선 밖 대상은 위협으로 보지 않는다.
	const FVector LateralOffset = ToTarget - (Forward * ForwardDistance);
	if (LateralOffset.Size2D() > ThreatLateralTolerance)
	{
		return false;
	}

	// 차량 앞면 기준으로 재서, 이미 박스 안에 들어온 대상은 0초로 취급한다.
	const float FrontOffset = CollisionBox ? CollisionBox->GetUnscaledBoxExtent().X : 0.f;
	OutTimeToImpact = FMath::Max(0.f, (ForwardDistance - FrontOffset) / EffectiveSpeed);

	return true;
}

float ABaseVehicle::GetDistanceToPoint(const FVector& Point) const
{
	if (CollisionBox)
	{
		FVector ClosestPointOnBody = FVector::ZeroVector;
		const float SurfaceDistance = CollisionBox->GetClosestPointOnCollision(Point, ClosestPointOnBody);

		// 콜리전 바디를 못 찾으면 음수가 온다. 그때만 원점 거리로 대체한다.
		if (SurfaceDistance >= 0.f)
		{
			return SurfaceDistance;
		}
	}

	return FVector::Dist(GetActorLocation(), Point);
}

bool ABaseVehicle::ReceiveParryAttempt_Implementation(const FCWParryAttemptContext& ParryAttemptContext)
{
	// 클라에서 통과시키면 그 클라에서만 차가 멈춘다. 판정은 서버만 한다.
	if (!HasAuthority())
	{
		return false;
	}

	// 튕겨나갈 방향은 플레이어가 찍은 커서 방향이다. (기획서 8.1)
	return TryParry(ParryAttemptContext.Parrier, ParryAttemptContext.AttemptDirection);
}

bool ABaseVehicle::TryParry(AActor* Parrier, const FVector& LaunchDirection)
{
	if (!CanBeParried() || !Parrier)
	{
		return false;
	}

	// 판정은 서버에서만 확정한다. 클라 호출은 서버 RPC를 거쳐 들어와야 한다.
	if (!HasAuthority())
	{
		return false;
	}

	// 방향을 틀기 전에 판정해야 한다. 예측이 지금 진행 방향을 기준으로 하기 때문이다.
	float TimeToImpact = 0.f;
	if (!PredictTimeToImpact(Parrier, TimeToImpact) || TimeToImpact > ParryWindow)
	{
		return false;
	}

	// 기획서 8.1 - 정지가 아니라 커서 방향으로 튕겨나간다.
	// 방향이 비어 있으면 온 길로 되돌려 보낸다.
	const FVector LaunchForward = LaunchDirection.GetSafeNormal2D();
	SetActorRotation((LaunchForward.IsNearlyZero() ? -GetActorForwardVector().GetSafeNormal2D() : LaunchForward).Rotation());

	bParried = true;

	// 차선에서 뺀다. 그대로 두면 뒤차들이 튕겨나간 차를 앞차로 보고 줄줄이 멈춘다.
	CurrentLane = nullptr;

	// 쳐낸 사람은 튕겨나가는 차와 아직 겹쳐 있으므로 피격 대상에서 제외한다.
	HitActors.Add(Parrier);

	// 기획서 8.1 - 튕겨나간 뒤 2초간 이동하고 사라진다.
	// 다시 패링당하면 SetTimer 가 기존 타이머를 덮어써 2초가 새로 시작된다.
	if (ParriedDespawnDelay > 0.f)
	{
		GetWorldTimerManager().SetTimer(ParriedDespawnTimerHandle, this, &ABaseVehicle::OnReachedLaneEnd, ParriedDespawnDelay, false);
	}

	OnVehicleParried.Broadcast(this, Parrier);

	return true;
}

void ABaseVehicle::BeginPlay()
{
	Super::BeginPlay();

	CollisionBox->OnComponentBeginOverlap.AddDynamic(this, &ABaseVehicle::HandleBeginOverlap);

	if (UTrafficSubsystem* TrafficSubsystem = UTrafficSubsystem::Get(this))
	{
		TrafficSubsystem->RegisterVehicle(this);
	}
}

void ABaseVehicle::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ParriedDespawnTimerHandle);
	}

	if (UTrafficSubsystem* TrafficSubsystem = UTrafficSubsystem::Get(this))
	{
		TrafficSubsystem->UnregisterVehicle(this);
	}

	Super::EndPlay(EndPlayReason);
}

void ABaseVehicle::OnReachedLaneEnd()
{
	// 풀링을 붙이면 Destroy 대신 스포너에 반납하도록 자식에서 오버라이드한다.
	Destroy();
}

float ABaseVehicle::ResolveStepSpeed(float DeltaSeconds) const
{
	const float DesiredSpeed = GetEffectiveSpeed();

	if (DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		return DesiredSpeed;
	}

	const UTrafficSubsystem* TrafficSubsystem = UTrafficSubsystem::Get(this);
	if (!TrafficSubsystem)
	{
		return DesiredSpeed;
	}

	const ABaseVehicle* VehicleAhead = TrafficSubsystem->FindVehicleAhead(this);
	if (!VehicleAhead)
	{
		return DesiredSpeed;
	}

	// 두 차의 앞뒤 범퍼 사이 빈 거리. 여기서 FollowingGap을 더 빼 여유를 남긴다.
	const float BumperToBumper = VehicleAhead->GetDistanceAlongLane() - DistanceAlongLane
		- (GetVehicleLength() * 0.5f) - (VehicleAhead->GetVehicleLength() * 0.5f);
	const float AllowedTravel = BumperToBumper - FollowingGap;

	if (AllowedTravel <= 0.f)
	{
		// 이미 최소 간격 안까지 붙었으면 멈춰 세운다. 통과보다 정지가 덜 어색하다.
		return 0.f;
	}

	// 이번 프레임에 허용 거리 이상 전진하지 못하게 속도를 깎는다.
	return FMath::Min(DesiredSpeed, AllowedTravel / DeltaSeconds);
}

void ABaseVehicle::UpdateTransformOnLane()
{
	if (!CurrentLane)
	{
		return;
	}

	FTransform LaneTransform = CurrentLane->GetTransformAtDistance(DistanceAlongLane);

	// 중심선에서 좌우로 밀어, 같은 차선의 차들이 한 줄로 겹쳐 지나가지 않게 한다.
	if (!FMath::IsNearlyZero(LaneLateralOffset))
	{
		LaneTransform.AddToTranslation(LaneTransform.GetRotation().GetRightVector() * LaneLateralOffset);
	}

	SetActorTransform(LaneTransform);
}

void ABaseVehicle::HandleBeginOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool bFromSweep, const FHitResult& SweepResult)
{
	// 튕겨나가는 중에도 피격 판정은 살아 있다. 날아간 차가 다른 플레이어를 치는 게
	// 기획서 8.2 가 노리는 그림이다. 쳐낸 사람만 HitActors 로 제외된다.
	if (!HasAuthority() || !OtherActor || OtherActor == this)
	{
		return;
	}

	// 피격 인터페이스를 구현했으면 대상으로 본다. 아직 구현 전인 플레이어도 검증할 수 있도록
	// 프로토타입 단계에서는 폰도 함께 받아준다.
	const bool bImplementsReceiver = OtherActor->Implements<UInterface_VehicleHitReceiver>();
	if (!bImplementsReceiver && !OtherActor->IsA<APawn>())
	{
		return;
	}

	// 한 대가 같은 대상을 여러 프레임에 걸쳐 중복으로 치지 않게 막는다.
	if (HitActors.Contains(OtherActor))
	{
		return;
	}
	HitActors.Add(OtherActor);

	const FVector HitDirection = GetActorForwardVector();

	if (bImplementsReceiver)
	{
		FCWVehicleHitContext HitContext;
		HitContext.HitLocation = ResolveHitLocation(OtherActor, bFromSweep, SweepResult);
		HitContext.HitDirection = HitDirection;
		HitContext.VehicleSpeed = GetEffectiveSpeed();
		HitContext.VehicleType = VehicleType;
		HitContext.bCanParry = bCanBeParried;
		HitContext.bFlattenOnHit = bFlattenOnHit;

		// 넉백 강도·스턴 시간 산출과 상태 전이는 수신 측(플레이어/물리 파트)이 결정한다.
		IInterface_VehicleHitReceiver::Execute_ReceiveVehicleHit(OtherActor, HitContext);
	}

	// 인터페이스를 구현하지 않는 관찰자(연출·사운드·집계)를 위해 남겨둔다.
	// 서버에서만 발화하므로 클라 연출에는 쓸 수 없다.
	OnVehicleHitActor.Broadcast(this, OtherActor, HitDirection);
}

FVector ABaseVehicle::ResolveHitLocation(const AActor* HitActor, bool bFromSweep, const FHitResult& SweepResult) const
{
	// 스윕으로 발생한 오버랩이 아니면 SweepResult는 비어 있다.
	// 차량은 SetActorTransform으로 위치를 덮어쓰므로 대부분 이 경우다.
	if (bFromSweep)
	{
		return SweepResult.ImpactPoint;
	}

	const FVector TargetLocation = HitActor ? HitActor->GetActorLocation() : GetActorLocation();

	FVector ClosestPoint = FVector::ZeroVector;
	if (CollisionBox && CollisionBox->GetClosestPointOnCollision(TargetLocation, ClosestPoint) >= 0.f)
	{
		return ClosestPoint;
	}

	// 대상이 이미 박스 안쪽이면 최근접점을 못 구한다. 그때는 대상 위치가 곧 접점에 가깝다.
	return TargetLocation;
}
