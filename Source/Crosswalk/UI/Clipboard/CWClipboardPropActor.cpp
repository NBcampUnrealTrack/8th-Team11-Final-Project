// CWClipboardPropActor.cpp

#include "UI/Clipboard/CWClipboardPropActor.h"

#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "UI/Clipboard/CWClipboardWidgetInteractionComponent.h"
#include "Blueprint/UserWidget.h"
#include "Curves/CurveFloat.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY(LogCWClipboard);

#pragma region Constructor

ACWClipboardPropActor::ACWClipboardPropActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// 카메라 갱신이 끝난 뒤에 위치를 잡아야 스프링암 지연과 어긋나지 않음
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	bReplicates = false;
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);

	PropRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PropRoot"));
	SetRootComponent(PropRoot);

	ClipboardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ClipboardMesh"));
	ClipboardMesh->SetupAttachment(PropRoot);
	ClipboardMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ClipboardMesh->bReceivesDecals = false;

	// 실제 메시와 크기·회전은 BP 가 지정함 (BP_ClipboardProp)
	// C++ 은 아트 애셋 경로를 알지 않음. 아트가 폴더를 옮겨도 코드가 깨지지 않음
	// 반대로 C++ 이 경로를 들고 있으면 BP 가 같은 값으로 소유권을 가져올 수 없음
	// (프로퍼티 델타가 안 생겨 오버라이드로 기록되지 않음)
	//
	// 플레이스홀더로 엔진 큐브를 22 x 30 x 2 cm 판때기로 설정함
	// 실행 중에 이 큐브가 보이면 BP 배선이 빠졌다는 신호임
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaceholderMeshFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube")
	);
	if (PlaceholderMeshFinder.Succeeded())
	{
		ClipboardMesh->SetStaticMesh(PlaceholderMeshFinder.Object);
	}
	ClipboardMesh->SetRelativeScale3D(FVector(0.22f, 0.30f, 0.02f));

	HandMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandMesh"));
	HandMesh->SetupAttachment(PropRoot);
	HandMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HandMesh->bReceivesDecals = false;

	PropLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("PropLight"));
	PropLight->SetupAttachment(PropRoot);

	// 판 앞면(+Z) 을 정면에서 비춤. 각도는 정식 메시가 오면 다시 잡을 것
	PropLight->SetRelativeLocation(FVector(0.f, 0.f, 35.f));
	PropLight->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
	PropLight->Intensity = 12000.f;
	PropLight->AttenuationRadius = 180.f;
	PropLight->OuterConeAngle = 50.f;
	PropLight->InnerConeAngle = 20.f;
	PropLight->CastShadows = false;

	// 루트 기준 +Z가 종이 앞면이며 +X가 위쪽임
	PaperWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("PaperWidget"));
	PaperWidget->SetupAttachment(PropRoot);
	PaperWidget->SetRelativeLocation(FVector(8.f, 0.f, 1.2f));
	PaperWidget->SetRelativeRotation(FRotator(90.f, 180.f, 0.f));
	PaperWidget->SetRelativeScale3D(FVector(0.06f));
	PaperWidget->SetWidgetSpace(EWidgetSpace::World);
	PaperWidget->SetDrawSize(FVector2D(800.f, 1040.f));
	PaperWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PaperWidget->SetGenerateOverlapEvents(false);
	PaperWidget->SetTickWhenOffscreen(true);
	PaperWidget->SetCastShadow(false);
	PaperWidget->SetVisibility(false);

	PaperInteraction = CreateDefaultSubobject<UCWClipboardWidgetInteractionComponent>(TEXT("PaperInteraction"));
	PaperInteraction->SetupAttachment(PropRoot);
	PaperInteraction->InteractionSource = EWidgetInteractionSource::Custom;
	PaperInteraction->PrimaryComponentTick.bStartWithTickEnabled = false;
}

#pragma endregion

#pragma region ActorOverrides

void ACWClipboardPropActor::BeginPlay()
{
	Super::BeginPlay();

	ApplyLightingChannels();
}

void ACWClipboardPropActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (ClipboardState == ECWClipboardState::Hidden)
	{
		SetActorTickEnabled(false);
		return;
	}

	const bool bIsMoving =
		ClipboardState == ECWClipboardState::Raising ||
		ClipboardState == ECWClipboardState::Lowering;

	if (bIsMoving)
	{
		MotionElapsed += DeltaSeconds;
	}

	SwayElapsed += DeltaSeconds;

	UpdateTransformFromCamera();
	UpdatePaperPointer();

	if (bIsMoving && MotionElapsed >= MotionDuration + RotationLagSeconds)
	{
		FinishMotion();
	}
}

#pragma endregion

#pragma region Setup

void ACWClipboardPropActor::SetOwningPlayerController(APlayerController* InPlayerController)
{
	OwningPlayerController = InPlayerController;
}

void ACWClipboardPropActor::SetPaperContent(UUserWidget* InOwner, UWidget* InContent)
{
	ReleasePaperPointers();
	PaperOwner = InOwner;
	PaperWidget->SetSlateWidget(InContent->TakeWidget());
	PaperWidget->SetVisibility(true);
	PaperWidget->RequestRedraw();
}

