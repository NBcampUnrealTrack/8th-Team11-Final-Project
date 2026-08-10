#include "Characters/CWPlayerCharacter.h"

#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Characters/Components/CWVehicleHitReactionComponent.h"
#include "Painting/CWWorkerComponent.h"
#include "Painting/Tools/CWToolHolderComponent.h"
#include "Traffic/Components/CWParryComponent.h"
#include "UObject/ConstructorHelpers.h"

ACWPlayerCharacter::ACWPlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	// 쿼터뷰라 마우스로 시야를 돌리지 않는다. 캐릭터는 이동 방향으로만 회전한다.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.f, 720.f, 0.f);
	// 기획서 3.3 의 "차량 속도 = 기본 이동속도의 1.5배" 기준값이 되므로 명시해 둔다.
	Movement->MaxWalkSpeed = 500.f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 1200.f;
	CameraBoom->SetRelativeRotation(FRotator(-55.f, 0.f, 0.f));
	// 캐릭터가 이동 방향으로 돌아도 카메라 각도는 고정되어야 한다.
	// 이걸 켜지 않으면 방향을 바꿀 때마다 화면이 같이 돌아서 쿼터뷰가 아니게 된다.
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->bUsePawnControlRotation = false;
	// 도로 위 구조물에 카메라가 끌려 들어오지 않게 막는다.
	CameraBoom->bDoCollisionTest = false;

	TopDownCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
	TopDownCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TopDownCamera->bUsePawnControlRotation = false;

	WorkerComponent = CreateDefaultSubobject<UCWWorkerComponent>(TEXT("WorkerComponent"));
	ToolHolderComponent = CreateDefaultSubobject<UCWToolHolderComponent>(TEXT("ToolHolderComponent"));
	ParryComponent = CreateDefaultSubobject<UCWParryComponent>(TEXT("ParryComponent"));
	HitReactionComponent = CreateDefaultSubobject<UCWVehicleHitReactionComponent>(TEXT("HitReactionComponent"));

	// 프로토타입 플레이스홀더. BP 를 만들지 않아도 바로 보이도록 여기서 물려 둔다.
	// 엔진 템플릿 마네킹이므로 우리 애셋과 섞이지 않게 ThirdParty 아래에 둔다.
	// 팀 캐릭터 애셋이 정해지면 BP 자식에서 갈아 끼우면 된다.
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> PlaceholderMesh(
		TEXT("/Game/ThirdParty/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	static ConstructorHelpers::FClassFinder<UAnimInstance> PlaceholderAnim(
		TEXT("/Game/ThirdParty/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));

	if (PlaceholderMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMeshAsset(PlaceholderMesh.Object);
		// 캡슐 중심 기준으로 발이 바닥에 닿게 내리고, 마네킹 정면을 캐릭터 정면(+X)에 맞춘다.
		GetMesh()->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -90.f), FRotator(0.f, -90.f, 0.f));
	}

	if (PlaceholderAnim.Succeeded())
	{
		GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		GetMesh()->AnimClass = PlaceholderAnim.Class;
	}
}

void ACWPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 잠금이 걸리고 풀릴 때 작업/도구를 정리해야 한다. 서버와 클라이언트 양쪽에서 들어온다.
	HitReactionComponent->OnHitLockChanged.AddDynamic(this, &ACWPlayerCharacter::HandleHitLockChanged);

	// 패링 성공 연출. 서버가 멀티캐스트로 뿌리므로 여기서 권한을 따질 필요가 없다.
	ParryComponent->OnParrySucceeded.AddDynamic(this, &ACWPlayerCharacter::HandleParrySucceeded);

	// 래그돌을 풀 때 되돌릴 기준값을 미리 잡아 둔다.
	DefaultMeshRelativeTransform = GetMesh()->GetRelativeTransform();
	DefaultMeshCollisionProfile = GetMesh()->GetCollisionProfileName();
}

void ACWPlayerCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	// 기획서 6.2 - 다운 2초는 착지한 뒤부터 흐른다. 컴포넌트가 서버 권한을 다시 확인한다.
	HitReactionComponent->NotifyLanded();
}

