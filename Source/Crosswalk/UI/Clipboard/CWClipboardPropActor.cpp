// CWClipboardPropActor.cpp

#include "UI/Clipboard/CWClipboardPropActor.h"
#include "Core/CWGameUserSettings.h"

#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "UI/Clipboard/CWClipboardWidgetInteractionComponent.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Curves/CurveFloat.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Components/Border.h"
#include "Components/Widget.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Slate/SRetainerWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Widgets/SVirtualWindow.h"
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

void ACWClipboardPropActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetHoldFullWorldResolution(false);
	Super::EndPlay(EndPlayReason);
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
	UpdatePaperCrossfade(DeltaSeconds);
	UpdatePaperPointer();

	if (bIsMoving && MotionElapsed >= GetActiveMotionDuration() + RotationLagSeconds)
	{
		FinishMotion();
	}
	else if (ClipboardState == ECWClipboardState::Lowering && !bLowerNotified &&
		(IsPropOffScreen() || MotionElapsed >= MotionDuration + RotationLagSeconds))
	{
		// 다음 화면은 내리기 완료를 기다려 열리므로 판이 화면 밖으로 나간 순간 미리 알림
		// 출발이 빠른 곡선이라 판은 도착보다 훨씬 먼저 사라짐. 끝까지 기다리면 빈 화면이 남음
		// 원래 숨김 포즈에 닿던 시각은 판이 화면에 걸려 있어도 늦어도 여는 상한
		bLowerNotified = true;
		OnLowerFinished.Broadcast();
	}
}

#pragma endregion

#pragma region Setup

void ACWClipboardPropActor::SetOwningPlayerController(APlayerController* InPlayerController)
{
	OwningPlayerController = InPlayerController;
}

void ACWClipboardPropActor::SetPresentationScale(float InScale, float InHeightRatio)
{
	if (OriginalPaperDrawSize.IsNearlyZero())
	{
		OriginalPaperDrawSize = PaperWidget->GetDrawSize();
		OriginalClipboardTransform = ClipboardMesh->GetRelativeTransform();
		OriginalPaperLocation = PaperWidget->GetRelativeLocation();
		OriginalHandLocation = HandMesh->GetRelativeLocation();
	}
	PresentationScale = FMath::Clamp(InScale, 0.5f, 2.f);
	const float HeightRatio = FMath::Clamp(InHeightRatio, 0.6f, 1.2f);
	const FVector PaperUp = PaperWidget->GetRelativeRotation().RotateVector(FVector::UpVector);
	const auto Compress = [&PaperUp, HeightRatio](const FVector& Value)
	{
		return Value + PaperUp * FVector::DotProduct(Value, PaperUp) * (HeightRatio - 1.f);
	};
	// 회전된 자식에 루트 비균일 스케일을 주면 판과 종이의 축이 달라져 개별 크기를 조절함
	const FQuat Rotation = OriginalClipboardTransform.GetRotation();
	const FVector MeshScale(
		Compress(Rotation.GetAxisX()).Size(),
		Compress(Rotation.GetAxisY()).Size(),
		Compress(Rotation.GetAxisZ()).Size());
	ClipboardMesh->SetRelativeScale3D(OriginalClipboardTransform.GetScale3D() * MeshScale);
	ClipboardMesh->SetRelativeLocation(Compress(OriginalClipboardTransform.GetLocation()));
	PaperWidget->SetRelativeLocation(Compress(OriginalPaperLocation));
	HandMesh->SetRelativeLocation(Compress(OriginalHandLocation));
	// 종이는 픽셀 밀도를 유지하고 표시 영역만 줄여 글자 눌림을 방지함
	PaperWidget->SetDrawSize(FVector2D(OriginalPaperDrawSize.X,
		FMath::RoundToInt(OriginalPaperDrawSize.Y * HeightRatio)));
	PaperWidget->RequestRedraw();
}

void ACWClipboardPropActor::SetPaperOnlyPresentation(bool bEnabled)
{
	bPaperOnlyPresentation = bEnabled;
	HandMesh->SetVisibility(!bEnabled);
	if (ClipboardState != ECWClipboardState::Hidden)
	{
		UpdateTransformFromCamera();
	}
}

