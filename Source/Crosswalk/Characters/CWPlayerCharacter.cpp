#include "Characters/CWPlayerCharacter.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
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
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Characters/Components/CWVehicleHitReactionComponent.h"
#include "Characters/Components/CWWearableComponent.h"
#include "Characters/Wearables/CWWearableItem.h"
#include "Core/CWDecalStatics.h"
#include "Interfaces/Interface_Interactable.h"
#include "Painting/Components/CWTemperatureComponent.h"
#include "Painting/Components/CWWorkerComponent.h"
#include "Painting/Tools/CWBaseTool.h"
#include "Painting/Components/CWSupplyCarrierComponent.h"
#include "Painting/Components/CWToolHolderComponent.h"
#include "Sinkhole/Components/CWAsphaltGunnerComponent.h"
#include "Sinkhole/Components/CWSinkholeFallComponent.h"
#include "Traffic/Components/CWParryComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 머티리얼 그래프의 파라미터 이름.
	const FName HeatRatioParameterName(TEXT("HeatRatio"));
}

ACWPlayerCharacter::ACWPlayerCharacter()
{
	// 커서 조준 회전에만 쓴다. 그 외 상태는 전부 타이머와 델리게이트로 돈다.
	//
	// BP_PlayerCharacter 는 PrimaryActorTick 을 직렬화하고 있지만 오버라이드된 항목은
	// bStartWithTickEnabled 뿐이라, bCanEverTick 은 여기 값을 그대로 상속한다.
	// 그 대신 시작 시 켜져 있다고 믿을 수 없어서 BeginPlay 에서 한 번 더 켠다.
	PrimaryActorTick.bCanEverTick = true;

	// 쿼터뷰라 마우스로 시야를 돌리지 않는다. 컨트롤 회전은 쓰지 않는다.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();

	// 커서를 보는 모드에서는 이동 방향 정렬을 끈다.
	// 켜 둔 채로 회전을 넣으면 무브먼트가 매 프레임 이동 방향으로 되돌려서 아무 효과가 없다.
	Movement->bOrientRotationToMovement = !bFaceCursor;
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
	TemperatureComponent = CreateDefaultSubobject<UCWTemperatureComponent>(TEXT("TemperatureComponent"));
	SinkholeFallComponent = CreateDefaultSubobject<UCWSinkholeFallComponent>(TEXT("SinkholeFallComponent"));
	AsphaltGunnerComponent = CreateDefaultSubobject<UCWAsphaltGunnerComponent>(TEXT("AsphaltGunnerComponent"));
	SupplyCarrierComponent = CreateDefaultSubobject<UCWSupplyCarrierComponent>(TEXT("SupplyCarrierComponent"));

	/**
	 * 착용물 컴포넌트. 헬멧과 옷을 하나로 관리한다.
	 *
	 * 슬롯을 여기서 만들지 않는다. 카탈로그가 정의한 슬롯 수만큼 컴포넌트가 BeginPlay 에서
	 * 직접 만든다. 그래서 슬롯을 늘릴 때 이 생성자를 고치지 않아도 된다.
	 */
	WearableComponent = CreateDefaultSubobject<UCWWearableComponent>(TEXT("WearableComponent"));

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

	/**
	 * 캐릭터는 바닥 데칼을 받지 않는다.
	 *
	 * 안 끄면 싱크홀 균열이나 아스팔트 끈끈이 데칼 위에 서 있는 동안 그 무늬가
	 * 몸과 착용물에 그대로 씌워진다. Super 를 지난 뒤라 착용물 컴포넌트가
	 * BeginPlay 에서 만든 슬롯 메시까지 여기서 함께 잡힌다.
	 */
	UCWDecalStatics::DisableDecalReceiveOnActor(this);

	/**
	 * 틱을 확실히 켠다. 생성자의 bCanEverTick 만으로는 부족하다.
	 *
	 * BP_PlayerCharacter 애셋이 PrimaryActorTick 의 bStartWithTickEnabled 를 직렬화하고 있어서,
	 * 생성자에서 bCanEverTick 을 켜도 시작 시점에는 꺼진 채로 등록될 수 있다.
	 * 그러면 커서 조준 회전이 조용히 동작하지 않는다.
	 *
	 * bCanEverTick 은 BP 가 덮지 않으므로(생성자 주석 참고) 틱 함수 자체는 등록되어 있고,
	 * 여기서 켜는 것으로 충분하다.
	 */
	SetActorTickEnabled(true);

	// 잠금이 걸리고 풀릴 때 작업/도구를 정리해야 한다. 서버와 클라이언트 양쪽에서 들어온다.
	HitReactionComponent->OnHitLockChanged.AddDynamic(this, &ACWPlayerCharacter::HandleHitLockChanged);
	SinkholeFallComponent->OnFallLockChanged.AddDynamic(this, &ACWPlayerCharacter::HandleFallLockChanged);

	/**
	 * 체온 오라. 서버 클라 양쪽다 각각 모든 캐릭터에 대해서 생성.
	 *
	 * 오버레이 슬롯은 메시당 하나뿐이라, 새 머티리얼을 넣으면 외곽선이 자리를 잃는다.
	 * 그래서 교체하지 않고 이미 걸려 있는 외곽선 머티리얼을 MID 로 감싸서
	 * 스칼라 파라미터만 흔든다.
	 */
	UMaterialInterface* BaseOverlay = HeatOverlayMaterial;
	if (BaseOverlay == nullptr)
	{
		// 컴포넌트에 지정된 것이 없으면 스켈레탈 메시 애셋에 구워진 외곽선이 나온다.
		BaseOverlay = GetMesh()->GetOverlayMaterial();
	}

	if (BaseOverlay != nullptr)
	{
		HeatOverlayMID = UMaterialInstanceDynamic::Create(BaseOverlay, this);
		GetMesh()->SetOverlayMaterial(HeatOverlayMID);

		/**
		 * 착용물은 Super::BeginPlay 안에서 컴포넌트 BeginPlay 가 먼저 돌면서 원본 오버레이를
		 * 이미 물려받았다. 새로 만든 MID 로 다시 물려야 헬멧과 작업복도 같이 달아오른다.
		 */
		if (WearableComponent != nullptr)
		{
			WearableComponent->RefreshOverlayMaterial();
		}
	}

	TemperatureComponent->OnTemperatureChanged.AddDynamic(this, &ACWPlayerCharacter::HandleTemperatureChanged);

	// BroadcastIfChanged 는 값이 변할 때만 쏜다. 초기 상태는 여기서 한 번 밀어넣는다.
	if (HeatOverlayMID != nullptr)
	{
		HeatOverlayMID->SetScalarParameterValue(HeatRatioParameterName, TemperatureComponent->GetHeatRatio());
	}

	// 다운 시간이 끝나면 일어난다. 서버에서만 들어오고, 연출은 여기서 멀티캐스트로 뿌린다.
	HitReactionComponent->OnGetUpRequested.AddDynamic(this, &ACWPlayerCharacter::HandleGetUpRequested);

	// 패링 성공 연출. 서버가 멀티캐스트로 뿌리므로 여기서 권한을 따질 필요가 없다.
	ParryComponent->OnParrySucceeded.AddDynamic(this, &ACWPlayerCharacter::HandleParrySucceeded);

	// 래그돌을 풀 때 되돌릴 기준값을 미리 잡아 둔다.
	DefaultMeshRelativeTransform = GetMesh()->GetRelativeTransform();
	DefaultMeshCollisionProfile = GetMesh()->GetCollisionProfileName();

	// 카메라 붐도 같이 잡아 둔다. 래그돌 중 pelvis 에 붙었다 돌아오면 스케일이 오염된다.
	DefaultCameraBoomRelativeTransform = CameraBoom->GetRelativeTransform();
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

	// 싱크홀에 빠져 있는 동안과 리스폰 직후 무적 구간에는 차에 치이지 않는다. (싱크홀 기획서 6-2)
	//
	// 무적이 없으면 스타트 지점에 차가 지나가는 순간 리스폰했을 때 연속으로 당해
	// 플레이어가 손쓸 수 없는 상황이 된다.
	if (SinkholeFallComponent->IsFallLocked() || SinkholeFallComponent->IsInvincible())
	{
		return;
	}

	HitReactionComponent->HandleVehicleHit(HitContext);

	// 들고 있던 페인트 통이 전부 터진다. (기획서 6-4)
	//
	// 넉백을 먹이기 전에, 지금 위치를 그대로 넘긴다. 오염은 착지 지점이 아니라 피격 지점에 생겨야 한다.
	// 굴러간 끝에 생기면 오염 위치가 매번 달라지고, 차도 한복판을 정리하러 들어가는 위험이 사라진다.
	SupplyCarrierComponent->BreakCansAndSpill(GetActorLocation());

	// 차가 밀고 나가던 방향으로 날린다. 세기는 차량 속도에 비례시킨다.
	const FVector LaunchDirection = HitContext.HitDirection.GetSafeNormal2D();
	const FVector LaunchVelocity = LaunchDirection * HitContext.VehicleSpeed * KnockbackSpeedScale
		+ FVector::UpVector * KnockbackUpSpeed;

	// [진단로그 1/4] 서버가 보내는 시작 위치. 각 머신의 2번 로그와 비교해 전달이 제대로 됐는지 확인한다.
	// 이름을 찍어두는 이유: 4인 테스트라 동시에 여러 명이 맞으면 로그가 섞여서 어느 캐릭터 건지 헷갈린다.
	UE_LOG(LogTemp, Warning, TEXT("[RagdollSync] [%s] 1.서버발신 위치=%s"), *GetName(), *GetActorLocation().ToString());

	Multicast_StartRagdoll(LaunchVelocity, GetActorLocation());
}