void ACWPlayerCharacter::ReceiveVehicleHit_Implementation(const FCWVehicleHitContext& HitContext)
{
	if (!HasAuthority())
	{
		return;
	}

	// 이미 쓰러져 있으면 컴포넌트가 알아서 무시한다. 넉백을 두 번 먹이지 않도록 여기서도 막는다.
	if (HitReactionComponent->IsHitLocked())
	{
		return;
	}

	HitReactionComponent->HandleVehicleHit(HitContext);

	// 차가 밀고 나가던 방향으로 날린다. 세기는 차량 속도에 비례시킨다.
	const FVector LaunchDirection = HitContext.HitDirection.GetSafeNormal2D();
	const FVector LaunchVelocity = LaunchDirection * HitContext.VehicleSpeed * KnockbackSpeedScale
		+ FVector::UpVector * KnockbackUpSpeed;

	Multicast_StartRagdoll(LaunchVelocity);
}

void ACWPlayerCharacter::Multicast_StartRagdoll_Implementation(FVector_NetQuantize LaunchVelocity)
{
	StartRagdoll(LaunchVelocity);

	if (!HasAuthority())
	{
		return;
	}

	// 멈춤 판정은 서버만 한다. 머신마다 물리 결과가 조금씩 달라 기준이 갈리면 안 된다.
	GetWorldTimerManager().SetTimer(
		RagdollRestTimerHandle,
		this,
		&ACWPlayerCharacter::CheckRagdollRest,
		RagdollRestCheckInterval,
		true,
		RagdollRestCheckDelay
	);
}

void ACWPlayerCharacter::Multicast_StopRagdoll_Implementation(FVector_NetQuantize RecoverLocation)
{
	StopRagdoll(RecoverLocation);
}

void ACWPlayerCharacter::StartRagdoll(const FVector& LaunchVelocity)
{
	if (bRagdollActive)
	{
		return;
	}

	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (MeshComponent == nullptr || MeshComponent->GetPhysicsAsset() == nullptr)
	{
		// 물리 애셋이 없으면 래그돌이 성립하지 않는다. 조용히 넘어가면 원인을 찾기 어렵다.
		UE_LOG(LogTemp, Warning, TEXT("[%s] 물리 애셋이 없어 래그돌을 켤 수 없습니다. 스켈레탈 메시 설정을 확인하세요."), *GetName());

		/**
		 * 래그돌 없이도 잠금은 정상적인 길이로 끝나야 한다.
		 *
		 * 여기서 그냥 돌아가면 몸이 뜨지 않아 ACharacter::Landed 가 오지 않고,
		 * bRagdollActive 가 false 라 CheckRagdollRest 도 착지를 통보하지 않는다.
		 * 결국 컴포넌트의 MaxLandingWait(5초) 안전장치가 유일한 탈출구가 되어,
		 * 1초짜리 잠금이 6초가 된다. 서 있는 채로 6초를 못 움직이면 버그로 보인다.
		 *
		 * 스켈레톤을 갈아끼울 때 물리 애셋을 빠뜨리는 건 실제로 자주 있는 일이라,
		 * 연출만 빠지고 게임은 계속 굴러가는 쪽을 택한다.
		 */
		if (HasAuthority())
		{
			HitReactionComponent->NotifyLanded();
		}

		return;
	}

	bRagdollActive = true;

	// 무브먼트가 계속 돌면 캡슐이 캐릭터를 끌고 다니면서 물리와 서로 밀어낸다.
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->StopMovementImmediately();
	Movement->DisableMovement();
	Movement->SetComponentTickEnabled(false);

	// 캡슐이 살아 있으면 쓰러진 몸이 자기 캡슐에 걸려 튄다.
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	/**
	 * 카메라를 쓰러지는 몸통에 붙인다.
	 *
	 * 캡슐을 매 프레임 몸통 자리로 옮기는 방법은 쓰면 안 된다. 메시가 캡슐의 자식이라
	 * 부모를 옮기면 자식 물리 바디까지 끌려가고, 그 결과가 다음 프레임 입력이 되면서
	 * 몸이 뒤로 튀거나 바닥을 뚫는다. 캡슐은 그대로 두고 카메라만 따라가게 한다.
	 *
	 * 붐은 회전이 절대값으로 고정돼 있어 몸이 굴러도 화면 각도는 그대로다.
	 */
	CameraBoom->AttachToComponent(
		MeshComponent,
		FAttachmentTransformRules::KeepWorldTransform,
		RagdollPivotBone
	);

	MeshComponent->SetCollisionProfileName(TEXT("Ragdoll"));
	MeshComponent->SetSimulatePhysics(true);

	// bVelChange 를 켜서 질량과 무관하게 넉백 속도가 그대로 실리게 한다.
	MeshComponent->AddImpulse(LaunchVelocity, NAME_None, true);

	// 물리 결과는 머신마다 다르다. 서버 캡슐 위치를 밀어 넣으면 클라 래그돌과 싸운다.
	if (HasAuthority())
	{
		SetReplicateMovement(false);
	}
}