void ACWClipboardPropActor::SetPaperContent(UUserWidget* InOwner, UWidget* InContent)
{
	// 전환 도중 강제로 바뀌면 겹침 패널부터 걷어야 두 페이지가 반투명하게 남지 않음
	EndPaperWarmup();
	EndPaperCrossfade();
	ReleasePaperPointers();
	PaperOwner = InOwner;
	PaperContent = InContent;
	PaperContentOpacity = InContent->GetRenderOpacity();
	PaperWidget->SetSlateWidget(InContent->TakeWidget());
	PaperWidget->SetVisibility(true);
	PaperWidget->RequestRedraw();

	// Slate 내용만 옮긴 경우 엔진이 부모 창을 연결하지 않아 한글 조합 창이 누락됨
	if (InOwner->GetOwningLocalPlayer() && InOwner->GetOwningLocalPlayer()->ViewportClient)
	{
		if (const TSharedPtr<SWindow> Window = PaperWidget->GetSlateWindow())
		{
			Window->AssignParentWidget(InOwner->GetOwningLocalPlayer()->ViewportClient->GetGameViewportWidget());
		}
	}
}

void ACWClipboardPropActor::ClearPaperContent(const UUserWidget* InOwner)
{
	if (!IsPaperOwner(InOwner))
	{
		return;
	}

	SetPaperInteractionEnabled(false);
	EndPaperWarmup();
	EndPaperCrossfade();
	PaperWidget->SetSlateWidget(nullptr);
	PaperWidget->SetVisibility(false);
	PaperOwner.Reset();
	PaperContent.Reset();
}

void ACWClipboardPropActor::CrossfadePaperContent(UUserWidget* InOwner, UWidget* InContent)
{
	// 진행 중인 전환이 있으면 끝난 상태로 만든 뒤 그 결과를 이전 종이로 삼음
	// 미리 그리기 레이어가 씌워져 있으면 그 패널이 이전 종이로 잡히므로 먼저 걷음
	EndPaperWarmup();
	EndPaperCrossfade();
	UWidget* Previous = PaperContent.Get();
	const TSharedPtr<SWidget> PreviousSlate = PaperWidget->GetSlateWidget();
	if (!Previous || !PreviousSlate.IsValid() || Previous == InContent ||
		ClipboardState != ECWClipboardState::Raised)
	{
		SetPaperContent(InOwner, InContent);
		OnPaperCrossfadeFinished.Broadcast();
		return;
	}

	ReleasePaperPointers();
	FadingOutContent = Previous;
	FadingOutOpacity = PaperContentOpacity;
	PaperOwner = InOwner;
	PaperContent = InContent;
	PaperContentOpacity = InContent->GetRenderOpacity();
	const TSharedRef<SWidget> IncomingSlate = InContent->TakeWidget();

	// 위젯 컴포넌트는 내용을 하나만 그리므로 전환 동안만 두 페이지를 한 패널에 겹침
	// 이전 종이를 창에서 먼저 떼어야 겹침 패널로 옮길 때 부모가 둘이 되지 않음
	PaperWidget->SetSlateWidget(nullptr);
	const TSharedRef<SOverlay> PaperLayers = SNew(SOverlay);
	if (PaperInkWipeMaterial)
	{
		// 페이지마다 종이 배경이 들어 있어 통째로 지우면 판이 드러남
		// 바닥에 같은 종이를 깔아 두면 지워진 자리에도 종이가 남아 글씨만 바뀌어 보임
		if (const UBorder* PaperBorder = Cast<UBorder>(Previous))
		{
			PaperFadeBrush = PaperBorder->Background;
			PaperLayers->AddSlot()[SNew(SImage).Image(&PaperFadeBrush).ColorAndOpacity(PaperBorder->GetBrushColor())];
		}
		WriteInkLayer = MakeInkLayer(IncomingSlate, true);
		EraseInkLayer = MakeInkLayer(PreviousSlate.ToSharedRef(), false);
		PaperLayers->AddSlot()[WriteInkLayer.ToSharedRef()];
		PaperLayers->AddSlot()[EraseInkLayer.ToSharedRef()];
	}
	else
	{
		InContent->SetRenderOpacity(0.f);
		PaperLayers->AddSlot()[PreviousSlate.ToSharedRef()];
		PaperLayers->AddSlot()[IncomingSlate];
	}
	PaperWidget->SetSlateWidget(PaperLayers);
	PaperWidget->RequestRenderUpdate();

	bPaperCrossfading = true;
	PaperFadeElapsed = 0.f;
	UpdatePaperCrossfade(0.f);
}

