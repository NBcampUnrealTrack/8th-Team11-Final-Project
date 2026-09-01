// Copyright Crosswalk Team. All Rights Reserved.

#include "CWBaseVehicle.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "CWTrafficLane.h"
#include "CWTrafficSubsystem.h"
#include "Data/Structs/CWVehicleHitContext.h"
#include "Interfaces/Interface_VehicleHitReceiver.h"

ACWBaseVehicle::ACWBaseVehicle()
{
	// 클라이언트도 업데이트 사이를 스스로 메우므로 양쪽에서 돈다.
	PrimaryActorTick.bCanEverTick = true;

	// 스폰과 판정은 서버 권위다.
	bReplicates = true;

	// 트랜스폼은 복제하지 않는다. 클라이언트는 DistanceAlongLane 으로 위치를 직접 계산한다.
	// (이유는 클래스 주석의 네트워크 항목 참고)
	SetReplicateMovement(false);

	// 진행 거리는 클라이언트가 추정으로 메우므로 자주 보낼 이유가 없다.
	// 20 이면 업데이트 사이가 50ms 다. 기본 속도 750cm/s 기준 추정 구간이 37cm 라
	// 추정이 어긋나도 보정량이 눈에 띄지 않는다.
	SetNetUpdateFrequency(20.f);

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

void ACWBaseVehicle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 차선과 개체별 편차는 스폰 직후 한 번 정해지고 끝이라, 초기 번치 이후로는 오가지 않는다.
	// 주기적으로 실제 흐르는 것은 DistanceAlongLane 하나뿐이다.
	DOREPLIFETIME(ACWBaseVehicle, CurrentLane);
	DOREPLIFETIME(ACWBaseVehicle, DistanceAlongLane);
	DOREPLIFETIME(ACWBaseVehicle, SpeedMultiplier);
	DOREPLIFETIME(ACWBaseVehicle, LaneLateralOffset);

	DOREPLIFETIME(ACWBaseVehicle, bParried);
	DOREPLIFETIME(ACWBaseVehicle, ParryLaunchSerial);
	DOREPLIFETIME(ACWBaseVehicle, ParriedLaunchLocation);
	DOREPLIFETIME(ACWBaseVehicle, ParriedLaunchDirection);
}

void ACWBaseVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	/**
	 * 겹쳐 있는데 아직 못 친 대상을 지난 프레임 속도·위치로 다시 본다.
	 * 랙 보정으로 보류됐거나 속도가 하한을 넘어선 대상이 여기서 뒤늦게 성립한다.
	 *
	 * 이번 프레임 이동을 반영하려면 아래 두 경로(튕겨나가는 중 / 차선 주행)가 각자
	 * 속도를 채운 뒤에 불러야 하는데, 그러면 호출 지점이 둘로 갈라져 한쪽을 빼먹기 쉽다.
	 * 판정이 한 프레임 늦는 것이 전부이고, 그 어긋남은 보정량(핑)에 비하면 무시할 만하다.
	 */
	RetryPendingHits();

	// 패링당한 차량은 차선을 벗어나 튕겨나간 방향으로 직진한다.
	// 앞차와의 간격도 보지 않는다. 소멸은 패링 시 걸어둔 타이머가 맡는다. (서버 전용)
	if (bParried)
	{
		CurrentVelocity = GetActorForwardVector() * GetEffectiveSpeed();
		AddActorWorldOffset(CurrentVelocity * DeltaSeconds);
		return;
	}

	if (!CurrentLane)
	{
		return;
	}

	// 클라이언트도 같은 계산을 돈다. 속도를 정하는 값이 양쪽에 모두 있어서 결과가 같고,
	// 서버 값이 도착하면 DistanceAlongLane 이 그 값으로 덮이면서 어긋난 만큼만 맞춰진다.
	bool bStopLineLimited = false;
	float RemainingToStop = 0.f;
	const float StepSpeed = ResolveStepSpeed(&bStopLineLimited, &RemainingToStop);
	DistanceAlongLane += StepSpeed * DeltaSeconds;

	if (DistanceAlongLane >= CurrentLane->GetLaneLength())
	{
		if (HasAuthority())
		{
			OnReachedLaneEnd();
			return;
		}

		// 클라이언트는 차선 끝에서 멈춰 서서 서버의 소멸 통보를 기다린다.
		// 추정이 서버보다 앞서 있을 수 있으므로 스플라인 밖으로 넘기지 않는다.
		DistanceAlongLane = CurrentLane->GetLaneLength();
	}

	UpdateTransformOnLane(StepSpeed);

	LogStopLineSync(bStopLineLimited, StepSpeed, RemainingToStop);
}