void ACWPlayerCharacter::StopRagdoll(const FVector& RecoverLocation)
{
	if (!bRagdollActive)
	{
		return;
	}

	bRagdollActive = false;

	USkeletalMeshComponent* MeshComponent = GetMesh();

	/**
	 * 물리를 확실히 내린다.
	 *
	 * SetSimulatePhysics(false) 만으로는 바디가 깨어 있는 채로 남을 수 있다. 그 상태에서
	 * 메시를 옮기면 "완전 시뮬레이션으로 구동되는 스켈레탈 메시를 이동하려 하였습니다"
	 * 경고가 뜨고 이동이 반영되지 않는다.
	 */
	MeshComponent->SetSimulatePhysics(false);
	MeshComponent->SetAllBodiesSimulatePhysics(false);
	MeshComponent->PutAllRigidBodiesToSleep();
	MeshComponent->bBlendPhysics = false;
	MeshComponent->SetCollisionProfileName(DefaultMeshCollisionProfile);

	// 캡슐을 먼저 세우고 메시를 되돌린다. 순서가 바뀌면 메시가 한 프레임 원점에 찍힌다.
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetActorLocation(RecoverLocation, false, nullptr, ETeleportType::TeleportPhysics);

	MeshComponent->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::KeepRelativeTransform);

	/**
	 * 래그돌 동안 캡슐은 제자리에 있고 메시만 월드에서 굴러다닌다. 그래서 메시의 상대
	 * 트랜스폼에는 물리가 만든 큰 오프셋이 남아 있고, 여기서 원래 값으로 되돌려야 한다.
	 *
	 * Teleport 플래그를 주지 않으면 이 이동이 반영되지 않는다. 그러면 캡슐만 기상 위치로
	 * 가고 메시는 엉뚱한 오프셋을 유지해서, 화면에서는 캐릭터가 사라진 것처럼 보인다.
	 */
	MeshComponent->SetRelativeTransform(DefaultMeshRelativeTransform, false, nullptr, ETeleportType::TeleportPhysics);

	// 카메라를 캡슐 위로 돌려놓는다. 붐의 원점은 캐릭터 위치여야 한다.
	CameraBoom->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	CameraBoom->SetRelativeLocation(FVector::ZeroVector);

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->SetComponentTickEnabled(true);
	Movement->SetMovementMode(MOVE_Walking);

	if (HasAuthority())
	{
		SetReplicateMovement(true);
	}
}

void ACWPlayerCharacter::CheckRagdollRest()
{
	if (!HasAuthority() || !bRagdollActive)
	{
		GetWorldTimerManager().ClearTimer(RagdollRestTimerHandle);
		return;
	}

	const float PivotSpeed = GetMesh()->GetPhysicsLinearVelocity(RagdollPivotBone).Size();
	if (PivotSpeed > RagdollRestSpeed)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(RagdollRestTimerHandle);

	// 여기서부터 잠금 시간이 흐른다. 다 지나면 HandleHitLockChanged 로 돌아온다.
	HitReactionComponent->NotifyLanded();
}

FVector ACWPlayerCharacter::ResolveRecoverLocation() const
{
	const FVector PivotLocation = GetMesh()->GetSocketLocation(RagdollPivotBone);
	const float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	// 쓰러진 자리 아래 바닥을 찾아 캡슐을 올려놓는다. 못 찾으면 몸통 높이를 그대로 쓴다.
	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	const bool bHitGround = GetWorld()->LineTraceSingleByChannel(
		Hit,
		PivotLocation,
		PivotLocation - FVector(0.f, 0.f, 500.f),
		ECC_Visibility,
		QueryParams
	);

	const FVector GroundLocation = bHitGround ? Hit.Location : PivotLocation;

	return GroundLocation + FVector(0.f, 0.f, CapsuleHalfHeight);
}