void ACWClipboardPropActor::ClearPaperContent(const UUserWidget* InOwner)
{
	if (!IsPaperOwner(InOwner))
	{
		return;
	}

	SetPaperInteractionEnabled(false);
	PaperWidget->SetSlateWidget(nullptr);
	PaperWidget->SetVisibility(false);
	PaperOwner.Reset();
}

bool ACWClipboardPropActor::IsPaperOwner(const UUserWidget* InOwner) const
{
	return InOwner && PaperOwner.Get() == InOwner;
}

void ACWClipboardPropActor::SetPaperInteractionEnabled(bool bEnabled)
{
	bPaperInteractionEnabled = bEnabled;
	if (!bEnabled)
	{
		// 닫는 중에는 눌린 버튼을 놓아도 클릭으로 처리하지 않음
		PaperInteraction->UpdatePointer(FHitResult());
		ReleasePaperPointers();
	}
}

bool ACWClipboardPropActor::PressPaperPointer(FKey InKey)
{
	if (!bPaperInteractionEnabled || ClipboardState != ECWClipboardState::Raised)
	{
		return false;
	}

	UpdatePaperPointer();
	if (!PaperInteraction->IsOverHitTestVisibleWidget())
	{
		return false;
	}

	PressedPaperKeys.Add(InKey);
	PaperInteraction->PressPointerKey(InKey);
	return true;
}

void ACWClipboardPropActor::ReleasePaperPointer(FKey InKey)
{
	if (PressedPaperKeys.Remove(InKey) > 0)
	{
		UpdatePaperPointer();
		PaperInteraction->ReleasePointerKey(InKey);
	}
}

void ACWClipboardPropActor::ReleasePaperPointers()
{
	// Release가 닫기 이벤트를 재진입시켜도 같은 키를 두 번 해제하지 않음
	const TSet<FKey> KeysToRelease = MoveTemp(PressedPaperKeys);
	for (const FKey& Key : KeysToRelease)
	{
		PaperInteraction->ReleasePointerKey(Key);
	}
}

void ACWClipboardPropActor::ScrollPaper(float InDelta)
{
	if (bPaperInteractionEnabled && ClipboardState == ECWClipboardState::Raised)
	{
		UpdatePaperPointer();
		PaperInteraction->ScrollWheel(InDelta);
	}
}

void ACWClipboardPropActor::UpdatePaperPointer()
{
	FHitResult Hit;
	APlayerController* PlayerController = OwningPlayerController.Get();
	if (bPaperInteractionEnabled && PaperOwner.IsValid() && IsValid(PlayerController) &&
		ClipboardState == ECWClipboardState::Raised)
	{
		FVector RayOrigin;
		FVector RayDirection;
		if (PlayerController->DeprojectMousePositionToWorld(RayOrigin, RayDirection))
		{
			const FVector Normal = PaperWidget->GetForwardVector();
			const double Denominator = FVector::DotProduct(RayDirection, Normal);
			if (!FMath::IsNearlyZero(Denominator))
			{
				const double Distance = FVector::DotProduct(
					PaperWidget->GetComponentLocation() - RayOrigin, Normal) / Denominator;
				const FVector HitLocation = RayOrigin + RayDirection * Distance;
				FVector2D LocalHit;
				PaperWidget->GetLocalHitLocation(HitLocation, LocalHit);
				const FVector2D DrawSize = PaperWidget->GetDrawSize();
				if (Distance >= 0.0 && LocalHit.X >= 0.0 && LocalHit.Y >= 0.0 &&
					LocalHit.X <= DrawSize.X && LocalHit.Y <= DrawSize.Y)
				{
					Hit.bBlockingHit = true;
					Hit.Component = PaperWidget;
					Hit.Location = Hit.ImpactPoint = HitLocation;
					Hit.TraceStart = RayOrigin;
					Hit.TraceEnd = HitLocation;
				}
			}
		}
	}

	PaperInteraction->UpdatePointer(Hit);
}

#pragma endregion

#pragma region Motion

void ACWClipboardPropActor::StartRaise()
{
	if (ClipboardState == ECWClipboardState::Raised ||
		ClipboardState == ECWClipboardState::Raising)
	{
		return;
	}

	// 내려가는 중에 다시 올리면 남은 진행도를 이어받아 튀지 않게 함
	const float TotalDuration = MotionDuration + RotationLagSeconds;
	MotionElapsed = (ClipboardState == ECWClipboardState::Lowering)
		? FMath::Max(0.f, TotalDuration - MotionElapsed)
		: 0.f;

	ClipboardState = ECWClipboardState::Raising;

	SetActorHiddenInGame(false);
	SetActorTickEnabled(true);

	// 첫 프레임부터 제자리에 있도록 미리 한 번 설정함
	UpdateTransformFromCamera();
}

