// CWBaseVehicle.cpp

#include "CWBaseVehicle.h"

#include "Components/BoxComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "CWTrafficDiagnostics.h"
#include "CWTrafficLane.h"
#include "CWTrafficMath.h"
#include "CWTrafficSubsystem.h"
#include "Core/CWDecalStatics.h"
#include "Traffic/Components/CWVehicleAppearanceComponent.h"
#include "Traffic/Components/CWVehicleAudioComponent.h"
#include "Traffic/Components/CWVehicleHitDetectionComponent.h"
#include "Traffic/CWTrafficLog.h"
#include "Data/CWVehicleSpeedRow.h"

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

	// 원점을 노면에 고정하는 빈 루트. 콜리전 박스가 루트를 맡을 수 없는 이유는 헤더 주석 참고.
	VehicleRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VehicleRoot"));
	SetRootComponent(VehicleRoot);

	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	CollisionBox->SetupAttachment(VehicleRoot);
	// 승용차 대략치. 플레이스홀더 메쉬 크기와 맞춰 조정한다.
	ApplyCollisionExtent(FVector(220.f, 90.f, 70.f));
	CollisionBox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	CollisionBox->SetGenerateOverlapEvents(true);

	// 박스가 아니라 루트에 붙인다. 박스는 높이의 절반만큼 올라가 있어서,
	// 거기에 붙이면 메쉬도 같이 떠올라 차가 공중에 뜬 채로 다닌다.
	VehicleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VehicleMesh"));
	VehicleMesh->SetupAttachment(VehicleRoot);
	VehicleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 바퀴가 도는 차종용. 정적 메쉬와 같은 자리에 서고 변형이 지정한 쪽만 채워진다.
	VehicleSkeletalMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("VehicleSkeletalMesh"));
	VehicleSkeletalMesh->SetupAttachment(VehicleRoot);
	VehicleSkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 엔진음·경적·도플러. 소리가 나지 않아도 주행에는 영향이 없다.
	AudioComponent = CreateDefaultSubobject<UCWVehicleAudioComponent>(TEXT("AudioComponent"));

	// 차체·색 변형과 바퀴. 변형 목록은 이 액터의 MeshVariants 이고 컴포넌트는 뽑아 씌우기만 한다.
	AppearanceComponent = CreateDefaultSubobject<UCWVehicleAppearanceComponent>(TEXT("AppearanceComponent"));

	// 플레이어 피격 판정. 콜리전 박스의 오버랩을 BeginPlay 에서 스스로 바인딩한다.
	HitDetectionComponent = CreateDefaultSubobject<UCWVehicleHitDetectionComponent>(TEXT("HitDetectionComponent"));
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
	// 차체·색(VariantPick)은 외형 컴포넌트가 자기 GetLifetimeReplicatedProps 에서 복제한다.

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
	HitDetectionComponent->RetryPendingHits();

	// 바퀴 각도. 아래 두 경로가 각자 return 으로 빠져나가므로 갈라지기 전에 한 번만 민다.
	AppearanceComponent->UpdateWheelSpin(DeltaSeconds);

	// 패링당한 차량은 차선을 벗어나 튕겨나간 방향으로 직진한다.
	// 앞차와의 간격도 보지 않는다. 소멸은 패링 시 걸어둔 타이머가 맡는다. (서버 전용)
	if (bParried)
	{
		// 차선을 벗어난 차는 더 이상 정지선을 보지 않는다. 서 있다가 튕겨나간 경우
		// 이걸 안 풀면 날아가는 중에도 IsStoppedAtStopLine 이 true 로 남는다.
		StopTargetDistance = -1.f;
		UpdateStopState(false);

		// 차선을 벗어난 뒤로 DistanceAlongLane 은 아무도 안 본다. 남은 빚도 같이 버린다.
		DistanceCorrectionRemaining = 0.f;

		CurrentVelocity = GetActorForwardVector() * GetEffectiveSpeed();
		AddActorWorldOffset(CurrentVelocity * DeltaSeconds);
		return;
	}

	if (!CurrentLane)
	{
		return;
	}

	// 클라이언트도 같은 계산을 돈다. 속도를 정하는 값이 양쪽에 모두 있어서 결과가 같고,
	// 어긋난 만큼은 서버 값이 도착할 때마다 조금씩 좁혀진다. (OnRep_DistanceAlongLane)
	const FCWVehicleStepResolution Step = ResolveStepSpeed();

	// 결과가 다 정해진 뒤에 멤버를 갱신한다. 정지선이 사라진 프레임에는 -1 이 그대로 들어온다.
	StopTargetDistance = Step.StopTargetDistance;

	/**
	 * 목표 지점 코앞까지 왔으면 거기에 못 박고 끝낸다.
	 *
	 * 감속식이 지수형이라 굴러서는 목표에 도착하지 못한다. 확정을 안 하면 차가 영원히
	 * 미세하게 기어가고, 값이 계속 바뀌니 리플리케이션도 끊기지 않아서 클라이언트는
	 * 50ms 마다 보정을 받는다. 정지선 앞 떨림의 출처가 여기다.
	 *
	 * 넣는 값이 서버와 클라이언트에서 같다는 점이 중요하다. 목표 지점은 양쪽이 정지선
	 * 위치와 차체 길이로 각자 계산하므로, 통신 없이도 두 기계가 같은 자리에 선다.
	 * 그래서 갚을 빚도 같이 없앤다 - 남겨 두면 확정해 놓고 도로 밀어낸다.
	 */
	if (Step.bStopLineLimited && Step.StopTargetDistance >= 0.f
		&& Step.StopTargetDistance - DistanceAlongLane <= StopSettleTolerance)
	{
		DistanceAlongLane = Step.StopTargetDistance;
		LocalDistanceAlongLane = DistanceAlongLane;
		DistanceCorrectionRemaining = 0.f;

		UpdateTransformOnLane(0.f);
		UpdateStopState(true);
		return;
	}

	DistanceAlongLane += Step.Speed * DeltaSeconds;

	// 로컬 추정으로 나아간 뒤에 갚는다. 순서가 반대면 이번 프레임 이동이 보정을 도로 밀어낸다.
	if (!HasAuthority())
	{
		ApplyServerDistanceCorrection(DeltaSeconds);
	}

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

	// 이번 프레임 값이 확정된 자리. 다음 수신이 덮기 전의 로컬 값으로 이걸 본다.
	LocalDistanceAlongLane = DistanceAlongLane;

	UpdateTransformOnLane(Step.Speed);

	// 여기까지 왔으면 확정 구간에 못 들어왔다는 뜻이라 아직 선 것이 아니다.
	// 정지 확정은 위 분기 하나가 전담한다. 속도로 다시 판정하면 판정 기준이 둘로 갈린다.
	UpdateStopState(false);
}

