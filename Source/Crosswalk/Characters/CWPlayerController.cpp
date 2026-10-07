#include "Characters/CWPlayerController.h"

#include "Characters/CWPlayerCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "Core/CWPlayerState.h"
#include "Cinematics/CWCinematicCaptureSubsystem.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Console.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"

namespace
{
	enum EObserverAction
	{
		Team, Player1, Player2, Player3, Player4, Free, Shot1, Shot2, Shot3,
		Follow, OrbitLeft, OrbitRight, OrbitStop, CaptureToggle, ReleaseMouse, Speed, ActionCount
	};
}

ASpectatorPawn* ACWPlayerController::SpawnSpectatorPawn()
{
	const ACWPlayerState* State = GetPlayerState<ACWPlayerState>();
	if (State && (State->IsFilmObserver() || State->IsOnlyASpectator())) { return nullptr; }
	return Super::SpawnSpectatorPawn();
}

void ACWPlayerController::PlayerTick(float DeltaTime)
{
	UpdateObserverInput();
	Super::PlayerTick(DeltaTime);
	const ACWPlayerState* State = GetPlayerState<ACWPlayerState>();
	// The spectator-state RPC can arrive before the role properties on the client.
	if (State && State->IsFilmObserver() && GetSpectatorPawn()) { DestroySpectatorPawn(); }
}

void ACWPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ULocalPlayer* Local = GetLocalPlayer())
	{
		if (auto* Input = Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (ObserverMapping) { Input->RemoveMappingContext(ObserverMapping); }
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ACWPlayerController::UpdateObserverInput()
{
	if (!IsLocalController()) { return; }
	const ACWPlayerState* State = GetPlayerState<ACWPlayerState>();
	const bool bEnabled = State && State->IsFilmObserver() && GetWorld()->GetMapName().Contains(TEXT("Stage_"));
	ULocalPlayer* Local = GetLocalPlayer();
	auto* Input = Local ? Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	auto* EnhancedComponent = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input || !EnhancedComponent || bEnabled == bObserverMappingActive) { return; }
	if (!bEnabled)
	{
		Input->RemoveMappingContext(ObserverMapping);
		bObserverMappingActive = false;
		return;
	}
	if (!ObserverMapping)
	{
		ObserverMapping = NewObject<UInputMappingContext>(this);
		const FKey Keys[] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four,
			EKeys::F, EKeys::F1, EKeys::F2, EKeys::F3, EKeys::R, EKeys::Z, EKeys::X, EKeys::C,
			EKeys::F5, EKeys::Escape, EKeys::MouseWheelAxis };
		static_assert(UE_ARRAY_COUNT(Keys) == ActionCount);
		for (int32 Index = 0; Index < ActionCount; ++Index)
		{
			UInputAction* Action = NewObject<UInputAction>(this);
			Action->ValueType = Index == Speed ? EInputActionValueType::Axis1D : EInputActionValueType::Boolean;
			Action->bConsumeInput = true;
			ObserverActions.Add(Action);
			ObserverMapping->MapKey(Action, Keys[Index]);
			EnhancedComponent->BindAction(Action, Index == Speed ? ETriggerEvent::Triggered : ETriggerEvent::Started,
				this, &ACWPlayerController::HandleObserverAction, Index);
		}
	}
	Input->AddMappingContext(ObserverMapping, 100);
	bObserverMappingActive = true;
	SetInputMode(FInputModeGameOnly());
	UE_LOG(LogTemp, Display, TEXT("[CWFilmKeys] Observer mapping enabled: 16 actions. 1-4 players, F free, F1-F3 shots, F5 capture."));
}

void ACWPlayerController::HandleObserverAction(const FInputActionValue& Value, int32 ActionIndex)
{
	const ACWPlayerState* State = GetPlayerState<ACWPlayerState>();
	if (!State || !State->IsFilmObserver() || !bObserverMappingActive) { return; }
	const UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	// Typing in the console or switching to OBS must never trigger camera shortcuts.
	if (!Viewport || !Viewport->Viewport || !Viewport->Viewport->HasFocus() ||
		(Viewport->ViewportConsole && Viewport->ViewportConsole->ConsoleActive())) { return; }
	UCWCinematicCaptureSubsystem* Film = GetWorld()->GetSubsystem<UCWCinematicCaptureSubsystem>();
	if (!Film) { return; }
	if (ActionIndex == ReleaseMouse) { Film->SetFreeCamera(false); return; }
	if (ActionIndex == CaptureToggle)
	{
		if (Film->IsCapturing()) { Film->StopCapture(); }
		else { Film->StartCapture(); }
		return;
	}
	if (!Film->IsCapturing()) { Film->StartCapture(); }
	if (ActionIndex >= Team && ActionIndex <= Player4) { Film->SetPlayerTarget(ActionIndex); }
	else if (ActionIndex == Free) { Film->SetFreeCamera(!Film->IsFreeCamera()); }
	else if (ActionIndex >= Shot1 && ActionIndex <= Shot3) { Film->SetShot(ActionIndex - Shot1 + 1); }
	else if (ActionIndex == Follow) { Film->SetFollow(!Film->IsFollowingPlayers()); }
	else if (ActionIndex == OrbitLeft || ActionIndex == OrbitRight)
	{
		const float Desired = ActionIndex == OrbitLeft ? -2.f : 2.f;
		Film->SetOrbitSpeed(Film->GetOrbitSpeed() == Desired ? 0.f : Desired);
	}
	else if (ActionIndex == OrbitStop) { Film->SetOrbitSpeed(0.f); }
	else if (ActionIndex == Speed && Film->IsFreeCamera())
	{
		Film->SetFreeCameraSpeed(Film->GetFreeCameraSpeed() * (Value.Get<float>() > 0.f ? 1.25f : 0.8f));
	}
}

ACWPlayerController::ACWPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Crosshairs;
}

void ACWPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	ApplyAudioListenerOverride(InPawn);
}

void ACWPlayerController::AcknowledgePossession(APawn* InPawn)
{
	Super::AcknowledgePossession(InPawn);

	ApplyAudioListenerOverride(InPawn);
}

void ACWPlayerController::OnUnPossess()
{
	// 폰이 사라진 뒤에도 오버라이드가 남아 있으면 파괴된 컴포넌트를 가리키게 된다.
	if (IsLocalController())
	{
		ClearAudioListenerOverride();
	}

	Super::OnUnPossess();
}

void ACWPlayerController::ApplyAudioListenerOverride(APawn* InPawn)
{
	// 소리를 듣는 지점은 로컬 플레이어에게만 의미가 있다. 데디 서버와 원격 프록시는 건너뛴다.
	if (!IsLocalController())
	{
		return;
	}

	const ACWPlayerCharacter* PlayerCharacter = Cast<ACWPlayerCharacter>(InPawn);
	if (PlayerCharacter == nullptr || PlayerCharacter->CameraBoom == nullptr)
	{
		return;
	}

	// 붐이 아래를 내려다보는 만큼 반대로 돌려 리스너를 수평으로 되돌린다.
	// 카메라 각도를 나중에 바꿔도 여기서 다시 계산되므로 값을 따로 맞출 필요가 없다.
	const FRotator BoomRotation = PlayerCharacter->CameraBoom->GetRelativeRotation();
	const FRotator ListenerRotation(-BoomRotation.Pitch, 0.f, 0.f);

	SetAudioListenerOverride(PlayerCharacter->CameraBoom, FVector::ZeroVector, ListenerRotation);
}
