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
#include "Painting/CWWorkerComponent.h"
#include "Painting/Tools/CWBaseTool.h"
#include "Painting/Tools/CWToolHolderComponent.h"
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

	EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ACWPlayerCharacter::Move);
	EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &ACWPlayerCharacter::Interact);

	// 누르고 있는 동안 게이지가 오르는 방식이라 시작과 끝을 따로 잡는다.
	// Canceled 를 빼먹으면 키를 떼도 작업이 계속되는 버그가 생긴다.
	EnhancedInput->BindAction(WorkAction, ETriggerEvent::Started, this, &ACWPlayerCharacter::StartWork);
	EnhancedInput->BindAction(WorkAction, ETriggerEvent::Completed, this, &ACWPlayerCharacter::StopWork);
	EnhancedInput->BindAction(WorkAction, ETriggerEvent::Canceled, this, &ACWPlayerCharacter::StopWork);
}

void ACWPlayerCharacter::BuildBuiltInInput()
{
	// 애셋이 이미 지정돼 있으면 그쪽을 그대로 쓴다.
	if (DefaultMappingContext != nullptr)
	{
		UE_LOG(LogTemp, Log, TEXT("[%s] 지정된 매핑 컨텍스트 애셋 '%s' 를 사용합니다."),
			*GetName(), *DefaultMappingContext.GetName());
		return;
	}

	MoveAction = NewObject<UInputAction>(this, TEXT("BuiltIn_Move"));
	MoveAction->ValueType = EInputActionValueType::Axis2D;

	InteractAction = NewObject<UInputAction>(this, TEXT("BuiltIn_Interact"));
	InteractAction->ValueType = EInputActionValueType::Boolean;

	WorkAction = NewObject<UInputAction>(this, TEXT("BuiltIn_Work"));
	WorkAction->ValueType = EInputActionValueType::Boolean;

	UInputMappingContext* Context = NewObject<UInputMappingContext>(this, TEXT("BuiltIn_MappingContext"));

	// Axis2D 에서 X = 좌우, Y = 앞뒤.
	// 키 하나는 값 1.0 을 X 축에만 넣으므로, 앞뒤 키는 Swizzle 로 값을 Y 축으로 옮겨야 한다.
	AddMoveKeyMapping(Context, EKeys::W, true, false);
	AddMoveKeyMapping(Context, EKeys::S, true, true);
	AddMoveKeyMapping(Context, EKeys::D, false, false);
	AddMoveKeyMapping(Context, EKeys::A, false, true);

	Context->MapKey(InteractAction, EKeys::E);

	// 기획서 3.1 에 작업 키가 좌클릭 / Space 두 가지로 적혀 있어 둘 다 넣는다.
	Context->MapKey(WorkAction, EKeys::SpaceBar);
	Context->MapKey(WorkAction, EKeys::LeftMouseButton);

	DefaultMappingContext = Context;
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
	if (DefaultMappingContext == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] 매핑 컨텍스트가 없습니다."), *GetName());
		return;
	}

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

	// 템플릿에서 딸려온 컨텍스트가 남아 조작이 섞이는 걸 막는다.
	// 나중에 UI 전용 매핑 컨텍스트를 쓰게 되면, 그것까지 지워버리므로 이 줄을 걷어내야 한다.
	InputSubsystem->ClearAllMappings();
	InputSubsystem->AddMappingContext(DefaultMappingContext, 0);

	UE_LOG(LogTemp, Log, TEXT("[%s] 매핑 컨텍스트 '%s' 등록 완료. 매핑 %d개"),
		*GetName(), *DefaultMappingContext.GetName(), DefaultMappingContext->GetMappings().Num());

	// 어떤 키가 어떤 액션에 걸려 있는지 그대로 찍는다. 애셋을 눈으로 보는 것보다 확실하다.
	for (const FEnhancedActionKeyMapping& Mapping : DefaultMappingContext->GetMappings())
	{
		const FString ActionName = (Mapping.Action != nullptr) ? Mapping.Action.GetName() : TEXT("None");

		UE_LOG(LogTemp, Log, TEXT("    %s <- %s (Triggers=%d, Modifiers=%d)"),
			*ActionName, *Mapping.Key.ToString(), Mapping.Triggers.Num(), Mapping.Modifiers.Num());
	}
}

void ACWPlayerCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MoveInput = Value.Get<FVector2D>();

	// 첫 입력만 한 번 찍는다. 이 줄이 안 보이면 입력이 여기까지 오지 않는다는 뜻이고,
	// (0.00, 0.00) 으로 찍히면 Swizzle/Negate 설정이 잘못된 것이다.
	if (!bLoggedFirstMoveInput)
	{
		bLoggedFirstMoveInput = true;
		UE_LOG(LogTemp, Log, TEXT("[%s] 첫 Move 입력 수신: X=%.2f Y=%.2f"),
			*GetName(), MoveInput.X, MoveInput.Y);
	}

	if (MoveInput.IsNearlyZero())
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
	UE_LOG(LogTemp, Log, TEXT("[%s] Interact 입력 수신. 대상 도구=%s"),
		*GetName(),
		ToolHolderComponent->GetFocusedTool() != nullptr
			? *ToolHolderComponent->GetFocusedTool()->GetName()
			: TEXT("없음"));

	ToolHolderComponent->Interact();
}

void ACWPlayerCharacter::StartWork()
{
	UE_LOG(LogTemp, Log, TEXT("[%s] Work 입력 수신"), *GetName());

	WorkerComponent->StartWork();
}

void ACWPlayerCharacter::StopWork()
{
	WorkerComponent->StopWork();
}