float ACWBaseVehicle::GetVehicleLength() const
{
	// 스케일을 적용한 크기로 잰다. 차종 BP 가 콜리전 박스를 키워 놓으면 그만큼 길어져야 한다.
	//
	// GetDistanceToPoint 는 실제 콜리전 바디로 거리를 재기 때문에 이미 스케일이 반영돼 있다.
	// 여기만 스케일 전 크기를 쓰면 두 기준이 갈려서, 패링 반경 판정과 스포너 간격 계산이
	// 서로 다른 차량 크기를 보게 된다.
	return CollisionBox ? CollisionBox->GetScaledBoxExtent().X * 2.f : 0.f;
}

void ACWBaseVehicle::InitializeOnLane(ACWTrafficLane* InLane, const FCWTrafficVehicleSpawnParams& Params)
{
	CurrentLane = InLane;
	DistanceAlongLane = Params.StartDistance;
	LaneLateralOffset = Params.LateralOffset;

	bParried = false;
	ParriedLaunchLocation = FVector::ZeroVector;
	ParriedLaunchDirection = FVector::ZeroVector;

	// 풀링으로 재사용된 차량이 지난 주행의 발사 시각으로 보정량을 깎으면 안 된다.
	ParryLaunchWorldTime = -1.f;

	// 0 이하가 들어오면 차량이 멈춰버리므로 하한을 둔다.
	SpeedMultiplier = FMath::Max(KINDA_SMALL_NUMBER, Params.SpeedMultiplier);

	// 풀링으로 재사용될 때 지난 주행의 피격 기록이 남아 있으면 안 된다.
	HitActors.Reset();
	OverlappingReceivers.Reset();

	UpdateTransformOnLane();
}

