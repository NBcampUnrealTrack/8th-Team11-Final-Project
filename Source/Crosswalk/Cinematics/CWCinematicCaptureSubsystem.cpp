#include "Cinematics/CWCinematicCaptureSubsystem.h"

#include "Characters/CWPlayerCharacter.h"
#include "Core/CWPlayerState.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Console.h"
#include "UnrealClient.h"
#include "InputCoreTypes.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/WidgetComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "String/LexFromString.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/SNullWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogCWFilm, Log, All);

namespace CWFilm
{
	UCWCinematicCaptureSubsystem* Get(UWorld* World)
	{
		return World ? World->GetSubsystem<UCWCinematicCaptureSubsystem>() : nullptr;
	}

	bool ReadNumbers(const TArray<FString>& Args, int32 Count, TArray<float>& Numbers)
	{
		if (Args.Num() != Count)
		{
			UE_LOG(LogCWFilm, Warning, TEXT("Expected %d numeric arguments; see command help."), Count);
			return false;
		}
		for (const FString& Arg : Args)
		{
			float Value = 0.f;
			if (!LexTryParseString(Value, *Arg) || !FMath::IsFinite(Value))
			{
				UE_LOG(LogCWFilm, Warning, TEXT("Invalid number: %s"), *Arg);
				return false;
			}
			Numbers.Add(Value);
		}
		return true;
	}

	FAutoConsoleCommandWithWorldAndArgs Start(TEXT("cw.Film.Start"),
		TEXT("Open local filming window: cw.Film.Start [width height fps]. Default 1920 1080 30."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (auto* Film = Get(World))
			{
				if (Args.IsEmpty()) { Film->StartCapture(); return; }
				TArray<float> Values;
				if (ReadNumbers(Args, 3, Values))
				{
					if (Values[0] < 320 || Values[0] > 3840 || Values[1] < 180 || Values[1] > 2160 || Values[2] < 1 || Values[2] > 60)
					{
						UE_LOG(LogCWFilm, Warning, TEXT("Width 320..3840, height 180..2160, fps 1..60."));
						return;
					}
					Film->StartCapture(static_cast<int32>(Values[0]), static_cast<int32>(Values[1]), static_cast<int32>(Values[2]));
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs Stop(TEXT("cw.Film.Stop"), TEXT("Close filming window and release extra rendering."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{ if (auto* Film = Get(World)) { Film->StopCapture(); } }));

	FAutoConsoleCommandWithWorldAndArgs Shot(TEXT("cw.Film.Shot"), TEXT("Preset: 1 elevated, 2 side, 3 wide. Retains focus position."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			TArray<float> Values;
			if (auto* Film = Get(World); Film && ReadNumbers(Args, 1, Values))
			{ Film->SetShot(static_cast<int32>(Values[0])); }
		}));

	FAutoConsoleCommandWithWorldAndArgs Follow(TEXT("cw.Film.Follow"), TEXT("1 follows the four-player center; 0 locks current focus."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			TArray<float> Values;
			if (auto* Film = Get(World); Film && ReadNumbers(Args, 1, Values))
			{ Film->SetFollow(Values[0] != 0.f); }
		}));

	FAutoConsoleCommandWithWorldAndArgs Place(TEXT("cw.Film.Place"), TEXT("Manual fixed camera: x y z pitch yaw fov. Units cm/degrees."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			TArray<float> Values;
			if (auto* Film = Get(World); Film && ReadNumbers(Args, 6, Values))
			{ Film->PlaceCamera(FVector(Values[0], Values[1], Values[2]), FRotator(Values[3], Values[4], 0.f), Values[5]); }
		}));

	FAutoConsoleCommandWithWorldAndArgs Focus(TEXT("cw.Film.Focus"), TEXT("Lock preset shots to a point: x y z (cm)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			TArray<float> Values;
			if (auto* Film = Get(World); Film && ReadNumbers(Args, 3, Values))
			{ Film->SetFocus(FVector(Values[0], Values[1], Values[2])); }
		}));