void ACWPlayerCharacter::Multicast_StartRagdoll_Implementation(
	FVector_NetQuantize LaunchVelocity,
	FVector_NetQuantize ServerActorLocation)
{
	// 위치 동기화는 StartRagdoll 안, 무브먼트를 끈 다음으로 옮겼다 (아래 StartRagdoll 주석 참고).
	StartRagdoll(LaunchVelocity, ServerActorLocation);

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

void ACWPlayerCharacter::Multicast_StopRagdoll_Implementation(FVector_NetQuantize RecoverLocation, float RecoverYaw, bool bFaceDown)
{
	StopRagdoll(RecoverLocation, RecoverYaw);

	// 몬타주는 각 머신이 자기 화면에서 돌린다. 길이는 어디서 재도 같다.
	const float MontageLength = PlayGetUpMontage(bFaceDown);

	// 재생할 몬타주가 없으면 밑에 깔 이유도 없다. 그대로 두면 잠깐 누운 자세로 서 있게 된다.
	if (MontageLength <= 0.f)
	{
		bUsingRagdollPose = false;
		GetWorldTimerManager().ClearTimer(RagdollPoseBlendTimerHandle);

		FinishGetUp();
		return;
	}

	/**
	 * 블렌드 아웃 시점은 각 머신이 자기 타이머로 잡는다. 서버만 재는 게 아니다.
	 *
	 * 몬타주 재생이 모든 머신에서 같은 시점에 시작되므로 결과도 같다. 서버의 잠금 해제
	 * 복제를 기다리면 그만큼 늦게 블렌드 아웃해서, 남의 화면에서만 기상 포즈가 더 오래 굳어 있다.
	 */
	const float LockDuration = MontageLength - GetUpUnlockLeadTime;

	// 리드 타임이 몬타주보다 길면 기다릴 게 없다. SetTimer 는 0 이하를 받으면 조용히 타이머를 지운다.
	if (LockDuration <= 0.f)
	{
		FinishGetUp();
		return;
	}

	GetWorldTimerManager().SetTimer(
		GetUpTimerHandle,
		this,
		&ACWPlayerCharacter::FinishGetUp,
		LockDuration,
		false
	);
}

void ACWPlayerCharacter::StartRagdoll(const FVector& LaunchVelocity, const FVector& ServerActorLocation)
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

	/**
	 * 일어나던 중에 다시 치일 수 있다. GetUpUnlockLeadTime 만큼은 몬타주가 남아 있는데
	 * 잠금은 이미 풀린 구간이라, 그 사이에 들어온 피격이 여기로 온다.
	 * 몬타주를 세우지 않으면 래그돌 위로 기상 애니가 계속 흘러 포즈가 섞인다.
	 */
	StopAnimMontage();
	GetWorldTimerManager().ClearTimer(GetUpTimerHandle);

	// 물리가 도는 동안에는 스냅샷 포즈가 의미 없다. 남아 있으면 다음 기상 때까지 끌려간다.
	bUsingRagdollPose = false;
	GetWorldTimerManager().ClearTimer(RagdollPoseBlendTimerHandle);

	// 일어나던 중(복귀 이어붙이기 진행 중)에 다시 치이면, 이 타이머가 살아남아
	// 방금 SetSimulatePhysics(true) 한 메시의 상대 트랜스폼을 계속 덮어쓰려 든다.
	GetWorldTimerManager().ClearTimer(RecoverBlendTimerHandle);

	// 이전 비행에서 누적된 값이 새 비행 초반에 바로 보정을 쏘게 만들면 안 된다.
	RagdollCorrectionElapsed = 0.f;

	// 무브먼트가 계속 돌면 캡슐이 캐릭터를 끌고 다니면서 물리와 서로 밀어낸다.
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->StopMovementImmediately();
	Movement->DisableMovement();
	Movement->SetComponentTickEnabled(false);

	/**
	 * 무브먼트를 끈 다음에 위치를 맞춘다.
	 *
	 * 맞은 사람 본인 화면(오토너머스 프록시)만 이 캐릭터의 무브먼트를 로컬로 예측하고 있다.
	 * 무브먼트가 아직 살아있는 상태에서 SetActorLocation을 먼저 부르면, 다음 틱에 무브먼트
	 * 컴포넌트가 자기 예측 위치와 다르다고 보고 이 위치를 다시 밀어낼 수 있다. 서버나
	 * 시뮬레이티드 프록시(다른 사람 화면)는 로컬 예측이 없어서 이 문제가 안 보였다.
	 */
	SetActorLocation(ServerActorLocation, false, nullptr, ETeleportType::TeleportPhysics);

	// [진단로그 2/4] Role별로 실제 반영된 위치. 오토너머스 프록시(내 캐릭터, 값=2)에서만
	// 목표위치와 실제위치가 벌어진다면 무브먼트가 여전히 이 SetActorLocation을 밀어내고 있다는 뜻이다.
	UE_LOG(LogTemp, Warning, TEXT("[RagdollSync] [%s] 2.StartRagdoll직후 Role=%d(0무권한/1시뮬/2오토/3서버) 목표위치=%s 실제위치=%s"),
		*GetName(), (int32)GetLocalRole(), *ServerActorLocation.ToString(), *GetActorLocation().ToString());

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

void ACWPlayerCharacter::StopRagdoll(const FVector& RecoverLocation, float RecoverYaw)
{
	if (!bRagdollActive)
	{
		return;
	}

	bRagdollActive = false;

	USkeletalMeshComponent* MeshComponent = GetMesh();

	// [진단로그 4/4] 순간이동 직전 서버가 준 최종 위치 vs 지금 내 pelvis 위치. 여기 Delta가 크면
	// 3번 로그의 비행 중 보정이 다 끝나기도 전에 서버가 "멈췄다"고 판단해 StopRagdoll을 쏜 것이다.
	UE_LOG(LogTemp, Warning, TEXT("[RagdollSync] [%s] 4.StopRagdoll직전 Role=%d 서버RecoverLoc=%s 내pelvis=%s Delta크기=%.1f"),
		*GetName(), (int32)GetLocalRole(), *RecoverLocation.ToString(), *MeshComponent->GetSocketLocation(RagdollPivotBone).ToString(),
		(RecoverLocation - MeshComponent->GetSocketLocation(RagdollPivotBone)).Size());

	// 캡슐을 옮기기 전, 메시가 지금 화면에 보이던 월드 위치를 떠 둔다.
	// 서버·클라 물리가 크게 벌어졌을 때 곧장 스냅하면 순간이동한 것처럼 튀어서,
	// 이 값에서부터 TickRecoverBlend 로 기본 포즈까지 짧게 이어붙인다.
	const FTransform PreCorrectionMeshWorldTransform = MeshComponent->GetComponentTransform();

	/**
	 * 쓰러진 포즈를 물리를 끄기 전에 떠 둔다. 끄고 나면 이미 애니메이션 포즈로 돌아가 있다.
	 *
	 * 스냅샷은 컴포넌트 스페이스로 저장되므로, 아래에서 캡슐을 옮기고 돌리면 포즈도 같이 돌아간다.
	 * 그래서 RecoverYaw 를 몸이 누워 있던 방향으로 맞추는 게 중요하다. 방향이 어긋나면
	 * 스냅샷 포즈가 엉뚱한 쪽을 보고 시작해 기상 첫 프레임에 몸이 홱 돈다.
	 */
	if (UAnimInstance* AnimInstance = MeshComponent->GetAnimInstance())
	{
		AnimInstance->SavePoseSnapshot(RagdollPoseSnapshotName);

		// 스냅샷을 뜬 경우에만 켠다. 못 떴는데 켜면 AnimBP 가 빈 포즈를 밑에 깔아 T 포즈가 보인다.
		bUsingRagdollPose = true;
		GetWorldTimerManager().SetTimer(
			RagdollPoseBlendTimerHandle,
			FTimerDelegate::CreateWeakLambda(this, [this]() { bUsingRagdollPose = false; }),
			FMath::Max(RagdollPoseBlendTime, KINDA_SMALL_NUMBER),
			false
		);
	}

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
	SetActorRotation(FRotator(0.f, RecoverYaw, 0.f), ETeleportType::TeleportPhysics);

	MeshComponent->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::KeepRelativeTransform);

	/**
	 * 래그돌 동안 캡슐은 제자리에 있고 메시만 월드에서 굴러다닌다. 그래서 메시의 상대
	 * 트랜스폼에는 물리가 만든 큰 오프셋이 남아 있고, 여기서 원래 값으로 되돌려야 한다.
	 *
	 * Teleport 플래그를 주지 않으면 이 이동이 반영되지 않는다. 그러면 캡슐만 기상 위치로
	 * 가고 메시는 엉뚱한 오프셋을 유지해서, 화면에서는 캐릭터가 사라진 것처럼 보인다.
	 *
	 * 다만 기본 포즈로 곧장 스냅하지는 않는다. 방금 캡슐을 옮기기 전 메시가 있던 자리를
	 * 새 캡슐 기준 상대 트랜스폼으로 환산해 먼저 넣고, TickRecoverBlend 가 그 값에서
	 * DefaultMeshRelativeTransform 까지 짧게 이어붙인다. 오차가 클수록 이 처리가 효과를 낸다.
	 */
	RecoverBlendFromRelativeTransform = PreCorrectionMeshWorldTransform.GetRelativeTransform(GetCapsuleComponent()->GetComponentTransform());

	/**
	 * 이어붙일 실제 거리(=오차)에 맞춰 이어붙이는 시간을 늘린다.
	 *
	 * 비행 중 임펄스 보정([RagdollSync] 3번 로그)이 물리 발산을 다 못 따라잡고 남은 오차가
	 * StopRagdoll 시점까지 그대로 남을 수 있다는 게 로그로 확인됐다. 오차가 1m 넘게 남아도
	 * 고정된 짧은 시간에 억지로 이어붙이면 그 자체가 순간이동처럼 보인다. 오차가 클수록
	 * 더 천천히 미끄러지듯 이어붙여서, 눈에는 "늦게 따라잡는" 것처럼 보이게 한다.
	 */
	const float RecoverBlendDistance = (RecoverBlendFromRelativeTransform.GetLocation() - DefaultMeshRelativeTransform.GetLocation()).Size();
	RecoverBlendActiveDuration = RecoverBlendDuration > 0.f
		? FMath::Clamp(RecoverBlendDistance / RecoverBlendSpeed, RecoverBlendDuration, RecoverBlendMaxDuration)
		: 0.f;

	if (RecoverBlendActiveDuration > 0.f)
	{
		MeshComponent->SetRelativeTransform(RecoverBlendFromRelativeTransform, false, nullptr, ETeleportType::TeleportPhysics);

		RecoverBlendElapsed = 0.f;
		GetWorldTimerManager().SetTimer(
			RecoverBlendTimerHandle,
			this,
			&ACWPlayerCharacter::TickRecoverBlend,
			RecoverBlendTickInterval,
			true
		);
	}
	else
	{
		MeshComponent->SetRelativeTransform(DefaultMeshRelativeTransform, false, nullptr, ETeleportType::TeleportPhysics);
		VerifyRecoverAlignment();
	}

	/**
	 * 카메라를 캡슐 위로 돌려놓는다. 붐의 원점은 캐릭터 위치여야 한다.
	 *
	 * 위치만 0 으로 되돌리면 스케일이 남는다. 래그돌을 켤 때 KeepWorldTransform 으로 pelvis 에
	 * 붙는데, 그 규칙은 월드 트랜스폼을 유지하려고 상대 스케일까지 다시 계산한다.
	 *
	 *   SetRelativeScale3D_Direct(RelativeTM.GetScale3D());   // SceneComponent.cpp
	 *
	 * 메시 스케일이 1 이면 결과가 같아서 지금 당장 눈에 보이는 문제는 아니다. 다만 캐릭터를
	 * 갈아끼우며 스케일을 조정하면 붐 팔 길이가 조용히 어긋난다.
	 * 메시와 같은 방식으로 기본 트랜스폼을 통째로 되돌려 그 여지를 없앤다.
	 */
	CameraBoom->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	CameraBoom->SetRelativeTransform(DefaultCameraBoomRelativeTransform);

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->StopMovementImmediately();
	Movement->SetComponentTickEnabled(true);
	Movement->SetMovementMode(MOVE_Walking);

	if (HasAuthority())
	{
		SetReplicateMovement(true);
	}
}