bool ACWBaseVehicle::PredictTimeToImpact(const AActor* Target, float& OutTimeToImpact, float ViewRewindSeconds) const
{
	OutTimeToImpact = 0.f;

	/**
	 * 희망 속도가 아니라 지금 실제로 내고 있는 속도로 판정한다.
	 *
	 * 앞차에 막혀 서 있는 차량도 희망 속도로 재면 곧 나를 칠 것처럼 계산되어,
	 * 도로에 정지해 있는 차를 패링할 수 있게 된다. 실제 속도로 재면 0 이라 여기서 걸린다.
	 *
	 * 튕겨나가는 중인 차량도 예측 대상이다. 날아오는 차를 다시 패링할 수 있어야 한다.
	 * 그 구간은 앞차를 보지 않으므로 실제 속도와 희망 속도가 같다.
	 */
	const float CurrentSpeed = GetCurrentSpeed();
	if (!Target || CurrentSpeed <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector Forward = GetActorForwardVector();
	const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();

	const float ForwardDistance = FVector::DotProduct(ToTarget, Forward);

	/**
	 * 랙 보정. 차량 위치를 되돌리는 대신 진행 방향 거리에 되돌릴 만큼을 더한다.
	 *
	 * 차량은 진행 방향으로 등속 직진하므로, ViewRewindSeconds 전의 위치는 지금 위치에서
	 * 정확히 (속도 x 그 시간) 만큼 뒤다. 그래서 진행 방향 성분만 늘려주면 되고,
	 * 좌우로 벗어난 양은 이동이 진행 방향으로만 일어나므로 바뀌지 않는다.
	 *
	 * 위치를 실제로 되돌려 스플라인에서 다시 뽑는 방법도 있지만, 되돌리는 구간이
	 * 100ms 안팎(기본 속도로 75cm)이라 곡률의 영향이 좌우 허용 폭보다 훨씬 작다.
	 *
	 * 넘어온 보정량을 그대로 쓰지 않는다. 위의 등속 직진 가정이 성립하는 구간까지만
	 * 되돌린다. 패링당한 차량은 그 순간 방향이 꺾여서 그 앞 구간에는 가정이 깨진다.
	 */
	const float RewindSeconds = ResolveEffectiveRewindSeconds(ViewRewindSeconds);
	const float RewoundForwardDistance = ForwardDistance + (CurrentSpeed * RewindSeconds);

	// 되돌린 시점에도 이미 지나쳐 있었으면 그때도 칠 수 없는 차였다.
	if (RewoundForwardDistance <= 0.f)
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
	// 크기 기준은 GetVehicleLength 와 맞춘다. (스케일 적용 후)
	const float FrontOffset = CollisionBox ? CollisionBox->GetScaledBoxExtent().X : 0.f;
	OutTimeToImpact = FMath::Max(0.f, (RewoundForwardDistance - FrontOffset) / CurrentSpeed);

	return true;
}

float ACWBaseVehicle::GetDistanceToPoint(const FVector& Point) const
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

bool ACWBaseVehicle::ReceiveParryAttempt_Implementation(const FCWParryAttemptContext& ParryAttemptContext)
{
	// 클라에서 통과시키면 그 클라에서만 차가 멈춘다. 판정은 서버만 한다.
	if (!HasAuthority())
	{
		return false;
	}

	// 튕겨나갈 방향은 플레이어가 찍은 커서 지점 쪽이다.
	// 랙 보정량은 선별 단계가 쓴 값을 그대로 넘겨야 판정이 갈리지 않는다.
	return TryParry(ParryAttemptContext.Parrier, ResolveLaunchDirection(ParryAttemptContext),
		ParryAttemptContext.ViewRewindSeconds);
}

FVector ACWBaseVehicle::ResolveLaunchDirection(const FCWParryAttemptContext& ParryAttemptContext) const
{
	/**
	 * 커서 지점을 향해 날아간다. 기준점이 플레이어가 아니라 이 차량이라는 게 핵심이다.
	 *
	 * 플레이어 기준 방향(AttemptDirection)을 그대로 쓰면, 차량은 커서 쪽이 아니라
	 * 커서 방향과 "평행하게" 날아간다. 플레이어와 차량의 위치가 다르기 때문이다.
	 * 패링 윈도우가 0.3초라 차량이 2m 안쪽에 있어 차이가 크지는 않지만,
	 * 커서를 가까이 찍을수록 어긋남이 커진다.
	 */
	if (ParryAttemptContext.bHasCursorTarget)
	{
		const FVector ToCursor = (ParryAttemptContext.CursorWorldLocation - GetActorLocation()).GetSafeNormal2D();
		if (!ToCursor.IsNearlyZero())
		{
			return ToCursor;
		}
	}

	// 커서가 차량 바로 위에 찍혔거나 트레이스가 빗나갔다. 플레이어 기준 방향으로 대체한다.
	return ParryAttemptContext.AttemptDirection.GetSafeNormal2D();
}

float ACWBaseVehicle::ResolveEffectiveRewindSeconds(float ViewRewindSeconds) const
{
	const float RequestedSeconds = FMath::Max(0.f, ViewRewindSeconds);

	// 발사된 적이 없으면 차선 위를 계속 달려온 차량이다. 되돌릴 시간에 제한이 없다.
	if (!bParried || ParryLaunchWorldTime < 0.f)
	{
		return RequestedSeconds;
	}

	// 시간을 읽을 수 없으면 깎을 근거도 없다. 넘어온 값을 그대로 쓴다.
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return RequestedSeconds;
	}

	/**
	 * 발사 이후 흐른 시간까지만 되돌린다.
	 *
	 * 방금 패링당한 차량은 SetActorRotation 으로 방향이 꺾인 직후다. 그 차를 발사 이전
	 * 시점까지 되돌리면 새 방향의 반대쪽으로 되돌리는데, 그때 차는 거기 없었다.
	 * 차선 위에서 다른 방향으로 달리고 있었다. 날아오는 차를 다시 쳐내는 것은
	 * 스펙이므로(CanBeParried 주석) 이 구간은 반드시 발생한다.
	 *
	 * 어긋나는 양은 (속도 x 깎아낸 시간)이다. 기본값이면 최대 750 x 0.2 = 150cm 로,
	 * 윈도우가 커버하는 거리(750 x 0.3 = 225cm)의 3분의 2다. 그리고 양쪽으로 다 틀린다.
	 * 날아오는 차는 남은 시간이 부풀어 윈도우 밖으로 밀려나 안 쳐지고, 이미 지나쳐 간 차는
	 * 음수 거리가 양수로 뒤집혀서 등 뒤에 있는데도 쳐진다.
	 *
	 * 깎은 만큼은 보정을 포기하는 것이 맞다. 발사 이전 궤적을 들고 있지 않아 그 구간을
	 * 제대로 되돌릴 방법이 없고, 차가 있던 적 없는 위치로 되돌리는 것보다 서버의 지금
	 * 상황으로 판정하는 편이 낫다. 발사 후 보정량 상한을 넘기면 이 제한은 저절로 풀린다.
	 */
	const float SecondsSinceLaunch = FMath::Max(0.f, World->GetTimeSeconds() - ParryLaunchWorldTime);
	if (RequestedSeconds <= SecondsSinceLaunch)
	{
		return RequestedSeconds;
	}

	// 실제로 깎이는 구간은 발사 후 0.2초 안쪽뿐이라 로그가 쌓이지 않는다.
	UE_LOG(LogTemp, Log, TEXT("[Parry]   %s: 발사 직후라 랙 보정을 %.0fms -> %.0fms 로 깎습니다."),
		*GetName(), RequestedSeconds * 1000.f, SecondsSinceLaunch * 1000.f);

	return SecondsSinceLaunch;
}