	FAutoConsoleCommandWithWorldAndArgs Orbit(TEXT("cw.Film.Orbit"), TEXT("Slow orbit speed in degrees/sec, -10..10; 0 stops."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			TArray<float> Values;
			if (auto* Film = Get(World); Film && ReadNumbers(Args, 1, Values))
			{ Film->SetOrbitSpeed(Values[0]); }
		}));

	FAutoConsoleCommandWithWorldAndArgs Status(TEXT("cw.Film.Status"), TEXT("Log filming state and camera coordinates."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{ if (auto* Film = Get(World)) { Film->PrintStatus(); } }));

	FAutoConsoleCommandWithWorldAndArgs MatchGame(TEXT("cw.Film.MatchGame"), TEXT("Match a gameplay camera for a lighting comparison; Shot restores filming framing."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{ if (auto* Film = Get(World)) { Film->MatchGameCamera(); } }));

	FAutoConsoleCommandWithWorldAndArgs Player(TEXT("cw.Film.Player"), TEXT("Follow player 1..4 ordered by PlayerId; 0 follows the team center."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			TArray<float> Values;
			if (auto* Film = Get(World); Film && ReadNumbers(Args, 1, Values)) { Film->SetPlayerTarget(static_cast<int32>(Values[0])); }
		}));

	FAutoConsoleCommandWithWorldAndArgs Free(TEXT("cw.Film.Free"), TEXT("1: WASD + mouse free camera, E/Q up/down, Shift fast; 0: restore framing/input."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			TArray<float> Values;
			if (auto* Film = Get(World); Film && ReadNumbers(Args, 1, Values)) { Film->SetFreeCamera(Values[0] != 0.f); }
		}));

	FAutoConsoleCommandWithWorldAndArgs Speed(TEXT("cw.Film.Speed"), TEXT("Free camera speed in cm/sec, 100..10000. Default 1200."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			TArray<float> Values;
			if (auto* Film = Get(World); Film && ReadNumbers(Args, 1, Values)) { Film->SetFreeCameraSpeed(Values[0]); }
		}));
}

bool UCWCinematicCaptureSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCWCinematicCaptureSubsystem::Deinitialize()
{
	StopCapture();
	Super::Deinitialize();
}

void UCWCinematicCaptureSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	StopCapture();
	Super::OnWorldEndPlay(InWorld);
}

void UCWCinematicCaptureSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// Only the filming launcher opts in. A normal launch does no extra rendering.
	if (FParse::Param(FCommandLine::Get(), TEXT("CWFilm")) && InWorld.GetMapName().Contains(TEXT("Stage_")))
	{
		FTimerHandle StartupTimer;
		InWorld.GetTimerManager().SetTimer(StartupTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]() { StartCapture(); }), 2.f, false);
	}
}

bool UCWCinematicCaptureSubsystem::IsTickable() const
{
	return !IsTemplate() && bCapturing;
}

TStatId UCWCinematicCaptureSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCWCinematicCaptureSubsystem, STATGROUP_Tickables);
}