void ACWClipboardPropActor::UpdatePaperCrossfade(float DeltaSeconds)
{
	if (!bPaperCrossfading)
	{
		return;
	}

	PaperFadeElapsed += DeltaSeconds;
	const float EraseDuration = FMath::Max(PaperEraseDuration, 0.05f);
	const float DrawDelay = FMath::Max(PaperDrawDelay, 0.f);
	const float DrawDuration = FMath::Max(PaperDrawDuration, 0.05f);
	// 쓰기가 지우기보다 먼저 끝나도 지우던 글씨가 남지 않도록 늦게 끝나는 쪽까지 진행함
	const float Duration = FMath::Max(EraseDuration, DrawDelay + DrawDuration);
	// 구간마다 완만하게 시작하고 멈춰야 두 페이지가 겹치는 순간이 뚝 끊겨 보이지 않음
	const float EraseAlpha = FMath::SmoothStep(0.f, 1.f, PaperFadeElapsed / EraseDuration);
	const float DrawAlpha = FMath::SmoothStep(0.f, 1.f, (PaperFadeElapsed - DrawDelay) / DrawDuration);
	if (EraseInkLayer.IsValid() && WriteInkLayer.IsValid())
	{
		if (UMaterialInstanceDynamic* EraseMask = EraseInkLayer->GetEffectMaterial())
		{
			EraseMask->SetScalarParameterValue(TEXT("Progress"), EraseAlpha);
		}
		if (UMaterialInstanceDynamic* WriteMask = WriteInkLayer->GetEffectMaterial())
		{
			WriteMask->SetScalarParameterValue(TEXT("Progress"), DrawAlpha);
		}
	}
	else
	{
		if (UWidget* Outgoing = FadingOutContent.Get())
		{
			Outgoing->SetRenderOpacity(FadingOutOpacity * (1.f - EraseAlpha));
		}
		if (UWidget* Incoming = PaperContent.Get())
		{
			Incoming->SetRenderOpacity(PaperContentOpacity * DrawAlpha);
		}
	}
	PaperWidget->RequestRenderUpdate();

	if (PaperFadeElapsed >= Duration)
	{
		EndPaperCrossfade();
		OnPaperCrossfadeFinished.Broadcast();
	}
}

void ACWClipboardPropActor::EndPaperCrossfade()
{
	if (!bPaperCrossfading)
	{
		return;
	}

	bPaperCrossfading = false;
	// 페이지 인스턴스는 다시 쓰이므로 투명도를 남겨 두면 다음에 열 때 흐리게 보임
	if (UWidget* Outgoing = FadingOutContent.Get())
	{
		Outgoing->SetRenderOpacity(FadingOutOpacity);
	}
	FadingOutContent.Reset();
	PaperWidget->SetSlateWidget(nullptr);
	// 레이어가 페이지를 자식으로 쥔 채 남으면 창으로 되돌릴 때 부모가 둘이 됨
	for (TSharedPtr<SRetainerWidget>* Layer : {&EraseInkLayer, &WriteInkLayer})
	{
		if (Layer->IsValid())
		{
			(*Layer)->SetContent(SNullWidget::NullWidget);
			Layer->Reset();
		}
	}
	if (UWidget* Incoming = PaperContent.Get())
	{
		Incoming->SetRenderOpacity(PaperContentOpacity);
		PaperWidget->SetSlateWidget(Incoming->TakeWidget());
	}
	PaperWidget->RequestRenderUpdate();
}