bool ACWBaseVehicle::TryParry(AActor* Parrier, const FVector& LaunchDirection, float ViewRewindSeconds)
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
	if (!PredictTimeToImpact(Parrier, TimeToImpact, ViewRewindSeconds) || TimeToImpact > ParryWindow)
	{
		return false;
	}

	// 패링당한 차량은 멈추는 게 아니라 커서 방향으로 튕겨나간다.
	// 방향이 비어 있으면 온 길로 되돌려 보낸다.
	const FVector LaunchForward = LaunchDirection.GetSafeNormal2D();
	SetActorRotation((LaunchForward.IsNearlyZero() ? -GetActorForwardVector().GetSafeNormal2D() : LaunchForward).Rotation());

	// 클라이언트가 같은 지점·같은 자세에서 직진을 시작할 수 있도록 남긴다.
	// 회전이 끝난 뒤의 정면이라 차가 실제로 가는 쪽과 항상 일치한다.
	ParriedLaunchLocation = GetActorLocation();
	ParriedLaunchDirection = GetActorForwardVector().GetSafeNormal2D();

	// 두 번째 패링부터는 bParried 가 이미 true 라서 값이 바뀌지 않아 알림이 뜨지 않는다.
	// 이 번호가 매번 달라지면서 클라이언트가 새 발사를 반영하게 만든다.
	++ParryLaunchSerial;

	bParried = true;

	// 방향이 꺾인 시각. 랙 보정이 이 시점보다 앞까지 되돌리지 않도록 남긴다.
	// 여기서부터 등속 직진이 시작되므로, 그 이전 구간은 되돌릴 근거가 없다.
	ParryLaunchWorldTime = GetWorld()->GetTimeSeconds();

	// 차선에서 뺀다. 그대로 두면 뒤차들이 튕겨나간 차를 앞차로 보고 줄줄이 멈춘다.
	CurrentLane = nullptr;

	/**
	 * 쳐낸 사람이 지금 차와 겹쳐 있으면 이번 겹침 동안만 피격에서 뺀다.
	 * 충돌 직전에 쳐낸 경우라 차와 몸이 이미 닿아 있고, 그대로 두면 패링에 성공하자마자 맞는다.
	 *
	 * 겹치지 않은 상태에서 쳐냈으면 아무것도 하지 않는다. 커서를 자기 쪽으로 찍어
	 * 차를 자신에게 날려보냈다면 그건 맞아야 하는 상황이다.
	 *
	 * 어느 쪽이든 차에서 떨어지는 순간 HandleEndOverlap 이 기록을 지우므로,
	 * 다시 닿으면 쳐낸 사람도 똑같이 피격된다.
	 */
	if (CollisionBox && CollisionBox->IsOverlappingActor(Parrier))
	{
		HitActors.Add(Parrier);
	}

	/**
	 * 튕겨나간 뒤 ParriedDespawnDelay 동안 이동하고 사라진다.
	 * 다시 패링당하면 SetTimer 가 기존 타이머를 덮어써 그 시간이 새로 시작된다.
	 *
	 * 값이 0 이하로 들어와도 타이머는 반드시 건다. 조건부로 걸면 그 경우 소멸 경로가
	 * 사라져서 차량이 맵 밖까지 영원히 날아간다. "즉시 소멸" 이 필요하면 0 이 그 뜻이 된다.
	 */
	const float DespawnDelay = FMath::Max(ParriedDespawnDelay, KINDA_SMALL_NUMBER);
	GetWorldTimerManager().SetTimer(ParriedDespawnTimerHandle, this, &ACWBaseVehicle::OnReachedLaneEnd, DespawnDelay, false);

	// 여기까지는 서버에서만 돈다. 연출은 이 RPC 로 모든 머신에 따로 뿌린다.
	Multicast_PlayParriedFX(ParriedLaunchDirection, GetEffectiveSpeed(), Parrier);

	return true;
}

void ACWBaseVehicle::Multicast_PlayParriedFX_Implementation(FVector_NetQuantizeNormal LaunchDirection, float LaunchSpeed, AActor* Parrier)
{
	// C++ 은 통보만 하고 무엇을 재생할지는 차량 BP 가 정한다.
	OnParriedFX(LaunchDirection, LaunchSpeed, Parrier);
}

void ACWBaseVehicle::OnRep_ParryLaunch()
{
	// 풀링으로 재사용된 차량은 번호가 남아 있을 수 있다. 실제로 튕겨나간 상태만 반영한다.
	if (!bParried)
	{
		return;
	}

	// 서버도 차선을 끊고 그 값이 복제되지만, 도착 순서에 기대지 않도록 여기서도 끊는다.
	// 이걸 놓치면 이 차량이 다른 차량의 앞차 판정에 남아 뒤차들이 클라이언트에서만 멈춘다.
	CurrentLane = nullptr;

	// 회전은 복제되지 않으므로 직접 세운다. 위치도 서버가 튕겨낸 지점으로 맞춰,
	// 마지막 진행 거리 업데이트와 패링 사이의 어긋남을 여기서 없앤다.
	if (!ParriedLaunchDirection.IsNearlyZero())
	{
		SetActorLocationAndRotation(ParriedLaunchLocation, ParriedLaunchDirection.Rotation());
	}
}