void UCWCinematicCaptureSubsystem::StartCapture(int32 Width, int32 Height, int32 FrameRate)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || !FSlateApplication::IsInitialized()) { return; }
	StopCapture();
	Width = FMath::Clamp(Width, 320, 3840);
	Height = FMath::Clamp(Height, 180, 2160);
	CaptureInterval = 1.f / FMath::Clamp(FrameRate, 1, 60);
	CaptureAccumulator = 0.f;
	HiddenRefreshAccumulator = 0.f;
	ActiveShot = 1;
	TrackedPlayerId = INDEX_NONE;
	OrbitAngle = OrbitSpeed = 0.f;
	bFollowPlayers = true;
	bManualCamera = false;
	FocusLocation = FindPlayerCenter();

	RenderTarget = NewObject<UTextureRenderTarget2D>(this);
	RenderTarget->ClearColor = FLinearColor::Black;
	// Linear float keeps the tone-curved output; an 8-bit linear target crushes shadows onto the 1/255 step.
	RenderTarget->InitCustomFormat(Width, Height, PF_FloatRGBA, true);
	RenderTarget->UpdateResourceImmediate(true);
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	CaptureActor = World->SpawnActor<ASceneCapture2D>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
	if (!IsValid(CaptureActor)) { StopCapture(); return; }
	CaptureActor->SetReplicates(false);
	CaptureActor->SetActorTickEnabled(false);
	USceneCaptureComponent2D* Capture = CaptureActor->GetCaptureComponent2D();
	Capture->TextureTarget = RenderTarget;
	// Keeps exposure, tone curve and PPV grading but outputs linear color for Slate to encode once.
	Capture->CaptureSource = SCS_FinalToneCurveHDR;
	// A single scheduled capture avoids rendering both every-frame and explicit captures.
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	SyncRenderingSettings();
	UpdateCamera(0.f);
	RefreshHiddenComponents();
	Capture->CaptureScene();

	PreviewBrush.SetResourceObject(RenderTarget);
	PreviewBrush.ImageSize = FVector2D(Width, Height);
	PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	CaptureWindow = SNew(SWindow)
		.Title(FText::FromString(TEXT("Crosswalk Film Capture")))
		.ClientSize(FVector2D(Width, Height))
		.FocusWhenFirstShown(false)
		.ActivationPolicy(EWindowActivationPolicy::Never)
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		[
			SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
			[
				SNew(SImage).Image(&PreviewBrush).Visibility(EVisibility::HitTestInvisible)
			]
		];
	CaptureWindow->SetOnWindowClosed(FOnWindowClosed::CreateUObject(this, &UCWCinematicCaptureSubsystem::HandleWindowClosed));
	FSlateApplication::Get().AddWindow(CaptureWindow.ToSharedRef());
	bCapturing = true;
	UE_LOG(LogCWFilm, Display, TEXT("Started %dx%d at up to %d captures/sec. OBS: Window Capture 'Crosswalk Film Capture', cursor OFF. Game view and audio listener unchanged."), Width, Height, FMath::Clamp(FrameRate, 1, 60));
}

void UCWCinematicCaptureSubsystem::StopCapture()
{
	SetFreeCamera(false);
	bCapturing = false;
	if (CaptureWindow.IsValid())
	{
		// Detach the image before releasing the brush/UObject or closing the window.
		TSharedPtr<SWindow> Window = CaptureWindow;
		CaptureWindow.Reset();
		Window->SetOnWindowClosed(FOnWindowClosed());
		Window->SetContent(SNullWidget::NullWidget);
		Window->RequestDestroyWindow();
	}
	PreviewBrush.SetResourceObject(nullptr);
	if (IsValid(CaptureActor))
	{
		CaptureActor->GetCaptureComponent2D()->TextureTarget = nullptr;
		CaptureActor->Destroy();
	}
	CaptureActor = nullptr;
	RenderTarget = nullptr;
}

void UCWCinematicCaptureSubsystem::HandleWindowClosed(const TSharedRef<SWindow>& Window)
{
	Window->SetContent(SNullWidget::NullWidget);
	CaptureWindow.Reset();
	StopCapture();
}

FVector UCWCinematicCaptureSubsystem::FindPlayerCenter() const
{
	FVector Center = FVector::ZeroVector;
	int32 Count = 0;
	for (TActorIterator<ACWPlayerCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsActorBeingDestroyed() && It->GetPlayerState())
		{
			if (It->GetPlayerState()->GetPlayerId() == TrackedPlayerId) { return It->GetActorLocation(); }
			Center += It->GetActorLocation();
			++Count;
		}
	}
	if (Count > 0) { return Center / Count; }
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (const APawn* Pawn = PC->GetPawn()) { return Pawn->GetActorLocation(); }
	}
	return FVector::ZeroVector;
}

void UCWCinematicCaptureSubsystem::UpdateCamera(float DeltaTime)
{
	if (!IsValid(CaptureActor) || bManualCamera) { return; }
	if (bFollowPlayers)
	{
		FocusLocation = DeltaTime > 0.f
			? FMath::VInterpTo(FocusLocation, FindPlayerCenter(), DeltaTime, 1.5f)
			: FindPlayerCenter();
	}
	OrbitAngle = FMath::UnwindDegrees(OrbitAngle + OrbitSpeed * DeltaTime);
	const float Pitch = ActiveShot == 2 ? -22.f : (ActiveShot == 3 ? -55.f : -38.f);
	const float Distance = ActiveShot == 2 ? 1500.f : (ActiveShot == 3 ? 3200.f : 2200.f);
	const FRotator Rotation(Pitch, -90.f + OrbitAngle, 0.f);
	CaptureActor->SetActorLocationAndRotation(FocusLocation - Rotation.Vector() * Distance, Rotation);
	CaptureActor->GetCaptureComponent2D()->FOVAngle = ActiveShot == 2 ? 55.f : 65.f;
}