void ACWPlayerCharacter::HandleHitLockChanged(UCWVehicleHitReactionComponent* /*HitReactionComp*/, bool bHitLocked)
{
	// 연출은 각 머신이 알아서 낸다. 아래 권한 검사보다 앞에 둬야 클라이언트에서도 들린다.
	if (bHitLocked)
	{
		PlayVehicleHitSound();
	}

	// 작업 중단 상태는 복제되므로 서버에서만 바꾼다. 도구 드롭도 서버 전용이다.
	if (!HasAuthority())
	{
		return;
	}

	WorkerComponent->SetWorkBlocked(bHitLocked);

	// 기획서 6.2 - 치이면 들고 있던 도구를 떨어뜨린다. 일어설 때 다시 주워야 한다.
	if (bHitLocked)
	{
		ToolHolderComponent->DropHeldTool();
		return;
	}

	// 잠금이 풀리면 일어난다. 자리는 서버가 정해 모든 머신에 같은 값을 준다.
	if (bRagdollActive)
	{
		Multicast_StopRagdoll(ResolveRecoverLocation());
	}
}

void ACWPlayerCharacter::HandleParrySucceeded(const FCWParrySyncData& ParrySyncData)
{
	if (ParrySuccessSound == nullptr)
	{
		return;
	}

	/**
	 * 내가 성공시킨 패링은 거리로 깎지 않는다.
	 *
	 * 패링에는 거리 제한이 없어서(UCWParryComponent::ParryRadius 기본 0) 화면 끝의 차도
	 * 튕겨낼 수 있다. 그걸 감쇠에 태우면 정작 내가 누른 키의 성공 여부가 들리지 않는다.
	 */
	if (IsLocallyControlled())
	{
		UGameplayStatics::PlaySound2D(this, ParrySuccessSound);
		return;
	}

	// 남의 패링은 어디서 일어났는지가 정보다. 차량 위치에서 거리감 있게 낸다.
	UGameplayStatics::PlaySoundAtLocation(
		this,
		ParrySuccessSound,
		ParrySyncData.ParryLocation,
		FRotator::ZeroRotator,
		1.f,
		1.f,
		0.f,
		ResolveSoundAttenuation()
	);
}

void ACWPlayerCharacter::PlayVehicleHitSound()
{
	if (VehicleHitSound == nullptr)
	{
		return;
	}

	/**
	 * 본인 피격도 감쇠에 태운다. 따로 2D 로 뺄 이유가 없다.
	 *
	 * 내가 맞은 지점은 리스너(캐릭터 위치)와 거리가 0 이라 AttenuationInnerRadius 안쪽이고,
	 * NonSpatializedRadiusStart 안에도 들어가 어차피 풀볼륨 2D 로 재생된다.
	 * 남이 맞는 소리만 거리에 따라 자연스럽게 멀어진다.
	 */
	FVector HitLocation = HitReactionComponent->GetLastHitContext().HitLocation;

	// 클라이언트에서는 잠금 플래그가 피격 정보보다 먼저 도착할 수 있다.
	// 그때는 맞은 사람 위치로 대신한다. 어차피 충돌 지점 바로 옆이다.
	if (HitLocation.IsNearlyZero())
	{
		HitLocation = GetActorLocation();
	}

	UGameplayStatics::PlaySoundAtLocation(
		this,
		VehicleHitSound,
		HitLocation,
		FRotator::ZeroRotator,
		1.f,
		1.f,
		0.f,
		ResolveSoundAttenuation()
	);
}