void ACWBaseVehicle::BeginPlay()
{
	Super::BeginPlay();

	CollisionBox->OnComponentBeginOverlap.AddDynamic(this, &ACWBaseVehicle::HandleBeginOverlap);
	CollisionBox->OnComponentEndOverlap.AddDynamic(this, &ACWBaseVehicle::HandleEndOverlap);

	if (UCWTrafficSubsystem* TrafficSubsystem = UCWTrafficSubsystem::Get(this))
	{
		TrafficSubsystem->RegisterVehicle(this);
	}
}

void ACWBaseVehicle::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ParriedDespawnTimerHandle);
	}

	if (UCWTrafficSubsystem* TrafficSubsystem = UCWTrafficSubsystem::Get(this))
	{
		TrafficSubsystem->UnregisterVehicle(this);
	}

	Super::EndPlay(EndPlayReason);
}

void ACWBaseVehicle::OnReachedLaneEnd()
{
	// 풀링을 붙이면 Destroy 대신 스포너에 반납하도록 자식에서 오버라이드한다.
	Destroy();
}

float ACWBaseVehicle::ResolveStepSpeed(bool* OutStopLineLimited, float* OutRemainingToStop) const
{
	const float DesiredSpeed = GetEffectiveSpeed();

	const UCWTrafficSubsystem* TrafficSubsystem = UCWTrafficSubsystem::Get(this);
	if (!TrafficSubsystem)
	{
		return DesiredSpeed;
	}

	/**
	 * 앞차와 정지선을 각자 계산해서 더 느린 쪽을 쓴다.
	 *
	 * 두 감속을 하나의 식으로 합치면 안 된다. **제동 거리가 다르기 때문이다.**
	 * 앞차는 나와 비슷한 속도로 달리고 있어서 상대속도가 0 에 가깝고, FollowingGap
	 * 정도의 짧은 구간으로 충분히 흡수된다. 정지선은 상대속도가 주행 속도 그대로라
	 * 같은 구간을 쓰면 0.2초 만에 서는 급제동이 된다.
	 *
	 * 실제로 그렇게 만들어 놓고 "정지선이 이상하게 동작한다" 로 하루를 썼다.
	 * 정지선을 "길이 0인 가상의 앞차" 로 넣는 비유가 너무 잘 맞아서, 서는 자리는
	 * 분리했으면서 제동 거리는 앞차 값을 그대로 쓰고 있었다.
	 */
	float StepSpeed = DesiredSpeed;

	if (const ACWBaseVehicle* VehicleAhead = TrafficSubsystem->FindVehicleAhead(this))
	{
		const float BumperToBumper = VehicleAhead->GetDistanceAlongLane() - DistanceAlongLane
			- (GetVehicleLength() * 0.5f) - (VehicleAhead->GetVehicleLength() * 0.5f);

		StepSpeed = FMath::Min(StepSpeed, ResolveApproachSpeed(BumperToBumper - FollowingGap, FollowingGap, DesiredSpeed));
	}

	float StopLineDistance = 0.f;
	if (TrafficSubsystem->FindActiveStopLine(CurrentLane, DistanceAlongLane, StopLineDistance))
	{
		// 앞범퍼에서 정지선까지. 여기서 StopLineGap 을 빼면 실제로 서야 하는 자리까지의 거리다.
		const float ToStopLine = StopLineDistance - DistanceAlongLane - (GetVehicleLength() * 0.5f);

		const float RemainingToStop = ToStopLine - StopLineGap;
		const float StopLineSpeed = ResolveApproachSpeed(RemainingToStop, StopLineBrakeDistance, DesiredSpeed);

		if (OutRemainingToStop)
		{
			*OutRemainingToStop = RemainingToStop;
		}

		// 정지선이 앞차보다 더 느리게 만들었을 때만 정지선 탓이다. 둘을 뭉치면 로그가
		// "정지선 앞에 섰다" 라고 말하는데 실제로는 앞차에 막힌 상황이 섞여 들어온다.
		if (OutStopLineLimited)
		{
			*OutStopLineLimited = StopLineSpeed <= StepSpeed;
		}

		StepSpeed = FMath::Min(StepSpeed, StopLineSpeed);
	}

	return StepSpeed;
}