void UCWCinematicCaptureSubsystem::RefreshHiddenComponents()
{
	USceneCaptureComponent2D* Capture = CaptureActor->GetCaptureComponent2D();
	Capture->HiddenComponents.Reset();
	Capture->HiddenActors.Reset();
	// World-space UI renders as geometry; screen-space HUD is naturally absent.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("CWFilmHide"))) { Capture->HiddenActors.Add(*It); }
		TInlineComponentArray<UWidgetComponent*> Widgets(*It);
		for (UWidgetComponent* Widget : Widgets) { Capture->HideComponent(Widget); }
	}
}

void UCWCinematicCaptureSubsystem::Tick(float DeltaTime)
{
	if (!bCapturing || !IsValid(CaptureActor)) { return; }
	if (bFreeCamera) { UpdateFreeCamera(DeltaTime); }
	else { UpdateCamera(DeltaTime); }
	SyncRenderingSettings();
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		const ACWPlayerState* State = PC->GetPlayerState<ACWPlayerState>();
		if (PC->IsLocalController() && State && State->IsFilmObserver())
		{
			// Pawnless observers still send a useful view location for network relevancy.
			PC->bAutoManageActiveCameraTarget = false;
			PC->SetViewTarget(CaptureActor);
			PC->SetAudioListenerOverride(nullptr, FocusLocation, CaptureActor->GetActorRotation());
		}
	}
	HiddenRefreshAccumulator += DeltaTime;
	if (HiddenRefreshAccumulator >= 1.f)
	{
		RefreshHiddenComponents();
		HiddenRefreshAccumulator = 0.f;
	}
	CaptureAccumulator += DeltaTime;
	if (CaptureAccumulator >= CaptureInterval)
	{
		// Do not catch up with several renders after a hitch.
		CaptureAccumulator = FMath::Fmod(CaptureAccumulator, CaptureInterval);
		CaptureActor->GetCaptureComponent2D()->CaptureScene();
	}
}

void UCWCinematicCaptureSubsystem::SyncRenderingSettings()
{
	if (!IsValid(CaptureActor)) { return; }
	USceneCaptureComponent2D* Capture = CaptureActor->GetCaptureComponent2D();
	// Camera settings blend over the same world volumes as a normal game view.
	FPostProcessSettings Settings;
	float BlendWeight = 1.f;
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const ACWPlayerState* State = PC ? PC->GetPlayerState<ACWPlayerState>() : nullptr;
	if (PC && PC->PlayerCameraManager && !(State && State->IsFilmObserver()))
	{
		const FMinimalViewInfo& View = PC->PlayerCameraManager->GetCameraCacheView();
		Settings = View.PostProcessSettings;
		BlendWeight = View.PostProcessBlendWeight;
	}
	else
	{
		for (TActorIterator<ACWPlayerCharacter> It(GetWorld()); It; ++It)
		{
			if (It->GetPlayerState() && It->TopDownCamera)
			{
				Settings = It->TopDownCamera->PostProcessSettings;
				BlendWeight = It->TopDownCamera->PostProcessBlendWeight;
				break;
			}
		}
	}
	// UE disables Lumen in scene captures after volume blending unless explicitly overridden.
	if (!Settings.bOverride_DynamicGlobalIlluminationMethod || BlendWeight <= 0.f)
	{
		const IConsoleVariable* Method = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod"));
		Settings.DynamicGlobalIlluminationMethod = static_cast<EDynamicGlobalIlluminationMethod::Type>(Method ? Method->GetInt() : 1);
	}
	if (!Settings.bOverride_ReflectionMethod || BlendWeight <= 0.f)
	{
		const IConsoleVariable* Method = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ReflectionMethod"));
		Settings.ReflectionMethod = static_cast<EReflectionMethod::Type>(Method ? Method->GetInt() : 1);
	}
	Settings.bOverride_DynamicGlobalIlluminationMethod = true;
	Settings.bOverride_ReflectionMethod = true;
	Settings.bOverride_LumenSurfaceCacheResolution = true;
	Settings.LumenSurfaceCacheResolution = 1.f;
	Capture->PostProcessSettings = Settings;
	Capture->PostProcessBlendWeight = 1.f;
}