USoundAttenuation* ACWPlayerCharacter::ResolveSoundAttenuation()
{
	// 애셋이 꽂혀 있으면 그쪽이 우선이다.
	if (SoundAttenuation != nullptr)
	{
		return SoundAttenuation;
	}

	if (RuntimeSoundAttenuation != nullptr)
	{
		return RuntimeSoundAttenuation;
	}

	RuntimeSoundAttenuation = NewObject<USoundAttenuation>(this);

	FSoundAttenuationSettings& Settings = RuntimeSoundAttenuation->Attenuation;
	Settings.bAttenuate = true;
	Settings.bSpatialize = true;

	// Linear 는 거리에 따라 뚝 끊기듯 줄어 인위적으로 들린다.
	Settings.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	Settings.AttenuationShape = EAttenuationShape::Sphere;
	Settings.dBAttenuationAtMax = -60.f;

	// Sphere 는 X 만 반경으로 쓴다. 이 안쪽은 볼륨이 깎이지 않는다.
	Settings.AttenuationShapeExtents = FVector(AttenuationInnerRadius, 0.f, 0.f);
	Settings.FalloffDistance = AttenuationFalloffDistance;

	// 쿼터뷰라 발밑에서 나는 소리가 좌우로 튀면 어지럽다. 가까운 소리는 2D 로 눕힌다.
	Settings.NonSpatializedRadiusStart = 400.f;
	Settings.NonSpatializedRadiusEnd = 200.f;

	// 도로 위 구조물에 계속 걸리는 데다 비용도 크다. 쿼터뷰에서 가려짐은 의미가 없다.
	Settings.bEnableOcclusion = false;

	return RuntimeSoundAttenuation;
}

bool ACWPlayerCharacter::IsHitLocked() const
{
	return HitReactionComponent->IsHitLocked();
}

void ACWPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	BuildBuiltInInput();
	ApplyMappingContext();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (EnhancedInput == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] InputComponent 가 UEnhancedInputComponent 가 아닙니다. Config/DefaultInput.ini 의 DefaultInputComponentClass 를 확인하세요."),
			*GetName());
		return;
	}

	// BindAction 은 액션이 비어 있어도 경고 없이 넘어가고, 그 입력만 조용히 죽는다.
	// 원인을 찾기 어려운 실패라 바인딩 전에 먼저 찍는다.
	if (MoveAction == nullptr || InteractAction == nullptr || WorkAction == nullptr || ParryAction == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] 비어 있는 InputAction 이 있습니다. Move=%d Interact=%d Work=%d Parry=%d (0 인 입력은 동작하지 않습니다)"),
			*GetName(), MoveAction != nullptr, InteractAction != nullptr, WorkAction != nullptr, ParryAction != nullptr);
	}

	EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ACWPlayerCharacter::Move);
	EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &ACWPlayerCharacter::Interact);

	// 누르고 있는 동안 게이지가 오르는 방식이라 시작과 끝을 따로 잡는다.
	// Canceled 를 빼먹으면 키를 떼도 작업이 계속되는 버그가 생긴다.
	EnhancedInput->BindAction(WorkAction, ETriggerEvent::Started, this, &ACWPlayerCharacter::StartWork);
	EnhancedInput->BindAction(WorkAction, ETriggerEvent::Completed, this, &ACWPlayerCharacter::StopWork);
	EnhancedInput->BindAction(WorkAction, ETriggerEvent::Canceled, this, &ACWPlayerCharacter::StopWork);

	// 패링은 누르는 순간 한 번만 판정한다. 연타 제한은 패링 컴포넌트의 쿨타임이 맡는다.
	EnhancedInput->BindAction(ParryAction, ETriggerEvent::Started, this, &ACWPlayerCharacter::Parry);

	// 미니게임에서 빠져나오는 키. 이 바인딩이 빠지면 Q 를 눌러도 아무 일도 안 일어난다.
	EnhancedInput->BindAction(CancelWorkAction, ETriggerEvent::Started, this, &ACWPlayerCharacter::CancelWork);
}