void ACWClipboardPropActor::StartLower()
{
	if (ClipboardState == ECWClipboardState::Hidden ||
		ClipboardState == ECWClipboardState::Lowering)
	{
		return;
	}

	const float TotalDuration = MotionDuration + RotationLagSeconds;
	MotionElapsed = (ClipboardState == ECWClipboardState::Raising)
		? FMath::Max(0.f, TotalDuration - MotionElapsed)
		: 0.f;

	ClipboardState = ECWClipboardState::Lowering;

	SetActorTickEnabled(true);
}

#pragma endregion

#pragma region PrivateFunctions

float ACWClipboardPropActor::ComputeAlpha(float InOffsetSeconds) const
{
	if (MotionDuration <= KINDA_SMALL_NUMBER)
	{
		return 1.f;
	}

	return FMath::Clamp((MotionElapsed - InOffsetSeconds) / MotionDuration, 0.f, 1.f);
}

float ACWClipboardPropActor::EvaluateBlend(float InAlpha) const
{
	if (MotionCurve)
	{
		return MotionCurve->GetFloatValue(InAlpha);
	}

	// 커브가 없을 때의 기본 감각: 끝에서 살짝 지나쳤다가 되돌아와 멎음 (easeOutBack)
	// 등속으로 딱 멈추면 로봇처럼 보임
	const float Overshoot = 1.70158f;
	const float T = InAlpha - 1.f;

	return 1.f + (Overshoot + 1.f) * T * T * T + Overshoot * T * T;
}

void ACWClipboardPropActor::UpdateTransformFromCamera()
{
	APlayerController* PlayerController = OwningPlayerController.Get();
	if (!IsValid(PlayerController))
	{
		return;
	}

	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

	// 회전은 위치보다 RotationLagSeconds 만큼 늦게 따라옴
	const float LocationBlend = EvaluateBlend(ComputeAlpha(0.f));
	const float RotationBlend = EvaluateBlend(ComputeAlpha(RotationLagSeconds));

	const bool bTowardRaised = (ClipboardState != ECWClipboardState::Lowering);

	FVector RelativeLocation = FVector::ZeroVector;
	FRotator RelativeRotation = FRotator::ZeroRotator;

	if (bTowardRaised)
	{
		RelativeLocation = FMath::Lerp(HiddenRelativeLocation, RaisedRelativeLocation, LocationBlend);
		RelativeRotation = FMath::Lerp(HiddenRelativeRotation, RaisedRelativeRotation, RotationBlend);
	}
	else
	{
		RelativeLocation = FMath::Lerp(RaisedRelativeLocation, HiddenRelativeLocation, LocationBlend);
		RelativeRotation = FMath::Lerp(RaisedRelativeRotation, HiddenRelativeRotation, RotationBlend);
	}

	// 흔들림은 다 올라온 정도에 비례해 섞음. 숨김 상태에서는 0
	const float RaisedWeight = FMath::Clamp(
		bTowardRaised ? LocationBlend : 1.f - LocationBlend,
		0.f,
		1.f
	);

	if (SwayAmplitudeDegrees > KINDA_SMALL_NUMBER && RaisedWeight > KINDA_SMALL_NUMBER)
	{
		const float SwayPhase = SwayElapsed * SwayFrequency * 2.f * PI;

		// Pitch와 Roll의 주기를 다르게 설정해 진자처럼 보이지 않게 함
		RelativeRotation.Pitch +=
			FMath::Sin(SwayPhase) * SwayAmplitudeDegrees * RaisedWeight;
		RelativeRotation.Roll +=
			FMath::Sin(SwayPhase * 0.73f + 1.1f) * SwayAmplitudeDegrees * 0.6f * RaisedWeight;
	}

	const FTransform CameraTransform(CameraRotation, CameraLocation);
	const FTransform RelativeTransform(RelativeRotation, RelativeLocation);

	SetActorTransform(RelativeTransform * CameraTransform);
}

void ACWClipboardPropActor::ApplyLightingChannels()
{
	// 격리 시 프롭은 채널 1 에만 존재함
	// 월드 라이트(채널 0)와 프롭 라이트가 서로의 대상에 영향을 주지 않음
	const bool bChannel0 = !bIsolateLighting;

	if (ClipboardMesh)
	{
		ClipboardMesh->SetLightingChannels(bChannel0, true, false);
	}

	if (HandMesh)
	{
		HandMesh->SetLightingChannels(bChannel0, true, false);
	}

	if (PropLight)
	{
		PropLight->SetLightingChannels(false, true, false);
	}
}

void ACWClipboardPropActor::FinishMotion()
{
	MotionElapsed = MotionDuration + RotationLagSeconds;

	if (ClipboardState == ECWClipboardState::Raising)
	{
		ClipboardState = ECWClipboardState::Raised;

		UE_LOG(LogCWClipboard, Verbose, TEXT("[Clipboard] Raise 완료"));

		OnRaiseFinished.Broadcast();
		return;
	}

	if (ClipboardState == ECWClipboardState::Lowering)
	{
		ClipboardState = ECWClipboardState::Hidden;

		SetActorHiddenInGame(true);
		SetActorTickEnabled(false);

		UE_LOG(LogCWClipboard, Verbose, TEXT("[Clipboard] Lower 완료"));

		OnLowerFinished.Broadcast();
	}
}

#pragma endregion
