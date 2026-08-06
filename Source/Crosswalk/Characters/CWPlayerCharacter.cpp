#include "Characters/CWPlayerCharacter.h"

#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
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

	Multicast_ApplyHitKnockback(LaunchVelocity);
}

void ACWPlayerCharacter::Multicast_ApplyHitKnockback_Implementation(FVector_NetQuantize LaunchVelocity)
{
	// 두 인자를 모두 true 로 줘야 기존 속도를 덮어쓰고 확실히 날아간다.
	LaunchCharacter(LaunchVelocity, true, true);
}

void ACWPlayerCharacter::HandleHitLockChanged(UCWVehicleHitReactionComponent* /*HitReactionComp*/, bool bHitLocked)
{
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
	}
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