void ACWPlayerCharacter::BuildBuiltInInput()
{
	// 예전에는 DefaultMappingContext 가 지정돼 있으면 이 함수를 통째로 건너뛰었다.
	// 그랬더니 BP 가 IMC 만 물고 액션 매핑을 일부 빼먹었을 때 그 입력이 조용히 죽었다.
	// (실제로 IMC 에 IA_Interact / IA_Work 가 빠져 있어 E 와 Space 가 먹지 않았다)
	//
	// 그래서 "전부 아니면 전무" 대신 빠진 것만 채우는 방식으로 바꿨다.
	// 애셋이 다루는 액션은 그대로 두고, 빠진 액션만 C++ 이 기본 키로 보완한다.
	EnsureInputAction(MoveAction, TEXT("BuiltIn_Move"), EInputActionValueType::Axis2D);
	EnsureInputAction(InteractAction, TEXT("BuiltIn_Interact"), EInputActionValueType::Boolean);
	EnsureInputAction(WorkAction, TEXT("BuiltIn_Work"), EInputActionValueType::Boolean);
	EnsureInputAction(ParryAction, TEXT("BuiltIn_Parry"), EInputActionValueType::Boolean);
	EnsureInputAction(CancelWorkAction, TEXT("BuiltIn_CancelWork"), EInputActionValueType::Boolean);

	BuildSupplementContext();
}

void ACWPlayerCharacter::EnsureInputAction(TObjectPtr<UInputAction>& Action, const TCHAR* ActionName, EInputActionValueType ValueType)
{
	if (Action != nullptr)
	{
		return;
	}

	Action = NewObject<UInputAction>(this, ActionName);
	Action->ValueType = ValueType;
}

void ACWPlayerCharacter::BuildSupplementContext()
{
	// 지정된 IMC 가 이미 다루고 있는 액션을 모아둔다.
	// 지정된 컨텍스트는 애셋이라 런타임에 매핑을 추가하면 안 되므로, 보완분은 별도 컨텍스트에 담는다.
	TSet<const UInputAction*> AlreadyMapped;
	if (DefaultMappingContext != nullptr)
	{
		for (const FEnhancedActionKeyMapping& Mapping : DefaultMappingContext->GetMappings())
		{
			AlreadyMapped.Add(Mapping.Action.Get());
		}
	}

	SupplementContext = NewObject<UInputMappingContext>(this, TEXT("BuiltIn_SupplementContext"));

	if (!AlreadyMapped.Contains(MoveAction))
	{
		// Axis2D 에서 X = 좌우, Y = 앞뒤.
		// 키 하나는 값 1.0 을 X 축에만 넣으므로, 앞뒤 키는 Swizzle 로 값을 Y 축으로 옮겨야 한다.
		AddMoveKeyMapping(SupplementContext, EKeys::W, true, false);
		AddMoveKeyMapping(SupplementContext, EKeys::S, true, true);
		AddMoveKeyMapping(SupplementContext, EKeys::D, false, false);
		AddMoveKeyMapping(SupplementContext, EKeys::A, false, true);
	}

	if (!AlreadyMapped.Contains(InteractAction))
	{
		SupplementContext->MapKey(InteractAction, EKeys::E);
	}

	if (!AlreadyMapped.Contains(WorkAction))
	{
		// 기획서 3.1 에 작업 키가 좌클릭 / Space 두 가지로 적혀 있어 둘 다 넣는다.
		SupplementContext->MapKey(WorkAction, EKeys::SpaceBar);
		SupplementContext->MapKey(WorkAction, EKeys::LeftMouseButton);
	}

	if (!AlreadyMapped.Contains(ParryAction))
	{
		// 좌클릭과 Space 가 작업 키라서 패링은 우클릭에 뒀다.
		SupplementContext->MapKey(ParryAction, EKeys::RightMouseButton);
	}

	if (!AlreadyMapped.Contains(CancelWorkAction))
	{
		// 미니게임 중에는 WASD 가 방향 전환에 묶여 캐릭터가 움직이지 않는다.
		// 차를 피하려면 이 키로 먼저 빠져나와야 한다.
		SupplementContext->MapKey(CancelWorkAction, EKeys::Q);
	}
}

void ACWPlayerCharacter::AddMoveKeyMapping(UInputMappingContext* Context, const FKey& Key, bool bSwizzleToForwardAxis, bool bNegate)
{
	FEnhancedActionKeyMapping& Mapping = Context->MapKey(MoveAction, Key);

	// 모디파이어는 배열 순서대로 적용된다.
	// Swizzle 로 축을 옮긴 다음 Negate 로 부호를 뒤집어야 순서가 맞다.
	if (bSwizzleToForwardAxis)
	{
		// 기본 Order 가 YXZ 라서 X 값이 Y 로 넘어간다.
		Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
	}

	if (bNegate)
	{
		Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(this));
	}
}