void UCWCinematicCaptureSubsystem::MatchGameCamera()
{
	for (TActorIterator<ACWPlayerCharacter> It(GetWorld()); It; ++It)
	{
		if (It->GetPlayerState() && It->TopDownCamera)
		{
			PlaceCamera(It->TopDownCamera->GetComponentLocation(), It->TopDownCamera->GetComponentRotation(), It->TopDownCamera->FieldOfView);
			return;
		}
	}
}

void UCWCinematicCaptureSubsystem::SetShot(int32 Shot)
{
	if (Shot < 1 || Shot > 3) { UE_LOG(LogCWFilm, Warning, TEXT("Shot must be 1, 2 or 3.")); return; }
	SetFreeCamera(false);
	ActiveShot = Shot;
	bManualCamera = false;
	UpdateCamera(0.f);
	PrintStatus();
}

void UCWCinematicCaptureSubsystem::SetFollow(bool bEnabled)
{
	SetFreeCamera(false);
	bFollowPlayers = bEnabled;
	bManualCamera = false;
	UpdateCamera(0.f);
}

void UCWCinematicCaptureSubsystem::SetFocus(const FVector& Location)
{
	SetFreeCamera(false);
	FocusLocation = Location;
	bFollowPlayers = false;
	bManualCamera = false;
	UpdateCamera(0.f);
}

void UCWCinematicCaptureSubsystem::PlaceCamera(const FVector& Location, const FRotator& Rotation, float FieldOfView)
{
	if (!IsValid(CaptureActor)) { UE_LOG(LogCWFilm, Warning, TEXT("Run cw.Film.Start first.")); return; }
	SetFreeCamera(false);
	bManualCamera = true;
	bFollowPlayers = false;
	CaptureActor->SetActorLocationAndRotation(Location, Rotation);
	CaptureActor->GetCaptureComponent2D()->FOVAngle = FMath::Clamp(FieldOfView, 20.f, 100.f);
	PrintStatus();
}

void UCWCinematicCaptureSubsystem::SetOrbitSpeed(float DegreesPerSecond)
{
	SetFreeCamera(false);
	OrbitSpeed = FMath::Clamp(DegreesPerSecond, -10.f, 10.f);
	bManualCamera = false;
}

void UCWCinematicCaptureSubsystem::PrintStatus() const
{
	UE_LOG(LogCWFilm, Display, TEXT("TargetPlayerId=%d FreeCamera=%d Speed=%.0f cm/sec"), TrackedPlayerId, bFreeCamera, FreeCameraSpeed);
	UE_LOG(LogCWFilm, Display, TEXT("Capture=%d Shot=%d Follow=%d Manual=%d FPS ceiling=%.0f Focus=%s"),
		bCapturing, ActiveShot, bFollowPlayers, bManualCamera, 1.f / CaptureInterval, *FocusLocation.ToString());
	if (IsValid(CaptureActor))
	{
		const USceneCaptureComponent2D* Capture = CaptureActor->GetCaptureComponent2D();
		UE_LOG(LogCWFilm, Display, TEXT("GI=%d Reflections=%d LumenCache=%.1f"),
			static_cast<int32>(Capture->PostProcessSettings.DynamicGlobalIlluminationMethod),
			static_cast<int32>(Capture->PostProcessSettings.ReflectionMethod), Capture->PostProcessSettings.LumenSurfaceCacheResolution);
		UE_LOG(LogCWFilm, Display, TEXT("Camera=%s Rotation=%s FOV=%.1f"),
			*CaptureActor->GetActorLocation().ToString(), *CaptureActor->GetActorRotation().ToString(), CaptureActor->GetCaptureComponent2D()->FOVAngle);
	}
}