float ACWBaseVehicle::GetVehicleLength() const
{
	return GetVehicleHalfLength() * 2.f;
}

float ACWBaseVehicle::GetVehicleHalfLength() const
{
	// 스케일을 적용한 크기로 잰다. 차종 BP 가 콜리전 박스를 키워 놓으면 그만큼 길어져야 한다.
	//
	// GetDistanceToPoint 는 실제 콜리전 바디로 거리를 재기 때문에 이미 스케일이 반영돼 있다.
	// 여기만 스케일 전 크기를 쓰면 두 기준이 갈려서, 패링 반경 판정과 스포너 간격 계산이
	// 서로 다른 차량 크기를 보게 된다.
	return CollisionBox ? CollisionBox->GetScaledBoxExtent().X : 0.f;
}

float ACWBaseVehicle::GetWheelSpinDegrees() const
{
	return AppearanceComponent ? AppearanceComponent->GetWheelSpinDegrees() : 0.f;
}

void ACWBaseVehicle::InitializeOnLane(ACWTrafficLane* InLane, const FCWTrafficVehicleSpawnParams& Params)
{
	CurrentLane = InLane;
	DistanceAlongLane = Params.StartDistance;
	LocalDistanceAlongLane = Params.StartDistance;
	LaneLateralOffset = Params.LateralOffset;

	/**
	 * 아래의 되돌리기는 전부 "이 액터가 이전 주행을 가진 채 다시 초기화되는" 경우를 위한 것이다.
	 *
	 * 지금은 그런 경우가 없다. 차량은 스폰마다 새 액터라 이 값들은 이미 초기값이다.
	 * 차량 풀링은 팀 결정으로 보류다 - 스폰 빈도가 낮아 얻는 것이 없다. 그래도 되돌리는 것은,
	 * 이 함수가 한 액터에 두 번 불리는 구조가 나중에 생겼을 때 조용히 틀리는 것보다
	 * 여기서 대여섯 줄 쓰는 편이 싸기 때문이다. 설계 전제가 아니라 방어다.
	 */
	bParried = false;
	ParriedLaunchLocation = FVector::ZeroVector;
	ParriedLaunchDirection = FVector::ZeroVector;

	// 이전 주행의 발사 시각으로 보정량을 깎으면 안 된다.
	ParryLaunchWorldTime = -1.f;

	// 이전 주행에서 정지선 앞에 서 있던 상태를 갖고 나오면 안 된다.
	// 알리지 않고 되돌린다. 초기화 직후라 아직 아무도 이 차량을 듣고 있지 않고,
	// 앞 주행의 정지가 이제 와서 풀렸다는 알림은 받는 쪽에서 해석할 수 없다.
	bStoppedAtStopLine = false;
	StopTargetDistance = -1.f;

	// 이전 주행에서 남은 빚을 새 출발 지점에 갚으면 차가 엉뚱하게 끌려간다.
	DistanceCorrectionRemaining = 0.f;

	// 속도 배율은 차체가 뽑힌 뒤 RollSpeedForVariant 가 정한다. 변형이 없는 차종은 1 그대로다.
	SpeedMultiplier = 1.f;

	/**
	 * 외형 변형은 서버가 골라 복제한다. 추첨과 씌우기는 외형 컴포넌트가 하고, 여기서는
	 * 뽑힌 차체의 차종 이름만 받아 속도를 굴린다.
	 *
	 * 속도는 차체가 정해진 다음에야 뽑을 수 있다. 세단과 밴이 같은 BP 에 들어 있어서
	 * 어느 쪽이 나올지는 추첨 전에는 알 수 없다. 그래서 속도 굴리기가 외형 컴포넌트 호출
	 * 뒤에 온다 - 이 순서가 외형과 주행이 만나는 유일한 지점이다.
	 */
	if (HasAuthority() && AppearanceComponent)
	{
		if (const FCWVehicleMeshVariant* PickedVariant = AppearanceComponent->PickVariant())
		{
			RollSpeedForVariant(PickedVariant->SpeedType, Params.SpeedTable);
		}
	}

	// 이전 주행의 피격 기록이 남아 있으면 안 된다.
	if (HitDetectionComponent)
	{
		HitDetectionComponent->ResetHitRecords();
	}

	UpdateTransformOnLane();
}