void ACWPlayerCharacter::ApplyMappingContext()
{
	// 아래 실패 경로는 전부 로그를 남긴다.
	// 조용히 return 하면 "키는 눌리는데 아무 일도 안 일어나는" 상태가 되어 원인을 찾을 수 없다.
	//
	// DefaultMappingContext 가 비어 있어도 문제없다. 그 경우 보완 컨텍스트가 전부를 담고 있다.
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (PlayerController == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] PlayerController 가 없어 매핑 컨텍스트를 등록하지 못했습니다."), *GetName());
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer());
	if (InputSubsystem == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] EnhancedInput 서브시스템을 찾지 못했습니다."), *GetName());
		return;
	}

	// ClearAllMappings 를 부르지 않는다. 보완 컨텍스트까지 같이 지워버리기 때문이다.
	if (DefaultMappingContext != nullptr)
	{
		InputSubsystem->AddMappingContext(DefaultMappingContext, 0);
	}

	if (SupplementContext != nullptr && SupplementContext->GetMappings().Num() > 0)
	{
		InputSubsystem->AddMappingContext(SupplementContext, 0);
	}
}

void ACWPlayerCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MoveInput = Value.Get<FVector2D>();

	if (MoveInput.IsNearlyZero())
	{
		return;
	}

	// 기획서 6.2 - 쓰러져 있는 동안은 아무 행동도 할 수 없다.
	if (IsHitLocked())
	{
		return;
	}

	// 테두리 스케치 미니게임 중이면 WASD 는 선의 방향 전환에 쓰이고 캐릭터는 제자리에 선다.
	// 미니게임이 없는 존이나 평소에는 false 가 돌아와 그냥 이동한다.
	if (WorkerComponent->ConsumeDirectionInput(MoveInput))
	{
		return;
	}

	// 카메라 Yaw 기준으로 방향을 만든다. 나중에 카메라 각도를 틀어도 조작 방향이 같이 따라온다.
	const FRotator CameraYaw(0.f, TopDownCamera->GetComponentRotation().Yaw, 0.f);

	AddMovementInput(CameraYaw.RotateVector(FVector::ForwardVector) * MoveInput.Y
		+ CameraYaw.RotateVector(FVector::RightVector) * MoveInput.X);
}

void ACWPlayerCharacter::Interact()
{
	if (IsHitLocked())
	{
		return;
	}

	ToolHolderComponent->Interact();
}

void ACWPlayerCharacter::CancelWork()
{
	WorkerComponent->CancelWork();
}

void ACWPlayerCharacter::StartWork()
{
	if (IsHitLocked())
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[%s] Work 입력 수신"), *GetName());

	WorkerComponent->StartWork();
}

void ACWPlayerCharacter::StopWork()
{
	WorkerComponent->StopWork();
}

void ACWPlayerCharacter::Parry()
{
	if (IsHitLocked())
	{
		return;
	}

	const FVector CursorWorldLocation = ResolveCursorWorldLocation();

	// 이 줄이 안 찍히면 입력이 여기까지 오지 않은 것이고,
	// 커서가 캐릭터 위치와 같으면 커서 트레이스가 지형을 못 맞힌 것이다.
	UE_LOG(LogTemp, Log, TEXT("[%s] 패링 입력 수신. 커서=%s 캐릭터=%s"),
		*GetName(), *CursorWorldLocation.ToCompactString(), *GetActorLocation().ToCompactString());

	ParryComponent->RequestParry(CursorWorldLocation);
}

FVector ACWPlayerCharacter::ResolveCursorWorldLocation() const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (PlayerController == nullptr)
	{
		return GetActorLocation();
	}

	// 작업 지점을 잡을 때와 같은 방식이다. 커서 아래 지형을 찍어 방향의 기준으로 쓴다.
	FHitResult Hit;
	if (PlayerController->GetHitResultUnderCursorByChannel(
		UEngineTypes::ConvertToTraceType(ECC_Visibility), false, Hit) && Hit.bBlockingHit)
	{
		return Hit.ImpactPoint;
	}

	return GetActorLocation();
}