TSharedRef<SRetainerWidget> ACWClipboardPropActor::MakeInkLayer(const TSharedRef<SWidget>& InContent, bool bReveal)
{
	TSharedRef<SRetainerWidget> Layer = SNew(SRetainerWidget).PhaseCount(1)[InContent];
	// 끄면 마스크 없이 자식을 그대로 그려 두 페이지가 동시에 다 보임
	Layer->SetRetainedRendering(true);
	Layer->SetWorld(GetWorld());
	Layer->SetTextureParameter(TEXT("Texture"));
	Layer->SetEffectMaterial(PaperInkWipeMaterial);
	if (UMaterialInstanceDynamic* Mask = Layer->GetEffectMaterial())
	{
		Mask->SetScalarParameterValue(TEXT("Reveal"), bReveal ? 1.f : 0.f);
		Mask->SetScalarParameterValue(TEXT("Progress"), 0.f);
	}
	return Layer;
}

void ACWClipboardPropActor::BeginPaperWarmup()
{
	UWidget* Content = PaperContent.Get();
	if (!PaperInkWipeMaterial || !Content || bPaperCrossfading || WarmupInkLayer.IsValid())
	{
		return;
	}

	// 전환 때 붓 레이어 안에 그려지는 것과 같은 종류(이미지·9분할·둥근 상자·글자)를 하나씩 넣음
	// 종류마다 렌더 준비가 따로라 하나라도 빠지면 그 요소를 처음 그릴 때 다시 깜빡일 수 있음
	UObject* PaperTexture = nullptr;
	if (const UBorder* PaperBorder = Cast<UBorder>(Content))
	{
		PaperTexture = PaperBorder->Background.GetResourceObject();
	}
	PaperWarmupImageBrush = FSlateBrush();
	PaperWarmupImageBrush.SetResourceObject(PaperTexture);
	PaperWarmupImageBrush.ImageSize = FVector2D(16.f);
	PaperWarmupBoxBrush = PaperWarmupImageBrush;
	PaperWarmupBoxBrush.DrawAs = ESlateBrushDrawType::Box;
	PaperWarmupBoxBrush.Margin = FMargin(0.25f);
	PaperWarmupRoundedBrush = FSlateRoundedBoxBrush(FLinearColor::White, 4.f, FVector2f(16.f, 16.f));

	// 쓰기 마스크의 진행도 0 은 완전히 투명해 화면에는 보이지 않음
	// 렌더 타깃 크기는 준비와 무관해 작게 두어 매 프레임 비용을 줄임
	WarmupInkLayer = MakeInkLayer(
		SNew(SBox).WidthOverride(64.f).HeightOverride(64.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[SNew(SImage).Image(&PaperWarmupImageBrush)]
			+ SOverlay::Slot()[SNew(SBorder).BorderImage(&PaperWarmupBoxBrush)]
			+ SOverlay::Slot()[SNew(SBorder).BorderImage(&PaperWarmupRoundedBrush)]
			+ SOverlay::Slot()[SNew(STextBlock).Text(FText::FromString(TEXT("가")))]
		],
		true);
	// 페이지 위에 겹쳐 두므로 클릭이 레이어에 걸리지 않게 함
	WarmupInkLayer->SetVisibility(EVisibility::HitTestInvisible);

	const TSharedRef<SWidget> PageSlate = Content->TakeWidget();
	PaperWidget->SetSlateWidget(nullptr);
	PaperWidget->SetSlateWidget(
		SNew(SOverlay)
		+ SOverlay::Slot()[PageSlate]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)[WarmupInkLayer.ToSharedRef()]);
	PaperWidget->RequestRenderUpdate();
}