void ACWBaseVehicle::RollSpeedForVariant(FName SpeedType, const UDataTable* SpeedTable)
{
	// 이름을 안 적었으면 굴리지 않는다. 아직 분류하지 않은 차체라는 뜻이고,
	// 그때는 차종 BP 의 Speed 로 달리는 것이 맞다.
	if (SpeedType.IsNone() || !SpeedTable)
	{
		return;
	}

	static const FString Context(TEXT("RollSpeedForVariant"));
	const FCWVehicleSpeedRow* Row = SpeedTable->FindRow<FCWVehicleSpeedRow>(SpeedType, Context, /* bWarnIfRowMissing */ false);
	if (!Row)
	{
		// 이름은 적었는데 행이 없다. 표를 아직 안 채웠거나 오타다. 조용히 기본 속도로
		// 달리면 "왜 이 차만 안 느려지지" 를 추적할 단서가 남지 않는다.
		UE_LOG(LogCWTraffic, Warning, TEXT("[%s] 속도 범위표에 '%s' 행이 없다. 차종 기본 속도로 달린다."),
			*GetName(), *SpeedType.ToString());

		return;
	}

	// 범위를 거꾸로 넣었으면 굴릴 것이 없다. Speed 0 은 나눗셈이 안 되고,
	// 어차피 어떤 배수를 곱해도 0 이라 굴릴 의미도 없다.
	if (Row->SpeedMax <= Row->SpeedMin || Speed <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	/**
	 * 뽑은 속도를 Speed 에 직접 넣지 않고 배수로 바꾸는 것은 복제 때문이다.
	 * Speed 는 클래스 기본값이라 복제되지 않아서, 서버가 인스턴스의 Speed 를 고쳐도
	 * 클라이언트는 원래 기본값을 그대로 들고 있어 같은 차가 다른 속도로 달린다.
	 * SpeedMultiplier 는 이미 DOREPLIFETIME 대상이라 이 값만 건너가면 양쪽이 맞는다.
	 */
	SpeedMultiplier = FMath::Max(KINDA_SMALL_NUMBER, FMath::FRandRange(Row->SpeedMin, Row->SpeedMax) / Speed);
}

void ACWBaseVehicle::ApplyCollisionExtent(const FVector& Extent)
{
	if (!CollisionBox)
	{
		return;
	}

	CollisionBox->SetBoxExtent(Extent);

	/**
	 * 박스 바닥을 노면에 맞춘다. 액터 원점이 노면이라 올려야 할 양이 곧 박스의 반높이다.
	 *
	 * 컴포넌트 자기 스케일을 곱한다. 익스텐트는 스케일이 붙기 전 값이라, BP 가 박스
	 * 컴포넌트를 키워 뒀으면 실제 반높이는 그만큼 크다. 곱하지 않으면 키운 만큼 도로 묻힌다.
	 * (액터 스케일은 상대 위치에도 똑같이 곱해지므로 여기서 볼 필요가 없다)
	 *
	 * 회전은 항등으로 되돌린다. 이 박스가 루트이던 시절에는 상대 회전이 저장돼 있어도
	 * 액터 트랜스폼이 매 프레임 덮어써서 드러나지 않았다. 자식으로 내려온 지금은 그 값이
	 * 그대로 살아나 박스만 비스듬히 돈다. 박스는 언제나 차선 방향을 봐야 한다.
	 */
	const float HalfHeight = Extent.Z * CollisionBox->GetRelativeScale3D().Z;
	CollisionBox->SetRelativeLocationAndRotation(FVector(0.f, 0.f, HalfHeight), FRotator::ZeroRotator);
}

void ACWBaseVehicle::OnRep_DistanceAlongLane()
{
	// 이 시점의 DistanceAlongLane 은 이미 서버 값이다. 덮이기 전 로컬 값은 우리가 들고 있다.
	const float ServerDistance = DistanceAlongLane;
	const float ServerError = ServerDistance - LocalDistanceAlongLane;

	// 이어붙일 수 없는 거리는 끌고 가지 않는다. 첫 수신이나 소환 직후가 여기로 온다.
	if (FMath::Abs(ServerError) > ClientSnapThreshold)
	{
		LocalDistanceAlongLane = ServerDistance;
		DistanceCorrectionRemaining = 0.f;
		return;
	}

	/**
	 * 로컬 추정값을 도로 돌려놓고 차이만 빚으로 든다. 화면에 보이는 위치가 이 대입 하나로
	 * 순간이동하는 것이 정지선 앞 떨림의 직접 원인이었다.
	 *
	 * 빚은 더하지 않고 덮어쓴다. 이 차이는 이미 "지금 서버와 얼마나 벌어져 있는가" 라서,
	 * 이전에 남은 몫이 있었다면 그것까지 포함된 값이다. 더하면 같은 오차를 두 번 갚는다.
	 */
	DistanceAlongLane = LocalDistanceAlongLane;
	DistanceCorrectionRemaining = ServerError;
}

void ACWBaseVehicle::ApplyServerDistanceCorrection(float DeltaSeconds)
{
	if (FMath::IsNearlyZero(DistanceCorrectionRemaining))
	{
		DistanceCorrectionRemaining = 0.f;
		return;
	}

	const float MaxStep = ClientCorrectionSpeed * DeltaSeconds;
	const float Applied = FMath::Clamp(DistanceCorrectionRemaining, -MaxStep, MaxStep);

	DistanceAlongLane += Applied;
	DistanceCorrectionRemaining -= Applied;
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
	OutTimeToImpact = FMath::Max(0.f, (RewoundForwardDistance - GetVehicleHalfLength()) / CurrentSpeed);

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

bool ACWBaseVehicle::ReceiveParryAttempt(const FCWParryAttemptContext& ParryAttemptContext)
{
	// 권한 검사는 TryParry 가 한다. 상태를 실제로 바꾸는 곳이 거기라 검사도 거기 한 번이면 된다.
	// 튕겨나갈 방향은 차량에서 커서 지점을 향하는 방향이다.
	// 데드존 전 방향도 받아 둔다. 진단 로그가 "순위가 갈린 것" 과 "데드존이 민 것" 을 가르는 데 쓴다.
	FVector BaseDirection = FVector::ZeroVector;
	const FVector LaunchDirection = ResolveLaunchDirection(ParryAttemptContext, BaseDirection);

	// 진단 로그가 볼 진행 방향은 여기서 잡아 둔다. TryParry 가 차를 돌린 뒤에 읽으면
	// 발사 방향과 같은 값이 되어 "얼마나 꺾였나" 가 항상 0 으로 나온다.
	const FVector IncomingDirection = GetActorForwardVector().GetSafeNormal2D();

	// 랙 보정량은 선별 단계가 쓴 값을 그대로 넘겨야 판정이 갈리지 않는다.
	const bool bAccepted = TryParry(ParryAttemptContext.Parrier, LaunchDirection,
		ParryAttemptContext.ViewRewindSeconds);

	// 판정 뒤에 찍는다. 성패를 같이 남겨야 "좌표가 틀린 줄" 과 "윈도우에서 떨어진 줄" 이 갈린다.
	CWTrafficDiagnostics::LogParryAim(*this, ParryAttemptContext, IncomingDirection, BaseDirection, LaunchDirection, bAccepted);

	return bAccepted;
}

FVector ACWBaseVehicle::ResolveLaunchDirection(const FCWParryAttemptContext& ParryAttemptContext, FVector& OutBaseDirection) const
{
	/**
	 * 1순위: 커서 지점을 **목적지**로 삼는다. 차량이 거기를 향해 날아간다.
	 *
	 * 기준이 플레이어가 아니라 차량이라 "찍은 자리로 간다" 가 그대로 성립한다. 플레이어
	 * 기준 방향으로 날리면 차량은 커서와 평행하게만 가고, 플레이어-차량 거리(윈도우 0.3초
	 * 기준 최대 2m)만큼 옆으로 빗나간 곳에 떨어진다. 그 어긋남을 없애려고 되돌린 것이다.
	 *
	 * 대신 커서 좌표를 못 믿는 구간이 있어서 MinCursorLaunchDistance 로 잘라낸다. 아래 참고.
	 */
	FVector BaseDirection = FVector::ZeroVector;
	if (ParryAttemptContext.bHasCursorTarget && IsCursorUsableAsLaunchTarget(ParryAttemptContext.CursorWorldLocation))
	{
		BaseDirection = (ParryAttemptContext.CursorWorldLocation - GetActorLocation()).GetSafeNormal2D();
	}

	/**
	 * 2순위: 플레이어가 가리킨 **방향**. 커서가 차체에 너무 붙어 목적지로 못 쓸 때 여기로 온다.
	 *
	 * 이 경로가 있는 이유는 커서 좌표 자체가 틀리기 때문이다. 차량 CollisionBox 가 블로킹이
	 * 아니라 커서 트레이스(ACWPlayerCharacter::ResolveCursorWorldLocation)가 차체를 뚫고
	 * 뒤쪽 노면을 찍는다. 밀려나는 방향은 카메라가 정하므로 월드에서 고정이고(2026-09-18
	 * 실측 두 건 모두 52도), 차량의 진행 벡터에 그 고정 오프셋이 더해지는 꼴이라 **오는
	 * 방향이 반대인 두 차가 자기 기준 좌우 반대로 꺾인다.**
	 *
	 * 그대로 두면 커서가 차에 가까울수록 (커서 - 차량)이 짧아져 방향이 폭발적으로 튄다.
	 * 실측 한 건이 커서-차량 86cm 에서 플레이어가 -70도를 가리켰는데 차는 +53도로 날아갔다
	 * (123도 차이). 진행 90도에서 37도만 꺾여 시전자 쪽으로 계속 왔다.
	 *
	 * 그 구간에서는 커서를 목적지가 아니라 방향으로만 읽는다. 같은 오프셋이어도 플레이어-커서
	 * 거리로 나뉘므로 각도 오차가 훨씬 작다. 차를 조준한 사람의 뜻은 "저기로 보내겠다" 가
	 * 아니라 "이 차를 친다" 에 가까우므로, 그때 캐릭터가 보는 쪽으로 나가는 편이 화면과 맞는다.
	 * 이 값은 캐릭터가 바라보는 방향이기도 하다(ACWPlayerCharacter::UpdateCursorFacing).
	 */
	if (BaseDirection.IsNearlyZero())
	{
		BaseDirection = ParryAttemptContext.AttemptDirection.GetSafeNormal2D();
	}

	// 3순위: 방향까지 비어서 왔다. 온 길로 되돌려 보낸다. 시전자 쪽이 아니라 데드존에 걸릴 일도 없다.
	if (BaseDirection.IsNearlyZero())
	{
		BaseDirection = -GetActorForwardVector().GetSafeNormal2D();
	}

	// 어느 순위가 뽑혔는지는 여기서만 알 수 있다. 진단 로그가 읽을 수 있게 내보낸다.
	OutBaseDirection = BaseDirection;

	// 어느 순위에서 나왔든 데드존은 한 번만 거친다. 폴백 경로라고 자기 쪽으로 쏠 수 있게
	// 두면 판정 결과가 커서 트레이스의 성패에 따라 갈린다.
	return ApplySelfLaunchDeadZone(BaseDirection, ParryAttemptContext);
}

bool ACWBaseVehicle::IsCursorUsableAsLaunchTarget(const FVector& CursorWorldLocation) const
{
	if (MinCursorLaunchDistance <= 0.f)
	{
		return true;
	}

	/**
	 * 차체 표면 기준으로 잰다. 원점 기준이면 차 길이가 그대로 거리에 섞여서, 같은 자리에
	 * 찍은 커서가 버스에서는 통과하고 승용차에서는 걸린다. 재는 것은 "커서가 차 위에
	 * 올라가 있나" 이므로 차체 크기는 빠져야 한다. (GetDistanceToPoint)
	 *
	 * 수평으로만 잰다. 커서는 노면에 찍히고 차량 원점은 차체 중심이라 Z 차가 항상 남는데,
	 * 그 값이 섞이면 차가 클수록 판정이 후해진다.
	 */
	const FVector FlattenedCursor(CursorWorldLocation.X, CursorWorldLocation.Y, GetActorLocation().Z);

	return GetDistanceToPoint(FlattenedCursor) >= MinCursorLaunchDistance;
}

FVector ACWBaseVehicle::ApplySelfLaunchDeadZone(const FVector& LaunchDirection, const FCWParryAttemptContext& ParryAttemptContext) const
{
	if (SelfLaunchDeadZoneAngle <= 0.f)
	{
		return LaunchDirection;
	}

	// 부르는 쪽(ResolveLaunchDirection)이 빈 방향을 넘기지 않는다. 방어로만 남긴다.
	const FVector Launch = LaunchDirection.GetSafeNormal2D();
	if (Launch.IsNearlyZero())
	{
		return LaunchDirection;
	}

	/**
	 * 시도 시점의 위치로 잰다. 지금 위치가 아니다.
	 *
	 * 판정은 이미 이 좌표를 기준으로 끝났다(PassesAttemptFilter 의 각도·거리). 여기서만
	 * 현재 위치를 쓰면, 시도한 뒤 움직인 만큼 금지 구역이 판정과 다른 곳에 생긴다.
	 */
	const FVector ToParrier = (ParryAttemptContext.AttemptLocation - GetActorLocation()).GetSafeNormal2D();
	if (ToParrier.IsNearlyZero())
	{
		// 시전자가 차량과 같은 지점에 있다. 밀어낼 방향을 정할 근거가 없다.
		return Launch;
	}

	if (!CWTrafficMath::IsInsideCone(Launch, ToParrier, SelfLaunchDeadZoneAngle))
	{
		return Launch;
	}

	const float HalfAngleDegrees = FMath::Clamp(SelfLaunchDeadZoneAngle, 0.f, 360.f) * 0.5f;
	const float Dot = FVector::DotProduct(Launch, ToParrier);

	/**
	 * 데드존 안이다. 가리킨 쪽에서 가장 가까운 경계로 밀어낸다.
	 *
	 * ToParrier 성분을 빼면 그 축에 수직인 성분만 남고, 그게 원래 가리킨 좌우 중 어느
	 * 쪽인지를 그대로 들고 있다. 경계 방향은 그 둘을 반각의 cos·sin 으로 다시 섞은 것이다.
	 */
	FVector Perpendicular = (Launch - ToParrier * Dot).GetSafeNormal2D();
	if (Perpendicular.IsNearlyZero())
	{
		/**
		 * 정확히 시전자 정면을 가리켰다. 커서를 차량과 자기 사이에 둔 경우가 여기로 온다.
		 *
		 * 좌우 어느 쪽이 가까운지가 정해지지 않으므로 시전자 기준 한쪽으로 몰아준다.
		 * 여기서 Launch 를 그대로 돌려주면 막으려던 그 입력이 통과한다.
		 *
		 * 밀려나는 좌우가 커서의 미세한 움직임으로 뒤집히는 구간이 이 근처에 남는다.
		 * 데드존 방식의 성질이라 없앨 수 없고, 실제로 거슬리면 방향을 버리고 온 길로
		 * 되돌려 보내는(-GetActorForwardVector) 쪽으로 바꿔야 한다.
		 */
		Perpendicular = FVector::CrossProduct(FVector::UpVector, ToParrier).GetSafeNormal2D();
	}

	const float HalfAngleRadians = FMath::DegreesToRadians(HalfAngleDegrees);
	const FVector ClampedDirection =
		(ToParrier * FMath::Cos(HalfAngleRadians) + Perpendicular * FMath::Sin(HalfAngleRadians)).GetSafeNormal2D();

	if (ClampedDirection.IsNearlyZero())
	{
		return Launch;
	}

	// 확정된 패링 1건에 한 줄이라 로그가 쌓이지 않는다. 여기서는 acos 를 써도 된다.
	UE_LOG(LogCWTraffic, Verbose, TEXT("[Parry]   %s: 시전자 쪽이라 발사 방향을 %.0f도에서 %.0f도로 밀어냅니다. (데드존 반각 %.0f도)"),
		*GetName(),
		CWTrafficMath::AngleBetweenDegrees(Launch, ToParrier),
		CWTrafficMath::AngleBetweenDegrees(ClampedDirection, ToParrier),
		HalfAngleDegrees);

	return ClampedDirection;
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
	UE_LOG(LogCWTraffic, Verbose, TEXT("[Parry]   %s: 발사 직후라 랙 보정을 %.0fms -> %.0fms 로 깎습니다."),
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

	// 패링당한 차량은 멈추는 게 아니라 커서 지점 쪽으로 튕겨나간다.
	// 빈 방향은 여기까지 오지 않는다. 폴백은 전부 ResolveLaunchDirection 이 끝냈다.
	const FVector LaunchForward = LaunchDirection.GetSafeNormal2D();
	if (!ensureMsgf(!LaunchForward.IsNearlyZero(), TEXT("[Parry] %s: 발사 방향이 비어 있다. ResolveLaunchDirection 을 거치지 않은 호출이다."), *GetName()))
	{
		return false;
	}

	SetActorRotation(LaunchForward.Rotation());

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

	// 쳐낸 사람이 지금 차와 겹쳐 있으면 이번 겹침 동안만 피격에서 뺀다. 충돌 직전에 쳐낸
	// 경우라 차와 몸이 이미 닿아 있고, 그대로 두면 패링에 성공하자마자 맞는다.
	// 겹침이 풀렸다 다시 닿으면 똑같이 맞는다. (피격 컴포넌트의 IgnoreWhileOverlapping 주석)
	if (HitDetectionComponent)
	{
		HitDetectionComponent->IgnoreWhileOverlapping(Parrier);
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
	// 번호만 도착하고 bParried 가 아직 false 인 순서는 없어야 하지만, 실제로 튕겨나간 상태만 반영한다.
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

void ACWBaseVehicle::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	/**
	 * BP 가 들고 있는 박스 크기에 맞춰 높이를 잡는다.
	 *
	 * 여기서 하는 이유는 편집 화면에 보이는 것이 곧 런타임 모습이어야 하기 때문이다.
	 * 플레이 시작 시점에만 맞추면 BP 뷰포트에는 정렬 전 위치가 그려져서, 박스 크기를
	 * 바꿀 때마다 높이를 손으로 다시 계산해야 하고 그 계산이 틀려도 플레이하기 전에는
	 * 드러나지 않는다. 박스 피벗이 정중앙이라 반높이를 암산하는 일이 매번 생긴다.
	 *
	 * 정렬 지점을 한 곳으로 모으는 목적도 있다. OnConstruction 은 레벨 로드와
	 * SpawnActor 양쪽에서, 그리고 BP 에 저장된 값이 적용된 뒤에 지나간다. 그래서
	 * "차종 BP 가 크기만 직접 편집해 ApplyMeshVariant 를 타지 않는" 경우도 여기서 걸린다.
	 *
	 * 크기는 BP 값을 그대로 쓰고 높이만 따라간다. 바닥이 노면에 붙는다는 것은 차종이
	 * 고를 수 있는 값이 아니라 트래픽의 불변식이다.
	 */
	if (CollisionBox)
	{
		ApplyCollisionExtent(CollisionBox->GetUnscaledBoxExtent());
	}
}

void ACWBaseVehicle::BeginPlay()
{
	Super::BeginPlay();

	/**
	 * 차량은 바닥 데칼을 받지 않는다.
	 *
	 * 안 끄면 싱크홀에 못 맞춘 아스팔트 끈끈이 데칼 위를 지날 때 그 무늬가 차체에 씌워진다.
	 * BP 에서 부속 메시를 더 달아도 액터 단위로 훑으므로 함께 꺼진다.
	 */
	UCWDecalStatics::DisableDecalReceiveOnActor(this);

	// 콜리전 박스의 오버랩은 피격 컴포넌트가 자기 BeginPlay 에서 바인딩한다.

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
	// 차량 풀링은 팀 결정으로 보류다(스폰 빈도가 낮아 이득이 없다). 소멸은 Destroy 로 끝낸다.
	Destroy();
}

FCWVehicleStepResolution ACWBaseVehicle::ResolveStepSpeed() const
{
	// 기본값이 곧 "정지선 없음" 이다. 정지선을 보지 못하고 빠져나가는 경로가 여럿인데,
	// 구조체 기본값이 그 경우를 전부 받아 주므로 경로마다 되돌릴 것이 없다.
	FCWVehicleStepResolution Result;

	const float DesiredSpeed = GetEffectiveSpeed();
	Result.Speed = DesiredSpeed;

	const UCWTrafficSubsystem* TrafficSubsystem = UCWTrafficSubsystem::Get(this);
	if (!TrafficSubsystem)
	{
		return Result;
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
	if (const ACWBaseVehicle* VehicleAhead = TrafficSubsystem->FindVehicleAhead(this))
	{
		const float BumperToBumper = VehicleAhead->GetDistanceAlongLane() - DistanceAlongLane
			- (GetVehicleLength() * 0.5f) - (VehicleAhead->GetVehicleLength() * 0.5f);

		Result.Speed = FMath::Min(Result.Speed, ResolveApproachSpeed(BumperToBumper - FollowingGap, FollowingGap, DesiredSpeed));
	}

	float StopLineDistance = 0.f;
	if (TrafficSubsystem->FindActiveStopLine(CurrentLane, DistanceAlongLane, StopLineDistance))
	{
		/**
		 * 정지선에서 차체 절반과 간격을 물러난 자리가 이 차가 서야 하는 지점이다.
		 * 앞범퍼가 정지선에서 StopLineGap 만큼 떨어진 자리에 놓이는 진행 거리를 뜻한다.
		 *
		 * 남은 거리가 아니라 지점으로 낸다. 남은 거리는 차가 굴러가는 동안 계속 줄어들어서
		 * 두 기계의 값을 비교해도 "지금 서로 다른 위치에 있다" 이상을 알 수 없다.
		 * 지점은 진행 거리와 무관하게 같은 값이라, 양쪽이 같은 자리를 목표로 삼고 있는지와
		 * 실제로 선 자리가 거기서 얼마나 벗어났는지를 따로 물을 수 있다.
		 */
		const float StopTarget = StopLineDistance - (GetVehicleLength() * 0.5f) - StopLineGap;

		const float RemainingToStop = StopTarget - DistanceAlongLane;
		float StopLineSpeed = ResolveApproachSpeed(RemainingToStop, StopLineBrakeDistance, DesiredSpeed);

		/**
		 * 마지막 구간에는 바닥을 깔아 고정 속도로 굴린다.
		 *
		 * 위 감속은 남은 거리에 비례하는 지수형이라 유한 시간에 0 이 되지 않는다.
		 * 바닥이 없으면 목표 지점에 도착하는 프레임이 영영 오지 않아서 Tick 의 정지 확정
		 * 분기가 안 걸리고, 차는 계속 미세하게 기어간다.
		 *
		 * 제동 곡선을 바꾸는 것이 아니라 아래쪽만 자르는 것이라, 감속이 시작되는 거리와
		 * 감속감은 그대로다. 주행 속도가 느린 차종이 이 바닥 때문에 오히려 빨라지지 않도록
		 * 자기 속도로 한 번 더 막는다.
		 */
		if (RemainingToStop > 0.f)
		{
			StopLineSpeed = FMath::Max(StopLineSpeed, FMath::Min(StopFinalCrawlSpeed, DesiredSpeed));
		}

		Result.StopTargetDistance = StopTarget;

		// 정지선이 앞차보다 더 느리게 만들었을 때만 정지선 탓이다. 둘을 뭉치면 로그가
		// "정지선 앞에 섰다" 라고 말하는데 실제로는 앞차에 막힌 상황이 섞여 들어온다.
		Result.bStopLineLimited = StopLineSpeed <= Result.Speed;

		Result.Speed = FMath::Min(Result.Speed, StopLineSpeed);
	}

	return Result;
}

void ACWBaseVehicle::UpdateStopState(bool bNowStopped)
{
	if (bNowStopped == bStoppedAtStopLine)
	{
		return;
	}

	bStoppedAtStopLine = bNowStopped;

	// 로그보다 먼저 알린다. 진단 스위치를 껐다고 보정이 안 도는 일이 없어야 한다.
	OnStopStateChanged.Broadcast(this, bStoppedAtStopLine, StopTargetDistance);

	if (!CWTrafficDiagnostics::IsSyncLoggingEnabled())
	{
		return;
	}

	if (bNowStopped)
	{
		// 목표 지점과 실제로 선 자리를 같이 남긴다. 양쪽 로그에서 목표가 같은데 선 자리가
		// 다르면 도착 지연 탓이고, 목표부터 다르면 신호색이 아직 안 갔거나 차선을 다르게 보고 있다.
		UE_LOG(LogCWTraffic, Log, TEXT("[TrafficSync][%s] 차량 %s 정지선 앞 정지 - 차선 %s / 진행거리 %.1fcm / 목표지점 %.1fcm / 오차 %.1fcm"),
			CWTrafficDiagnostics::GetNetRoleTag(this),
			*CWTrafficDiagnostics::GetNetIdTag(this),
			CurrentLane ? *CurrentLane->GetName() : TEXT("(없음)"),
			DistanceAlongLane,
			StopTargetDistance,
			DistanceAlongLane - StopTargetDistance);

		return;
	}

	UE_LOG(LogCWTraffic, Log, TEXT("[TrafficSync][%s] 차량 %s 정지선에서 출발 - 진행거리 %.1fcm"),
		CWTrafficDiagnostics::GetNetRoleTag(this),
		*CWTrafficDiagnostics::GetNetIdTag(this),
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