void ACWPlayerCharacter::TickRecoverBlend()
{
	RecoverBlendElapsed += RecoverBlendTickInterval;

	const float Alpha = FMath::Clamp(RecoverBlendElapsed / RecoverBlendActiveDuration, 0.f, 1.f);

	const FTransform Blended(
		FQuat::Slerp(RecoverBlendFromRelativeTransform.GetRotation(), DefaultMeshRelativeTransform.GetRotation(), Alpha),
		FMath::Lerp(RecoverBlendFromRelativeTransform.GetLocation(), DefaultMeshRelativeTransform.GetLocation(), Alpha),
		FMath::Lerp(RecoverBlendFromRelativeTransform.GetScale3D(), DefaultMeshRelativeTransform.GetScale3D(), Alpha)
	);

	GetMesh()->SetRelativeTransform(Blended, false, nullptr, ETeleportType::TeleportPhysics);

	if (Alpha >= 1.f)
	{
		GetWorldTimerManager().ClearTimer(RecoverBlendTimerHandle);

		// 블렌드가 끝나야 기본 트랜스폼에 도달한다. 그 전에 재면 어긋난 값이 정상으로 잡힌다.
		VerifyRecoverAlignment();
	}
}

void ACWPlayerCharacter::VerifyRecoverAlignment() const
{
	const USkeletalMeshComponent* MeshComponent = GetMesh();
	if (MeshComponent == nullptr)
	{
		return;
	}

	/**
	 * 캡슐과 메시의 거리를 그냥 재면 안 된다. 메시 기본 오프셋이 (0, 0, -90) 이라
	 * 정상 상태에서도 90cm 가 나온다. 기본 트랜스폼으로 계산한 기대 위치와의 편차를 잰다.
	 *
	 * FTransform 은 자식 * 부모 순서로 곱해 월드를 얻는다.
	 */
	const FTransform& CapsuleTransform = GetCapsuleComponent()->GetComponentTransform();

	const FVector ExpectedMeshLocation = (DefaultMeshRelativeTransform * CapsuleTransform).GetLocation();
	const FVector ExpectedBoomLocation = (DefaultCameraBoomRelativeTransform * CapsuleTransform).GetLocation();

	const float MeshDrift = FVector::Dist(ExpectedMeshLocation, MeshComponent->GetComponentLocation());
	const float BoomDrift = FVector::Dist(ExpectedBoomLocation, CameraBoom->GetComponentLocation());

	// 정상이면 둘 다 0 에 가깝다. 물리 한 틱 차이 정도는 넘어간다.
	constexpr float AlignmentTolerance = 50.f;

	if (MeshDrift <= AlignmentTolerance && BoomDrift <= AlignmentTolerance)
	{
		return;
	}

	/**
	 * 여기가 찍히면 기상 후 몸이나 카메라가 제자리에 없다는 뜻이다.
	 *
	 * 과거에 이 어긋남이 피격마다 누적되어, 어느 방향으로 쌓이느냐에 따라 "메시가 사라진다"
	 * 또는 "카메라가 몸에 박힌다"로 나타났다. #32 와 #37 이 각각의 경로를 막았지만,
	 * 래그돌 복귀 순서를 다시 손대면 되살아날 수 있는 종류의 버그라 감시를 남긴다.
	 */
	UE_LOG(LogTemp, Warning,
		TEXT("[%s] 기상 정렬이 어긋났습니다. 메시 %.0fcm, 카메라 붐 %.0fcm 벗어남 (허용 %.0fcm)"),
		*GetName(), MeshDrift, BoomDrift, AlignmentTolerance);
}