void ACWClipboardPropActor::EndPaperWarmup()
{
	if (!WarmupInkLayer.IsValid())
	{
		return;
	}

	PaperWidget->SetSlateWidget(nullptr);
	WarmupInkLayer->SetContent(SNullWidget::NullWidget);
	WarmupInkLayer.Reset();
	if (UWidget* Content = PaperContent.Get())
	{
		PaperWidget->SetSlateWidget(Content->TakeWidget());
	}
	PaperWidget->RequestRenderUpdate();
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

void ACWClipboardPropActor::FocusPaperInput()
{
	const APlayerController* PC = OwningPlayerController.Get();
	if (bPaperInteractionEnabled && IsValid(PC) && PC->GetLocalPlayer() &&
		FSlateApplication::IsInitialized())
	{
		PaperInteraction->FocusHardwareUser(FSlateApplication::Get().GetUserIndexForController(
			PC->GetLocalPlayer()->GetControllerId()));
	}
}

bool ACWClipboardPropActor::ProjectPaperWidget(const FGeometry& WidgetGeometry, FVector2D& OutPosition, FVector2D& OutSize) const
{
	APlayerController* PC = OwningPlayerController.Get();
	if (!bPaperInteractionEnabled || ClipboardState != ECWClipboardState::Raised ||
		!IsValid(PC) || !PaperWidget || !PaperWidget->GetVirtualWindow().IsValid()) return false;
	const FVector2D Size = WidgetGeometry.GetLocalSize();
	const FVector2D ViewportPixels = UWidgetLayoutLibrary::GetViewportSize(this);
	if (Size.X <= 0 || Size.Y <= 0 || ViewportPixels.X <= 0 || ViewportPixels.Y <= 0) return false;
	const FGeometry PaperGeometry = PaperWidget->GetVirtualWindow()->GetCachedGeometry();
	const FGeometry ViewportGeometry = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this);
	const FGeometry PlayerGeometry = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(PC);
	const FVector2D PaperPivotOffset = FVector2D(PaperWidget->GetCurrentDrawSize()) * PaperWidget->GetPivot();
	auto Project = [&](const FVector2D& Local, FVector2D& Out)
	{
		const FVector2D PaperPoint = PaperGeometry.AbsoluteToLocal(WidgetGeometry.LocalToAbsolute(Local));
		// 월드 종이의 좌표 변환을 역으로 적용하여 화면 입력칸이 해상도 변경에도 같은 위치를 유지함
		const FVector World = PaperWidget->GetComponentTransform().TransformPosition(
			FVector(0, PaperPivotOffset.X - PaperPoint.X, PaperPivotOffset.Y - PaperPoint.Y));
		FVector2D Pixel;
		if (!PC->ProjectWorldLocationToScreen(World, Pixel, false)) return false;
		Out = PlayerGeometry.AbsoluteToLocal(ViewportGeometry.LocalToAbsolute(Pixel / ViewportPixels * ViewportGeometry.GetLocalSize()));
		return true;
	};
	FVector2D BottomRight;
	if (!Project(FVector2D::ZeroVector, OutPosition) || !Project(Size, BottomRight)) return false;
	OutSize = BottomRight - OutPosition;
	return OutSize.X > 0 && OutSize.Y > 0;
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
		FVector2D PixelPosition;
		FVector2D ViewportPosition;
		// 화면 위젯이 마우스를 캡처하면 뷰포트의 캐시 좌표가 멈추므로 실제 커서를 변환함
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}
		USlateBlueprintLibrary::AbsoluteToViewport(this, FSlateApplication::Get().GetCursorPos(),
			PixelPosition, ViewportPosition);
		if (PlayerController->DeprojectScreenPositionToWorld(PixelPosition.X, PixelPosition.Y, RayOrigin, RayDirection))
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
				const bool bInsidePaper = LocalHit.X >= 0.0 && LocalHit.Y >= 0.0 &&
					LocalHit.X <= DrawSize.X && LocalHit.Y <= DrawSize.Y;
				// 드래그 중 종이 밖으로 나가도 캡처한 컨트롤에 이동·해제를 전달함
				if (Distance >= 0.0 && (bInsidePaper || !PressedPaperKeys.IsEmpty()))
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
	// 내리기는 길이가 달라 시간이 아니라 진행 비율로 넘김
	if (ClipboardState == ECWClipboardState::Lowering)
	{
		const float LowerTotal = LowerMotionDuration + RotationLagSeconds;
		const float Remaining = LowerTotal > KINDA_SMALL_NUMBER ? 1.f - MotionElapsed / LowerTotal : 0.f;
		MotionElapsed = FMath::Max(0.f, Remaining) * (MotionDuration + RotationLagSeconds);
	}
	else
	{
		MotionElapsed = 0.f;
	}

	ClipboardState = ECWClipboardState::Raising;

	SetActorHiddenInGame(false);
	SetActorTickEnabled(true);
	SetHoldFullWorldResolution(true);
	BeginPaperWarmup();

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

	// 원래 숨김 포즈까지 걸리던 속도 그대로 더 먼 도착 포즈까지 가도록 거리 비율로 늘림
	FRotator RaisedRotation;
	const FVector RaisedLocation = GetEffectiveRaisedLocation(RaisedRotation);
	const double HiddenDistance = FVector::Dist(RaisedLocation, HiddenRelativeLocation);
	const double LoweredDistance = FVector::Dist(RaisedLocation, LoweredRelativeLocation);
	LowerMotionDuration = HiddenDistance > KINDA_SMALL_NUMBER
		? MotionDuration * static_cast<float>(FMath::Max(1.0, LoweredDistance / HiddenDistance))
		: MotionDuration;
	bLowerNotified = false;

	if (ClipboardState == ECWClipboardState::Raising)
	{
		const float RaiseTotal = MotionDuration + RotationLagSeconds;
		const float Remaining = RaiseTotal > KINDA_SMALL_NUMBER ? 1.f - MotionElapsed / RaiseTotal : 0.f;
		MotionElapsed = FMath::Max(0.f, Remaining) * (LowerMotionDuration + RotationLagSeconds);
	}
	else
	{
		MotionElapsed = 0.f;
	}

	ClipboardState = ECWClipboardState::Lowering;

	SetActorTickEnabled(true);
}