void UCWCinematicCaptureSubsystem::SetPlayerTarget(int32 PlayerNumber)
{
	if (PlayerNumber < 0 || PlayerNumber > 4) { UE_LOG(LogCWFilm, Warning, TEXT("Player must be 0..4.")); return; }
	TArray<ACWPlayerCharacter*> Players;
	for (TActorIterator<ACWPlayerCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsActorBeingDestroyed() && It->GetPlayerState()) { Players.Add(*It); }
	}
	Players.Sort([](const ACWPlayerCharacter& A, const ACWPlayerCharacter& B)
	{
		return A.GetPlayerState()->GetPlayerId() < B.GetPlayerState()->GetPlayerId();
	});
	if (PlayerNumber > Players.Num()) { UE_LOG(LogCWFilm, Warning, TEXT("Player %d is not connected."), PlayerNumber); return; }
	SetFreeCamera(false);
	TrackedPlayerId = PlayerNumber == 0 ? INDEX_NONE : Players[PlayerNumber - 1]->GetPlayerState()->GetPlayerId();
	SetFollow(true);
	PrintStatus();
}

void UCWCinematicCaptureSubsystem::SetFreeCamera(bool bEnabled)
{
	if (bFreeCamera == bEnabled) { return; }
	if (bEnabled)
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (!IsValid(CaptureActor) || !PC || !PC->IsLocalController()) { return; }
		FreeCameraController = PC;
		bSavedMouseCursor = PC->bShowMouseCursor;
		// Do not re-enable a pawn whose input was already disabled by another flow.
		APawn* Pawn = PC->GetPawn();
		DisabledInputPawn = Pawn && Pawn->InputEnabled() ? Pawn : nullptr;
		if (DisabledInputPawn.IsValid()) { DisabledInputPawn->DisableInput(PC); }
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
		PC->bShowMouseCursor = false;
		FInputModeGameOnly InputMode;
		PC->SetInputMode(InputMode);
		bManualCamera = true;
	}
	else
	{
		if (APlayerController* PC = FreeCameraController.Get())
		{
			if (DisabledInputPawn.IsValid()) { DisabledInputPawn->EnableInput(PC); }
			PC->SetIgnoreMoveInput(false);
			PC->SetIgnoreLookInput(false);
			PC->bShowMouseCursor = bSavedMouseCursor;
			FInputModeGameAndUI InputMode;
			InputMode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(InputMode);
		}
		FreeCameraController.Reset();
		DisabledInputPawn.Reset();
		bManualCamera = false;
	}
	bFreeCamera = bEnabled;
	UE_LOG(LogCWFilm, Display, TEXT("Free camera %s. Focus game window; close console to use WASD/mouse."), bEnabled ? TEXT("ON") : TEXT("OFF"));
}

void UCWCinematicCaptureSubsystem::SetFreeCameraSpeed(float CentimetersPerSecond)
{
	FreeCameraSpeed = FMath::Clamp(CentimetersPerSecond, 100.f, 10000.f);
}

void UCWCinematicCaptureSubsystem::UpdateFreeCamera(float DeltaTime)
{
	APlayerController* PC = FreeCameraController.Get();
	UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport();
	if (ViewportClient && ViewportClient->Viewport && !ViewportClient->Viewport->HasFocus()) { return; }
	if (!PC || (GetWorld()->GetGameViewport() && GetWorld()->GetGameViewport()->ViewportConsole &&
		GetWorld()->GetGameViewport()->ViewportConsole->ConsoleActive())) { return; }
	float MouseX = 0.f, MouseY = 0.f;
	PC->GetInputMouseDelta(MouseX, MouseY);
	FRotator Rotation = CaptureActor->GetActorRotation();
	Rotation.Yaw = FMath::UnwindDegrees(Rotation.Yaw + MouseX * 0.15f);
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch + MouseY * 0.15f, -89.f, 89.f);
	const float Forward = float(PC->IsInputKeyDown(EKeys::W)) - float(PC->IsInputKeyDown(EKeys::S));
	const float Right = float(PC->IsInputKeyDown(EKeys::D)) - float(PC->IsInputKeyDown(EKeys::A));
	const float Up = float(PC->IsInputKeyDown(EKeys::E)) - float(PC->IsInputKeyDown(EKeys::Q));
	const FVector Movement = Rotation.Vector() * Forward + FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y) * Right + FVector::UpVector * Up;
	const float SpeedScale = PC->IsInputKeyDown(EKeys::LeftShift) ? 3.f : 1.f;
	CaptureActor->SetActorLocationAndRotation(CaptureActor->GetActorLocation() + Movement.GetClampedToMaxSize(1.f) * FreeCameraSpeed * SpeedScale * DeltaTime, Rotation);
}