void ACWPlayerCharacter::CheckRagdollRest()
{
	if (!HasAuthority() || !bRagdollActive)
	{
		GetWorldTimerManager().ClearTimer(RagdollRestTimerHandle);
		return;
	}

	/**
	 * 날아가는 동안 서버가 확인한 위치를 주기적으로 뿌린다.
	 *
	 * 시작점을 맞춰도(Multicast_StartRagdoll) 물리 시뮬레이션은 각 머신에서 따로 돌아
	 * 시간이 지날수록 벌어진다. 이 틱에 얹어서 RagdollCorrectionInterval마다 한 번씩만
	 * 내보낸다 — 별도 타이머를 새로 만들지 않는다.
	 */
	RagdollCorrectionElapsed += RagdollRestCheckInterval;
	if (RagdollCorrectionElapsed >= RagdollCorrectionInterval)
	{
		RagdollCorrectionElapsed = 0.f;
		Multicast_SyncRagdollPosition(GetMesh()->GetSocketLocation(RagdollPivotBone));
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

void ACWPlayerCharacter::Multicast_SyncRagdollPosition_Implementation(FVector_NetQuantize ServerPelvisLocation)
{
	// 아직 안 날고 있거나 이미 복귀했으면 늦게 도착한 보정이다. 무시한다.
	if (!bRagdollActive)
	{
		return;
	}

	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (MeshComponent == nullptr)
	{
		return;
	}

	const FVector Delta = FVector(ServerPelvisLocation) - MeshComponent->GetSocketLocation(RagdollPivotBone);

	// [진단로그 3/4] 시간이 지나며 서버-내 pelvis 오차가 벌어지는지/줄어드는지 추세를 본다.
	// 임계값(15cm) 미달로 보정을 건너뛰는 경우도 포함해서 매번 찍는다.
	UE_LOG(LogTemp, Warning, TEXT("[RagdollSync] [%s] 3.비행중보정 Role=%d 서버pelvis=%s 내pelvis=%s Delta크기=%.1f"),
		*GetName(), (int32)GetLocalRole(), *FVector(ServerPelvisLocation).ToString(), *MeshComponent->GetSocketLocation(RagdollPivotBone).ToString(), Delta.Size());

	// 오차가 작으면 그냥 둔다. 매번 미세하게 당기면 가만히 있어야 할 때도 떨리는 것처럼 보인다.
	if (Delta.SizeSquared() < FMath::Square(RagdollCorrectionMinDistance))
	{
		return;
	}

	// 순간이동시키지 않는다. 서버 위치 쪽으로 당기는 임펄스만 살짝 얹어서, 물리 시뮬레이션이
	// 스스로 그 방향으로 흘러가게 한다. bVelChange 를 켜서 질량과 무관하게 같은 세기로 당긴다.
	MeshComponent->AddImpulse(Delta * RagdollCorrectionStrength, RagdollPivotBone, true);
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

void ACWPlayerCharacter::HandleFallLockChanged(UCWSinkholeFallComponent* /*FallComponent*/, bool bFallLocked)
{
	// 작업 중단 상태는 복제되므로 서버에서만 바꾼다.
	if (!HasAuthority())
	{
		return;
	}

	WorkerComponent->SetWorkBlocked(bFallLocked);

	// 들고 있던 페인트 통이 전부 소실된다.
	//
	// 차량 피격(BreakCansAndSpill)과 달리 오염을 만들지 않는다. 통은 플레이어와 함께
	// 구멍 안으로 떨어진 것이라, 도로 위에 페인트가 쏟아져 있으면 그림이 맞지 않는다.
	// 정리할 오염이 남지 않는 대신 통을 다시 받으러 가는 시간이 페널티가 된다.
	if (bFallLocked)
	{
		SupplyCarrierComponent->BreakAllCans();
	}
}

void ACWPlayerCharacter::HandleHitLockChanged(UCWVehicleHitReactionComponent* /*HitReactionComp*/, bool bHitLocked)
{
	// 연출은 각 머신이 알아서 낸다. 아래 권한 검사보다 앞에 둬야 클라이언트에서도 들린다.
	if (bHitLocked)
	{
		PlayVehicleHitSound();
		PlayVehicleHitEffect();
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

	/**
	 * 일어나는 처리는 HandleGetUpRequested 가 이미 했다. 여기까지 래그돌이 살아 있다면
	 * 기상 경로를 타지 못하고 컴포넌트의 안전장치로 잠금만 풀린 것이다.
	 *
	 * 연출은 포기하더라도 몸은 세워야 한다. 쓰러진 채로 조작이 풀리면 캡슐만 움직이고
	 * 화면 속 캐릭터는 바닥에 누운 그대로다.
	 */
	if (bRagdollActive)
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] 기상 절차 없이 잠금이 풀렸습니다. 래그돌만 정리합니다."), *GetName());
		Multicast_StopRagdoll(ResolveRecoverLocation(), ResolveRecoverYaw(IsRagdollFaceDown()), IsRagdollFaceDown());
	}
}

void ACWPlayerCharacter::HandleGetUpRequested(UCWVehicleHitReactionComponent* /*HitReactionComp*/)
{
	if (!HasAuthority())
	{
		return;
	}

	/**
	 * 래그돌이 꺼져 있으면 일어날 것도 없다. 물리 애셋이 없어 StartRagdoll 이 통째로 빠진 경우다.
	 * 기다리게 두면 MaxGetUpWait 만큼 서 있는 채로 조작이 막힌다.
	 */
	if (!bRagdollActive)
	{
		HitReactionComponent->NotifyGetUpFinished();
		return;
	}

	// 앞뒤와 방향은 물리를 끄기 전에 재야 한다. 끄고 나면 몸이 이미 애니메이션 포즈로 돌아간다.
	const bool bFaceDown = IsRagdollFaceDown();

	Multicast_StopRagdoll(ResolveRecoverLocation(), ResolveRecoverYaw(bFaceDown), bFaceDown);
}

float ACWPlayerCharacter::PlayGetUpMontage(bool bFaceDown)
{
	UAnimMontage* Montage = bFaceDown ? GetUpFromFrontMontage : GetUpFromBackMontage;
	float PlayRate = bFaceDown ? GetUpFromFrontPlayRate : GetUpFromBackPlayRate;

	// 한쪽만 채워 넣었으면 있는 쪽으로 대신한다. 방향이 어색해도 아예 안 일어나는 것보단 낫다.
	// 재생 속도도 그 몬타주에 짝지은 값으로 함께 바꾼다. 남의 속도를 쓰면 길이 계산이 어긋난다.
	if (Montage == nullptr)
	{
		Montage = bFaceDown ? GetUpFromBackMontage : GetUpFromFrontMontage;
		PlayRate = bFaceDown ? GetUpFromBackPlayRate : GetUpFromFrontPlayRate;
	}

	if (Montage == nullptr)
	{
		return 0.f;
	}

	UAnimInstance* AnimInstance = GetMesh() != nullptr ? GetMesh()->GetAnimInstance() : nullptr;
	if (AnimInstance == nullptr)
	{
		return 0.f;
	}

	/**
	 * ACharacter::PlayAnimMontage 를 쓰지 않고 직접 재생한다. 반환값의 의미가 필요하기 때문이다.
	 *
	 * PlayAnimMontage 는 ReturnValueType 을 넘기지 않아 기본값인 MontageLength 로 받는다.
	 * 그건 몬타주 길이라서 재생 속도를 반영하지 않는다. 속도를 2배로 주면 몬타주는 절반 시간에
	 * 끝나는데 잠금은 원래 길이만큼 유지되어, 다 일어선 자세로 가만히 서 있는 구간이 생긴다.
	 *
	 * Duration 은 길이를 재생 속도로 나눈 실제 재생 시간이다. 애셋 쪽 Rate Scale 은 이미
	 * 몬타주 길이에 녹아 있으므로, 어느 쪽에서 속도를 바꾸든 이 값 하나로 맞는다.
	 *
	 * AnimBP 에 슬롯 노드가 없으면 시간은 정상으로 나오지만 화면에는 아무것도 보이지 않는다.
	 */
	return AnimInstance->Montage_Play(
		Montage,
		PlayRate,
		EMontagePlayReturnType::Duration
	);
}

void ACWPlayerCharacter::FinishGetUp()
{
	GetWorldTimerManager().ClearTimer(GetUpTimerHandle);

	/**
	 * 조작을 돌려주는 것과 같은 시점에 몬타주를 블렌드 아웃시킨다.
	 *
	 * 몬타주를 그냥 두면 슬롯이 기상 마지막 포즈로 100% 덮고 있어서, 캐릭터가 그 자세로
	 * 굳은 채 미끄러지다가 몬타주가 끝나는 순간 이동 애니로 뚝 끊긴다.
	 * 블렌드 아웃 길이는 몬타주 애셋의 Blend Out 설정을 그대로 쓴다.
	 */
	StopAnimMontage();

	if (!HasAuthority())
	{
		return;
	}

	HitReactionComponent->NotifyGetUpFinished();
}

bool ACWPlayerCharacter::IsRagdollFaceDown() const
{
	FVector BodyUp = FVector::ZeroVector;
	FVector BodyForward = FVector::ZeroVector;

	// 축을 못 구하면 누운 것으로 친다. 기본 몬타주가 그쪽이라 폴백 결과와 어긋나지 않는다.
	if (!ResolveRagdollBodyAxes(BodyUp, BodyForward))
	{
		return false;
	}

	// 가슴이 바닥을 향하고 있으면 엎드린 것이다.
	return BodyForward.Z < 0.f;
}

float ACWPlayerCharacter::ResolveRecoverYaw(bool bFaceDown) const
{
	FVector BodyUp = FVector::ZeroVector;
	FVector BodyForward = FVector::ZeroVector;

	if (!ResolveRagdollBodyAxes(BodyUp, BodyForward))
	{
		return GetActorRotation().Yaw;
	}

	// 누운 몸에서 머리가 향한 쪽을 캐릭터 정면으로 삼는다. 기상 애니 대부분이 이 기준으로 만들어진다.
	const FVector PlanarBodyUp = FVector(BodyUp.X, BodyUp.Y, 0.f);

	// 머리로 처박혀 몸이 거의 수직이면 머리 방향에 의미가 없다. 그냥 보던 쪽으로 일어난다.
	if (PlanarBodyUp.IsNearlyZero())
	{
		return GetActorRotation().Yaw;
	}

	const float YawOffset = bFaceDown ? GetUpFromFrontYawOffset : GetUpFromBackYawOffset;

	return FRotator::NormalizeAxis(PlanarBodyUp.Rotation().Yaw + YawOffset);
}

bool ACWPlayerCharacter::ResolveRagdollBodyAxes(FVector& OutBodyUp, FVector& OutBodyForward) const
{
	const USkeletalMeshComponent* MeshComponent = GetMesh();
	if (MeshComponent == nullptr)
	{
		return false;
	}

	/**
	 * 본 이름이 틀리면 GetSocketLocation 이 경고 없이 컴포넌트 원점을 돌려준다.
	 * 그러면 축이 엉뚱하게 잡혀 앞뒤가 무작위로 뒤집히는데, 원인을 찾기가 매우 어렵다.
	 */
	if (MeshComponent->GetBoneIndex(RagdollPivotBone) == INDEX_NONE
		|| MeshComponent->GetBoneIndex(RagdollHeadBone) == INDEX_NONE
		|| MeshComponent->GetBoneIndex(RagdollLeftShoulderBone) == INDEX_NONE
		|| MeshComponent->GetBoneIndex(RagdollRightShoulderBone) == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] 기상 방향 판별에 쓸 본을 찾지 못했습니다. Pivot=%s Head=%s ShoulderL=%s ShoulderR=%s (스켈레톤의 본 이름을 확인하세요)"),
			*GetName(), *RagdollPivotBone.ToString(), *RagdollHeadBone.ToString(),
			*RagdollLeftShoulderBone.ToString(), *RagdollRightShoulderBone.ToString());

		return false;
	}

	const FVector PelvisLocation = MeshComponent->GetSocketLocation(RagdollPivotBone);
	const FVector HeadLocation = MeshComponent->GetSocketLocation(RagdollHeadBone);
	const FVector LeftShoulderLocation = MeshComponent->GetSocketLocation(RagdollLeftShoulderBone);
	const FVector RightShoulderLocation = MeshComponent->GetSocketLocation(RagdollRightShoulderBone);

	OutBodyUp = (HeadLocation - PelvisLocation).GetSafeNormal();
	const FVector BodyRight = (RightShoulderLocation - LeftShoulderLocation).GetSafeNormal();

	if (OutBodyUp.IsNearlyZero() || BodyRight.IsNearlyZero())
	{
		return false;
	}

	// 언리얼은 왼손 좌표계라 Right × Up 이 Forward 가 된다.
	OutBodyForward = FVector::CrossProduct(BodyRight, OutBodyUp).GetSafeNormal();

	return !OutBodyForward.IsNearlyZero();
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

void ACWPlayerCharacter::HandleTemperatureChanged(UCWTemperatureComponent* InTemperatureComponent, float Temperature, float HeatRatio)
{
	if (HeatOverlayMID == nullptr)
	{
		return;
	}

	HeatOverlayMID->SetScalarParameterValue(HeatRatioParameterName, HeatRatio);
}

void ACWPlayerCharacter::PlayVehicleHitSound()
{
	/**
	 * 부딪히는 소리는 충돌 지점에 놔둔다. 몸이 날아가도 부딪힌 자리는 그 자리다.
	 *
	 * 본인 피격도 감쇠에 태운다. 따로 2D 로 뺄 이유가 없다.
	 * 내가 맞은 지점은 리스너(캐릭터 위치)와 거리가 0 이라 AttenuationInnerRadius 안쪽이고,
	 * NonSpatializedRadiusStart 안에도 들어가 어차피 풀볼륨 2D 로 재생된다.
	 * 남이 맞는 소리만 거리에 따라 자연스럽게 멀어진다.
	 */
	if (VehicleHitSound != nullptr)
	{
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

	/**
	 * 비명은 사람 입에서 나오는 소리라 몸에 붙여 같이 날아가게 한다.
	 *
	 * 충돌 지점에 고정하면 안 된다. 리스너가 붐에 물려 있어 날아가는 몸을 따라가는데,
	 * 소리만 맞은 자리에 남으면 재생 도중에 감쇠 거리를 벗어나 중간에 꺼진다.
	 * 넉백이 세서 비명 길이보다 훨씬 빨리 2500cm 를 벗어난다.
	 */
	if (VehicleHitVoiceSound != nullptr)
	{
		UGameplayStatics::SpawnSoundAttached(
			VehicleHitVoiceSound,
			GetMesh(),
			RagdollHeadBone,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget,
			// 캐릭터가 사라지면 비명도 같이 멈춘다. 두면 빈 자리에서 소리만 남는다.
			true,
			1.f,
			1.f,
			0.f,
			ResolveSoundAttenuation()
		);
	}
}

void ACWPlayerCharacter::PlayVehicleHitEffect()
{
	if (VehicleHitEffect == nullptr)
	{
		return;
	}

	const FCWVehicleHitContext HitContext = HitReactionComponent->GetLastHitContext();

	// 부딪히는 소리와 같은 자리에 낸다. 몸을 따라가면 충격이 어디서 났는지 알 수 없다.
	FVector EffectLocation = HitContext.HitLocation;

	// 사운드와 같은 폴백이다. 클라이언트에서는 잠금 플래그가 피격 정보보다 먼저 도착할 수 있다.
	if (EffectLocation.IsNearlyZero())
	{
		EffectLocation = GetActorLocation();
	}

	/**
	 * 이펙트 정면을 차가 밀고 나가는 방향으로 돌린다. 방향성이 있는 이펙트(스파크, 임팩트
	 * 콘)를 꽂았을 때 튀는 쪽이 넉백 방향과 맞아야 한다.
	 * HitDirection 이 비어 있으면 Rotation() 이 ZeroRotator 를 내므로 그대로 둬도 안전하다.
	 */
	const FRotator EffectRotation = HitContext.HitDirection.Rotation();

	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this,
		VehicleHitEffect,
		EffectLocation,
		EffectRotation
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

bool ACWPlayerCharacter::IsFallLocked() const
{
	return SinkholeFallComponent->IsFallLocked();
}

bool ACWPlayerCharacter::IsActionLocked() const
{
	return IsHitLocked() || IsFallLocked();
}

bool ACWPlayerCharacter::IsUsingRagdollPose() const
{
	return bUsingRagdollPose;
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

	// 헬멧 교체. 누르는 순간 한 번만.
	EnhancedInput->BindAction(SwapHelmetAction, ETriggerEvent::Started, this, &ACWPlayerCharacter::SwapHelmet);

	// 보급 입력 3종. (기획서 6-1 ~ 6-3)
	// 도구 집기(E)와 별도 키인 이유: 도구와 통은 동시에 들 수 없어서(6-6), 같은 키로 묶으면
	// 박스 앞에 도구가 떨어져 있을 때 무엇이 집히는지 플레이어가 예측할 수 없다.
	EnhancedInput->BindAction(PickUpCanAction, ETriggerEvent::Started, this, &ACWPlayerCharacter::PickUpCan);
	EnhancedInput->BindAction(DropCanAction, ETriggerEvent::Started, this, &ACWPlayerCharacter::DropCan);
	EnhancedInput->BindAction(ChargePaintAction, ETriggerEvent::Started, this, &ACWPlayerCharacter::ChargePaint);

	// 옷 입기/벗기.
	EnhancedInput->BindAction(ToggleOutfitAction, ETriggerEvent::Started, this, &ACWPlayerCharacter::ToggleOutfit);
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
	EnsureInputAction(SwapHelmetAction, TEXT("BuiltIn_SwapHelmet"), EInputActionValueType::Boolean);
	EnsureInputAction(PickUpCanAction, TEXT("BuiltIn_PickUpCan"), EInputActionValueType::Boolean);
	EnsureInputAction(DropCanAction, TEXT("BuiltIn_DropCan"), EInputActionValueType::Boolean);
	EnsureInputAction(ChargePaintAction, TEXT("BuiltIn_ChargePaint"), EInputActionValueType::Boolean);
	EnsureInputAction(ToggleOutfitAction, TEXT("BuiltIn_ToggleOutfit"), EInputActionValueType::Boolean);

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

	if (!AlreadyMapped.Contains(SwapHelmetAction))
	{
		// WASD·E·Q·Space·마우스가 이미 차 있어서 손이 닿는 자리 중 남은 키로 골랐다.
		SupplementContext->MapKey(SwapHelmetAction, EKeys::H);
	}

	// 보급 키. F 로 꺼내고 G 로 반납하고 R 로 붓는다. (기획서 6-1 ~ 6-3)
	//
	// 들기와 내려놓기를 한 키로 토글하지 않는 이유: 둘 다 박스 근처에서만 되는 조작이라
	// 같은 자리에서 두 의도가 겹친다. 통을 더 꺼내려다 반납하는 사고가 난다.
	if (PickUpCanAction != nullptr)
	{
		SupplementContext->MapKey(PickUpCanAction, EKeys::F);
	}

	if (DropCanAction != nullptr)
	{
		SupplementContext->MapKey(DropCanAction, EKeys::G);
	}

	if (ChargePaintAction != nullptr)
	{
		SupplementContext->MapKey(ChargePaintAction, EKeys::R);
	}

	if (!AlreadyMapped.Contains(ToggleOutfitAction))
	{
		// 헬멧(H) 바로 옆이라 착용 관련 키끼리 붙어 있게 된다.
		SupplementContext->MapKey(ToggleOutfitAction, EKeys::J);
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
	if (IsActionLocked())
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
	if (IsActionLocked())
	{
		return;
	}

	float InteractableDistanceSquared = TNumericLimits<float>::Max();
	AActor* Interactable = FindNearestInteractable(InteractableDistanceSquared);

	if (Interactable == nullptr)
	{
		ToolHolderComponent->Interact();
		return;
	}

	// 도구를 들고 있으면 도구 쪽 경로는 "내려놓기" 다. 트렁크 앞에서 E 를 눌렀는데 발밑에
	// 도구가 떨어지면 반납한 것으로 착각하게 되므로, 이 경우는 상호작용 대상을 우선한다.
	if (ToolHolderComponent->GetHeldTool() != nullptr)
	{
		PerformInteractWith(Interactable);
		return;
	}

	// 맨손이면 둘 다 후보다. 가까운 쪽을 고른다.
	// 차량 옆에 도구가 떨어져 있어도 바로 집을 수 있어야 한다.
	if (const ACWBaseTool* FocusedTool = ToolHolderComponent->GetFocusedTool())
	{
		const float ToolDistanceSquared = FVector::DistSquared(GetActorLocation(), FocusedTool->GetActorLocation());
		if (ToolDistanceSquared <= InteractableDistanceSquared)
		{
			ToolHolderComponent->Interact();
			return;
		}
	}

	PerformInteractWith(Interactable);
}

AActor* ACWPlayerCharacter::FindNearestInteractable(float& OutDistanceSquared) const
{
	OutDistanceSquared = TNumericLimits<float>::Max();

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	// 레벨 전체를 훑지 않고 반경 안만 본다. 상호작용 키를 누른 순간에만 도는 코드지만,
	// 액터 순회는 맵이 커질수록 그대로 비용이 된다.
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		GetActorLocation(),
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeSphere(InteractRadius),
		QueryParams);

	AActor* NearestActor = nullptr;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* OverlapActor = Overlap.GetActor();
		if (OverlapActor == nullptr || !OverlapActor->Implements<UInterface_Interactable>())
		{
			continue;
		}

		// 도구는 도구 소지 컴포넌트가 따로 관리한다. 위 함수 주석 참고.
		if (OverlapActor->IsA<ACWBaseTool>())
		{
			continue;
		}

		if (!IInterface_Interactable::Execute_CanInteract(OverlapActor, const_cast<ACWPlayerCharacter*>(this)))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(GetActorLocation(), OverlapActor->GetActorLocation());
		if (DistanceSquared >= OutDistanceSquared)
		{
			continue;
		}

		OutDistanceSquared = DistanceSquared;
		NearestActor = OverlapActor;
	}

	return NearestActor;
}

void ACWPlayerCharacter::PerformInteractWith(AActor* Target)
{
	if (Target == nullptr)
	{
		return;
	}

	// 판정은 서버에서만 한다. 클라이언트는 요청만 보내고 서버가 CanInteract 로 다시 본다.
	if (!HasAuthority())
	{
		Server_InteractWith(Target);
		return;
	}

	if (IInterface_Interactable::Execute_CanInteract(Target, this))
	{
		IInterface_Interactable::Execute_Interact(Target, this);
	}
}

void ACWPlayerCharacter::Server_InteractWith_Implementation(AActor* Target)
{
	if (Target == nullptr || !Target->Implements<UInterface_Interactable>())
	{
		return;
	}

	PerformInteractWith(Target);
}

void ACWPlayerCharacter::PickUpCan()
{
	SupplyCarrierComponent->PickUpCan();
}

void ACWPlayerCharacter::DropCan()
{
	SupplyCarrierComponent->DropCan();
}

void ACWPlayerCharacter::ChargePaint()
{
	if (IsActionLocked())
	{
		return;
	}

	// 아스팔트 건을 들고 있으면 같은 키가 수동 재장전이 된다. (싱크홀 기획서 7-5)
	//
	// R 은 "든 도구를 채운다" 는 자리다. 아스팔트 건은 페인트를 쓰지 않으므로
	// 이 키가 놀고 있고, 채운다는 의미도 그대로 이어진다.
	if (AsphaltGunnerComponent->GetHeldAsphaltGun() != nullptr)
	{
		AsphaltGunnerComponent->Reload();
		return;
	}

	SupplyCarrierComponent->ChargeNearestTool();
}

void ACWPlayerCharacter::CancelWork()
{
	WorkerComponent->CancelWork();
}

void ACWPlayerCharacter::StartWork()
{
	if (IsActionLocked())
	{
		return;
	}

	// 아스팔트 건을 들고 있으면 같은 키가 사격이 된다. (싱크홀 기획서 7-2)
	//
	// 별도 키를 두지 않는 이유: 좌클릭은 이미 "지금 든 도구를 쓴다" 는 자리다.
	// 아스팔트 건은 작업 도구가 아니라 어차피 존 게이지를 올리지도 못하므로,
	// 키를 나누면 손만 복잡해지고 얻는 게 없다.
	if (AsphaltGunnerComponent->GetHeldAsphaltGun() != nullptr)
	{
		AsphaltGunnerComponent->Fire();
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
	if (IsActionLocked())
	{
		return;
	}

	// 테두리 미니게임처럼 입력을 붙잡는 작업 중에는 패링이 막힌다.
	// 그 상태에서 패링까지 되면 못 피하는 위험이 상쇄되어 미니게임의 긴장이 사라진다.
	if (WorkerComponent->IsParryBlocked())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[%s] 작업 중이라 패링 입력을 무시합니다."), *GetName());
		return;
	}

	// 페인트 통을 들고 있으면 패링할 수 없다. (기획서 6-5)
	//
	// 통 소지의 실패 비용을 이 제한과 오염 확산량 둘이 떠받친다. 이동속도 페널티를 폐기했으므로
	// 여기까지 열어주면 통을 많이 드는 선택에 아무 대가가 없어진다.
	// 보급자는 차를 피하거나 팀원에게 패링을 부탁해야 한다.
	if (SupplyCarrierComponent->IsCarrying())
	{
		SupplyCarrierComponent->NotifyActionRejected(ECWSupplyRejectReason::CansInHand);
		UE_LOG(LogTemp, Verbose, TEXT("[%s] 페인트 통을 들고 있어 패링 입력을 무시합니다."), *GetName());
		return;
	}

	const FVector CursorWorldLocation = ResolveCursorWorldLocation();

	// 이 줄이 안 찍히면 입력이 여기까지 오지 않은 것이고,
	// 커서가 캐릭터 위치와 같으면 커서 트레이스가 지형을 못 맞힌 것이다.
	UE_LOG(LogTemp, Log, TEXT("[%s] 패링 입력 수신. 커서=%s 캐릭터=%s"),
		*GetName(), *CursorWorldLocation.ToCompactString(), *GetActorLocation().ToCompactString());

	ParryComponent->RequestParry(CursorWorldLocation);
}

void ACWPlayerCharacter::SwapHelmet()
{
	// 머리 슬롯만 순환한다. CWWearableSlots 에 슬롯 이름 상수를 둔 이유가 이 호출이다.
	WearableComponent->CycleSlotItem(CWWearableSlots::Head);
}

void ACWPlayerCharacter::ToggleOutfit()
{
	// 어느 슬롯이 함께 벗겨지는지는 카탈로그의 bIncludeInToggleAll 이 정한다.
	WearableComponent->ToggleAllSlots();
}

void ACWPlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ApplyCursorFacing(DeltaSeconds);
}

void ACWPlayerCharacter::ApplyCursorFacing(float DeltaSeconds)
{
	if (!bFaceCursor)
	{
		return;
	}

	/**
	 * 회전을 넣는 것은 조종 중인 본인 기계뿐이다.
	 *
	 * 남의 캐릭터 회전은 캐릭터 무브먼트의 리플리케이션으로 내려온다. 서버가 자기 판단으로
	 * 또 돌리면 클라이언트가 보내온 회전과 싸워서 남의 화면에서만 몸이 떨린다.
	 * 애초에 서버와 다른 클라이언트는 이 플레이어의 마우스 커서를 읽을 수 없다.
	 */
	if (!IsLocallyControlled())
	{
		return;
	}

	/**
	 * 쓰러져 있는 동안은 돌리지 않는다.
	 *
	 * 래그돌이 물리로 자세를 잡고 있고, 기상할 때는 복귀 정렬이 캡슐 회전을 직접 맞춘다.
	 * (HandleHitLockChanged 경로의 SetActorRotation) 여기서 매 프레임 덮으면 그 정렬과 싸운다.
	 */
	if (IsActionLocked())
	{
		return;
	}

	FVector ToCursor = ResolveCursorWorldLocation() - GetActorLocation();
	ToCursor.Z = 0.f;

	// 커서가 발밑이거나, 트레이스가 빗나가 ResolveCursorWorldLocation 이 캐릭터 위치를
	// 그대로 낸 경우다. 방향이 거의 0 이라 각도가 매 프레임 튀므로 마지막 방향을 유지한다.
	if (ToCursor.SizeSquared() < FMath::Square(MinCursorFacingDistance))
	{
		return;
	}

	const FRotator CurrentYaw(0.f, GetActorRotation().Yaw, 0.f);
	const FRotator TargetYaw(0.f, ToCursor.Rotation().Yaw, 0.f);

	// RInterpTo 는 각도 차를 정규화해서 보간하므로 ±180도 경계에서 한 바퀴 돌지 않는다.
	const FRotator NewYaw = (CursorFacingInterpSpeed <= 0.f)
		? TargetYaw
		: FMath::RInterpTo(CurrentYaw, TargetYaw, DeltaSeconds, CursorFacingInterpSpeed);

	SetActorRotation(FRotator(0.f, NewYaw.Yaw, 0.f));
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