#pragma endregion

#pragma region PrivateFunctions

float ACWClipboardPropActor::ComputeAlpha(float InOffsetSeconds) const
{
	const float Duration = GetActiveMotionDuration();
	if (Duration <= KINDA_SMALL_NUMBER)
	{
		return 1.f;
	}

	return FMath::Clamp((MotionElapsed - InOffsetSeconds) / Duration, 0.f, 1.f);
}

void ACWClipboardPropActor::SetHoldFullWorldResolution(bool bHold)
{
	if (bHoldsFullWorldResolution == bHold)
	{
		return;
	}
	UCWGameUserSettings* Settings = UCWGameUserSettings::GetCWGameUserSettings();
	if (!Settings)
	{
		return;
	}
	bHoldsFullWorldResolution = bHold;
	if (bHold)
	{
		Settings->AcquireFullWorldResolution();
	}
	else
	{
		Settings->ReleaseFullWorldResolution();
	}
}

bool ACWClipboardPropActor::IsPropOffScreen() const
{
	const APlayerController* PlayerController = OwningPlayerController.Get();
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	if (!IsValid(PlayerController))
	{
		return false;
	}
	PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
	if (ViewportHeight <= 0)
	{
		return false;
	}

	// 검사할 부품이 하나도 없으면 내리기 시작 즉시 다음 화면이 열리므로 판정하지 않음
	bool bCheckedAnyPart = false;
	for (const UPrimitiveComponent* Part : {static_cast<const UPrimitiveComponent*>(ClipboardMesh),
		static_cast<const UPrimitiveComponent*>(PaperWidget)})
	{
		if (!Part || !Part->IsVisible())
		{
			continue;
		}
		bCheckedAnyPart = true;
		// 회전된 판보다 조금 큰 축 정렬 상자라 판이 완전히 사라진 뒤에만 참이 됨
		const FBox Box = Part->Bounds.GetBox();
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Point(
				(Corner & 1) ? Box.Max.X : Box.Min.X,
				(Corner & 2) ? Box.Max.Y : Box.Min.Y,
				(Corner & 4) ? Box.Max.Z : Box.Min.Z);
			FVector2D Screen;
			// 카메라 뒤로 간 점은 투영이 실패하며 어차피 보이지 않음
			if (PlayerController->ProjectWorldLocationToScreen(Point, Screen) && Screen.Y < ViewportHeight)
			{
				return false;
			}
		}
	}
	return bCheckedAnyPart;
}

float ACWClipboardPropActor::GetActiveMotionDuration() const
{
	return ClipboardState == ECWClipboardState::Lowering ? LowerMotionDuration : MotionDuration;
}