void ACWBaseVehicle::LogStopLineSync(bool bStopLineLimited, float StepSpeed, float RemainingToStop)
{
	const bool bNowStopped = bStopLineLimited && StepSpeed <= KINDA_SMALL_NUMBER;
	if (bNowStopped == bStoppedAtStopLine)
	{
		return;
	}

	// 로그를 꺼 두었어도 상태는 따라간다. 도중에 켰을 때 지나간 사건이 한꺼번에 쏟아지지 않게 한다.
	bStoppedAtStopLine = bNowStopped;

	if (!UCWTrafficSubsystem::IsSyncLoggingEnabled())
	{
		return;
	}

	if (bNowStopped)
	{
		UE_LOG(LogTemp, Log, TEXT("[TrafficSync][%s] 차량 %s 정지선 앞 정지 - 차선 %s / 진행거리 %.1fcm / 서야 할 자리까지 %.1fcm"),
			UCWTrafficSubsystem::GetNetRoleTag(this),
			*UCWTrafficSubsystem::GetNetIdTag(this),
			CurrentLane ? *CurrentLane->GetName() : TEXT("(없음)"),
			DistanceAlongLane,
			RemainingToStop);

		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[TrafficSync][%s] 차량 %s 정지선에서 출발 - 진행거리 %.1fcm"),
		UCWTrafficSubsystem::GetNetRoleTag(this),
		*UCWTrafficSubsystem::GetNetIdTag(this),
		DistanceAlongLane);
}

float ACWBaseVehicle::ResolveApproachSpeed(float Remaining, float BrakeDistance, float DesiredSpeed)
{
	if (Remaining <= 0.f)
	{
		// 이미 서야 할 자리를 지났으면 멈춰 세운다. 통과보다 정지가 덜 어색하다.
		return 0.f;
	}

	// 제동 구간이 없으면 줄일 구간도 없다. 붙기 직전까지 제 속도로 간다.
	if (BrakeDistance <= KINDA_SMALL_NUMBER)
	{
		return DesiredSpeed;
	}

	/**
	 * 남은 거리에 비례해 속도를 줄인다. 제동 구간만큼 남아 있으면 제 속도이고,
	 * 0 에 가까워질수록 0 으로 수렴한다.
	 *
	 * "이번 프레임에 갈 수 있는 거리 / DeltaSeconds" 로 깎으면 안 된다. 뒤차가 앞차를
	 * 따라붙어 이 계산에 걸리는 순간부터 속도가 프레임 시간에 묶이기 때문이다.
	 * 앞차를 따라가는 상태에서는 여유가 대략 (앞차 속도 x 지난 프레임 시간) 으로 유지되므로,
	 * 이번 속도가 앞차 속도 x (지난 프레임 시간 / 이번 프레임 시간) 이 된다.
	 * 프레임 간격이 몇 % 흔들리면 속도도 그만큼 흔들려서 차가 미세하게 주춤거린다.
	 *
	 * 서버와 클라이언트가 같은 코드를 각자 돌리는 구조라 문제가 하나 더 있다.
	 * 프레임 시간이 기계마다 다르므로 같은 상황에서 속도가 갈리고, 그만큼 클라이언트가
	 * 서버 값을 받을 때마다 보정된다. 거리만 보고 정하면 양쪽 결과가 같아진다.
	 *
	 * 남은 거리가 0 에 가까울수록 속도도 0 에 가까워지므로 한 프레임에 통과하지 못한다.
	 * (앞차가 멈춰 있을 때 음수로 넘어가려면 프레임 시간이 제동 구간 / 속도,
	 *  기본값 기준 0.2초를 넘어야 한다. 그 경우도 다음 프레임에 위에서 0 으로 걸린다)
	 */
	return DesiredSpeed * FMath::Min(Remaining / BrakeDistance, 1.f);
}

void ACWBaseVehicle::UpdateTransformOnLane(float StepSpeed)
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

	// 리플리케이션이 실어 보낼 속도. 앞차에 막혀 StepSpeed가 0이면 정지 상태로 잡힌다.
	CurrentVelocity = LaneTransform.GetRotation().GetForwardVector() * StepSpeed;

	// 위치와 회전만 가져간다. 스케일은 차량 BP 가 정한 값을 그대로 둔다.
	//
	// 스플라인이 내주는 월드 트랜스폼에는 ACWTrafficLane 액터의 스케일이 섞여 있다.
	// SetActorTransform 으로 통째로 덮으면 차선을 늘려 놓은 만큼 차량도 같이 커지는데,
	// GetVehicleLength 가 재는 콜리전 크기와 화면에 보이는 크기가 어긋나면서
	// 스포너의 간격 계산이 실제보다 짧아져 차가 겹쳐 스폰된다.
	SetActorLocationAndRotation(LaneTransform.GetLocation(), LaneTransform.GetRotation());
}