FVector ACWClipboardPropActor::GetEffectiveRaisedLocation(FRotator& OutRotation) const
{
	OutRotation = bPaperOnlyPresentation ? FRotator(90.f, 0.f, 0.f) : RaisedRelativeRotation;
	FVector Location = RaisedRelativeLocation;
	if (bPaperOnlyPresentation && ClipboardMesh && ClipboardMesh->GetStaticMesh())
	{
		const FVector MeshCenter = ClipboardMesh->GetRelativeTransform().TransformPosition(
			ClipboardMesh->GetStaticMesh()->GetBounds().Origin) * PresentationScale;
		const FVector RotatedCenter = OutRotation.RotateVector(MeshCenter);
		// 메시 원점이 아니라 보이는 판의 경계 중심을 카메라 화면 중앙에 맞춤
		Location.Y = -RotatedCenter.Y;
		Location.Z = -RotatedCenter.Z;
	}
	return Location;
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
	FRotator EffectiveRaisedRotation;
	const FVector EffectiveRaisedLocation = GetEffectiveRaisedLocation(EffectiveRaisedRotation);

	FVector RelativeLocation = FVector::ZeroVector;
	FRotator RelativeRotation = FRotator::ZeroRotator;

	if (bTowardRaised)
	{
		RelativeLocation = FMath::Lerp(HiddenRelativeLocation, EffectiveRaisedLocation, LocationBlend);
		RelativeRotation = FMath::Lerp(HiddenRelativeRotation, EffectiveRaisedRotation, RotationBlend);
	}
	else
	{
		RelativeLocation = FMath::Lerp(EffectiveRaisedLocation, LoweredRelativeLocation, LocationBlend);
		RelativeRotation = FMath::Lerp(EffectiveRaisedRotation, LoweredRelativeRotation, RotationBlend);
	}

	// 흔들림은 다 올라온 정도에 비례해 섞음. 숨김 상태에서는 0
	const float RaisedWeight = FMath::Clamp(
		bTowardRaised ? LocationBlend : 1.f - LocationBlend,
		0.f,
		1.f
	);

	if (!bPaperOnlyPresentation && SwayAmplitudeDegrees > KINDA_SMALL_NUMBER && RaisedWeight > KINDA_SMALL_NUMBER)
	{
		const float SwayPhase = SwayElapsed * SwayFrequency * 2.f * PI;

		// Pitch와 Roll의 주기를 다르게 설정해 진자처럼 보이지 않게 함
		RelativeRotation.Pitch +=
			FMath::Sin(SwayPhase) * SwayAmplitudeDegrees * RaisedWeight;
		RelativeRotation.Roll +=
			FMath::Sin(SwayPhase * 0.73f + 1.1f) * SwayAmplitudeDegrees * 0.6f * RaisedWeight;
	}
	// 종이 화면은 이동 중에도 카메라 정면을 유지해 기울기가 되살아나지 않도록 함
	if (bPaperOnlyPresentation) RelativeRotation = EffectiveRaisedRotation;

	const FTransform CameraTransform(CameraRotation, CameraLocation);
	const FTransform RelativeTransform(RelativeRotation, RelativeLocation, FVector(PresentationScale));

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
	MotionElapsed = GetActiveMotionDuration() + RotationLagSeconds;

	if (ClipboardState == ECWClipboardState::Raising)
	{
		ClipboardState = ECWClipboardState::Raised;
		// 입력이 열리기 전에 페이지만 남겨야 포커스·클릭이 겹침 패널 교체에 끊기지 않음
		EndPaperWarmup();

		UE_LOG(LogCWClipboard, Verbose, TEXT("[Clipboard] Raise 완료"));

		OnRaiseFinished.Broadcast();
		return;
	}

	if (ClipboardState == ECWClipboardState::Lowering)
	{
		ClipboardState = ECWClipboardState::Hidden;

		SetActorHiddenInGame(true);
		SetActorTickEnabled(false);
		SetHoldFullWorldResolution(false);
		EndPaperWarmup();

		// 틱이 멈추면 전환이 끝나지 않아 기다리던 화면의 입력이 계속 잠김
		if (bPaperCrossfading)
		{
			EndPaperCrossfade();
			OnPaperCrossfadeFinished.Broadcast();
		}

		UE_LOG(LogCWClipboard, Verbose, TEXT("[Clipboard] Lower 완료"));

		// Presenter 는 PlayLower 한 번에 완료를 한 번만 받아야 함
		if (!bLowerNotified)
		{
			bLowerNotified = true;
			OnLowerFinished.Broadcast();
		}
	}
}

#pragma endregion