void ACWBaseVehicle::HandleBeginOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool bFromSweep, const FHitResult& SweepResult)
{
	// 겹치는 동안 다시 볼 수 있게 기록해 둔다. 인터페이스를 구현한 대상만 담는다.
	// (겹침 자체는 서버·클라 양쪽에서 일어나지만, 재검사는 서버만 하므로 서버에서만 채운다)
	if (HasAuthority() && OtherActor && OtherActor != this
		&& OtherActor->Implements<UInterface_VehicleHitReceiver>())
	{
		OverlappingReceivers.Add(OtherActor);
	}

	TryApplyHit(OtherActor, bFromSweep, SweepResult);
}

void ACWBaseVehicle::RetryPendingHits()
{
	// 피격은 서버만 확정한다.
	if (!HasAuthority() || OverlappingReceivers.IsEmpty())
	{
		return;
	}

	/**
	 * 순회 중에 TryApplyHit 가 피격을 넣고, 그 수신 측이 넉백으로 대상을 차 밖으로 밀어내
	 * HandleEndOverlap 이 이 집합을 건드릴 수 있다. 사본으로 돌아야 안전하다.
	 */
	TArray<TWeakObjectPtr<AActor>> PendingReceivers = OverlappingReceivers.Array();

	for (const TWeakObjectPtr<AActor>& ReceiverPtr : PendingReceivers)
	{
		AActor* Receiver = ReceiverPtr.Get();
		if (!Receiver)
		{
			// 겹친 채로 사라진 대상. EndOverlap 이 오지 않으므로 여기서 지운다.
			OverlappingReceivers.Remove(ReceiverPtr);
			continue;
		}

		// 이미 친 대상은 TryApplyHit 이 기록으로 걸러낸다. 여기서 다시 볼 것은
		// 아직 못 친 대상뿐이라, 조건이 갖춰지는 순간 그 프레임에 성립한다.
		//
		// 스윕 정보가 없으므로 피격 지점은 최근접점으로 대체된다. (ResolveHitLocation)
		TryApplyHit(Receiver, false, FHitResult());
	}
}

float ACWBaseVehicle::ResolveTargetViewRewindSeconds(const AActor* Target) const
{
	// 보정을 끈 설정이면 되돌릴 것이 없다.
	if (MaxHitRewindSeconds <= 0.f)
	{
		return 0.f;
	}

	/**
	 * 호스트가 직접 조종하는 대상은 되돌리지 않는다. 자기가 보는 것이 곧 서버 상황이라
	 * 어긋남 자체가 없고, 되돌리면 오히려 맞아야 할 피격을 흘려보낸다.
	 */
	const APawn* TargetPawn = Cast<APawn>(Target);
	if (TargetPawn == nullptr || TargetPawn->IsLocallyControlled())
	{
		return 0.f;
	}

	const APlayerState* TargetPlayerState = TargetPawn->GetPlayerState();
	if (TargetPlayerState == nullptr)
	{
		return 0.f;
	}

	/**
	 * 되돌릴 시간은 왕복 시간(ping) 전체다. 절반이 아니다.
	 * 근거는 패링 쪽과 같다. (UCWParryComponent::ResolveViewRewindSeconds 주석)
	 *
	 * 그 사람 화면의 차량은 서버가 보낸 값을 받아 그린 것이라 편도만큼 뒤처져 있고,
	 * 그 화면을 보고 피한 결과가 서버에 닿는 데 다시 편도가 걸린다.
	 */
	const float PingSeconds = TargetPlayerState->GetPingInMilliseconds() * 0.001f;

	return FMath::Clamp(PingSeconds, 0.f, MaxHitRewindSeconds);
}

bool ACWBaseVehicle::PassesHitLagCompensation(const AActor* Target) const
{
	const float RewindSeconds = ResolveTargetViewRewindSeconds(Target);
	if (RewindSeconds <= 0.f)
	{
		return true;
	}

	/**
	 * 되돌릴 거리. 차량은 진행 방향으로 등속 직진하므로, RewindSeconds 전의 차량은
	 * 지금 위치에서 정확히 이만큼 뒤에 있었다. (PredictTimeToImpact 의 랙 보정과 같은 가정)
	 *
	 * 희망 속도가 아니라 실제 속도를 쓴다. 앞차에 막혀 느리게 굴러온 차를 제 속도로
	 * 되돌리면 있지도 않았던 위치까지 물러나 맞아야 할 피격을 흘려보낸다.
	 */
	const float RewindDistance = GetCurrentSpeed() * RewindSeconds;
	if (RewindDistance <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	/**
	 * 차가 대상을 얼마나 파고들었는가. 앞면이 대상에 막 닿은 순간이 0 이고,
	 * 더 지나갈수록 커진다. 기준은 GetVehicleLength 와 같은 스케일 적용 후 크기다.
	 */
	const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	const float ForwardDistance = FVector::DotProduct(ToTarget, GetActorForwardVector());
	const float FrontOffset = CollisionBox ? CollisionBox->GetScaledBoxExtent().X : 0.f;
	const float PenetrationDepth = FrontOffset - ForwardDistance;

	/**
	 * 파고든 깊이가 되돌릴 거리보다 얕으면, 되돌린 차의 앞면은 아직 대상 뒤에 있다.
	 * 그 사람 화면에서는 차가 안 왔다는 뜻이라 이번 판정은 흘려보낸다.
	 *
	 * 몸을 빼면 겹침이 끝나면서 영영 안 맞고(피한 것이 맞다), 가만히 있으면 차가 계속
	 * 파고들어 이 조건을 넘는 순간 맞는다. 그 순간이 곧 그 사람 화면에서 차가 닿는 때다.
	 */
	return PenetrationDepth >= RewindDistance;
}

void ACWBaseVehicle::TryApplyHit(AActor* OtherActor, bool bFromSweep, const FHitResult& SweepResult)
{
	// 튕겨나가는 중에도 피격 판정은 살아 있다. 날아간 차가 다른 플레이어를 치는 게
	// 패링이 노리는 그림이다. 쳐낸 사람도 차에서 떨어졌다가 다시 닿으면 똑같이 맞는다.
	if (!HasAuthority() || !OtherActor || OtherActor == this)
	{
		return;
	}

	/**
	 * 너무 느린 차는 치지 않는다. 앞차에 막혀 서 있는 차에 걸어 들어간 경우다.
	 *
	 * 여기서 기록(HitActors)을 남기지 않는 것이 중요하다. 남기면 그 차가 다시 출발해도
	 * 이미 판정한 대상으로 걸러져서 영영 맞지 않는다. 대신 RetryPendingHits 가
	 * 겹쳐 있는 동안 매 틱 다시 보므로, 속도가 하한을 넘는 프레임에 그대로 성립한다.
	 */
	if (GetCurrentSpeed() < MinHitSpeed)
	{
		return;
	}

	// 피격 인터페이스를 구현한 액터만 대상으로 본다.
	// ACWPlayerCharacter 가 이 인터페이스를 구현하므로 폰을 따로 받아줄 이유가 없다.
	if (!OtherActor->Implements<UInterface_VehicleHitReceiver>())
	{
		return;
	}

	/**
	 * 이 사람의 화면에서도 실제로 닿았는지 본다. 아직이면 이번 판정은 흘려보낸다.
	 *
	 * 기록(HitActors)을 남기지 않는 것이 핵심이다. 남기면 화면에서 곧 닿을 사람도
	 * 이미 판정한 대상으로 걸러져 영영 안 맞는다. 대신 겹쳐 있는 동안 RetryPendingHits 가
	 * 매 틱 다시 보므로, 파고든 깊이가 보정 거리를 넘는 순간에 정상 피격이 성립한다.
	 */
	if (!PassesHitLagCompensation(OtherActor))
	{
		return;
	}

	// 한 번의 접촉이 여러 프레임에 걸쳐 중복으로 들어가지 않게 막는다.
	// 차에서 떨어지면 기록이 지워지므로, 떨어졌다 다시 닿는 건 새로운 피격이다.
	if (HitActors.Contains(OtherActor))
	{
		return;
	}
	HitActors.Add(OtherActor);

	FCWVehicleHitContext HitContext;
	HitContext.HitLocation = ResolveHitLocation(OtherActor, bFromSweep, SweepResult);
	HitContext.HitDirection = GetActorForwardVector();
	// 실제로 내고 있는 속도를 넘긴다. 앞차에 막혀 서 있는 차에 걸어 들어간 경우
	// 희망 속도를 넘기면 전속력으로 치인 것과 같은 값이 된다.
	HitContext.VehicleSpeed = GetCurrentSpeed();
	HitContext.VehicleType = VehicleType;
	HitContext.bCanParry = bCanBeParried;
	HitContext.bFlattenOnHit = bFlattenOnHit;

	// 넉백 강도·스턴 시간 산출과 상태 전이는 수신 측(플레이어/물리 파트)이 결정한다.
	IInterface_VehicleHitReceiver::Execute_ReceiveVehicleHit(OtherActor, HitContext);
}

void ACWBaseVehicle::HandleEndOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/)
{
	// 피격 기록은 서버만 들고 있다.
	if (!HasAuthority() || !OtherActor)
	{
		return;
	}

	HitActors.Remove(OtherActor);

	// 겹침이 끝났으니 재검사 대상에서도 뺀다. 랙 보정으로 보류된 채 몸을 뺀 대상은
	// 여기서 빠지면서 영영 안 맞게 된다 - 화면에서 피한 것이므로 그게 맞다.
	OverlappingReceivers.Remove(OtherActor);
}

FVector ACWBaseVehicle::ResolveHitLocation(const AActor* HitActor, bool bFromSweep, const FHitResult& SweepResult) const
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
