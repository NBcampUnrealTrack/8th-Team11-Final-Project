// CWVehicleAudioComponent.cpp

#include "Traffic/Components/CWVehicleAudioComponent.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Traffic/CWBaseVehicle.h"
#include "Traffic/CWTrafficSubsystem.h"

namespace
{
	/** 해수면 공기 중 음속(cm/s). 엔진의 USoundNodeDoppler 와 같은 값을 쓴다. */
	constexpr float SpeedOfSoundInAir = 33000.f;
}

UCWVehicleAudioComponent::UCWVehicleAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	/**
	 * 매 프레임 돌 필요가 없다. 도플러는 귀가 따라오는 속도보다 훨씬 빠르게 갱신되고,
	 * 어차피 보간으로 부드럽게 만든다. 0.05 면 차 한 대당 초당 20 번이라, 화면에 수십 대가
	 * 있어도 비용이 눈에 띄지 않는다.
	 */
	PrimaryComponentTick.TickInterval = 0.05f;
}

bool UCWVehicleAudioComponent::RequestHorn()
{
	if (!HornSound || !CanPlaySound())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// 1. 들리는 거리인가. 안 들릴 소리가 전역 예산을 까먹으면, 정작 옆에서 울려야 할
	//    차가 조용해진다.
	FVector ListenerLocation = FVector::ZeroVector;
	FVector ListenerVelocity = FVector::ZeroVector;
	if (!ResolveListener(ListenerLocation, ListenerVelocity))
	{
		return false;
	}

	if (FVector::DistSquared(ListenerLocation, GetOwner()->GetActorLocation()) > FMath::Square(HornAudibleRange))
	{
		return false;
	}

	// 2. 이 차가 최근에 울렸나.
	const float Now = World->GetTimeSeconds();
	if (Now - LastHornTime < HornCooldownSeconds)
	{
		return false;
	}

	// 3. 월드 전체가 최근에 울렸나. 여기까지 온 차들 중 먼저 부른 하나만 통과한다.
	UCWTrafficSubsystem* TrafficSubsystem = UCWTrafficSubsystem::Get(this);
	if (TrafficSubsystem && !TrafficSubsystem->TryConsumeHornBudget(GlobalHornIntervalSeconds))
	{
		return false;
	}

	HornAudio = UGameplayStatics::SpawnSoundAttached(
		HornSound,
		GetOwner()->GetRootComponent(),
		NAME_None,
		FVector::ZeroVector,
		EAttachLocation::KeepRelativeOffset,
		/* bStopWhenAttachedToDestroyed */ true,
		1.f,
		CurrentDopplerPitch,
		0.f,
		SharedAttenuation
	);

	LastHornTime = Now;

	return true;
}

void UCWVehicleAudioComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerVehicle = Cast<ACWBaseVehicle>(GetOwner());
	if (!OwnerVehicle)
	{
		SetComponentTickEnabled(false);

		return;
	}

	if (!CanPlaySound())
	{
		// 데디케이티드 서버에는 들을 사람이 없다.
		SetComponentTickEnabled(false);

		return;
	}

	StartEngineLoop();
}

void UCWVehicleAudioComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 차량이 파괴되면 붙은 소리도 같이 사라지지만, 레벨 전환처럼 파괴가 아닌 종료도 있다.
	if (IsValid(EngineAudio))
	{
		EngineAudio->Stop();
	}

	EngineAudio = nullptr;
	HornAudio = nullptr;

	Super::EndPlay(EndPlayReason);
}

void UCWVehicleAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!OwnerVehicle)
	{
		return;
	}

	UpdateEngineParameters();

	if (bEnableDoppler)
	{
		UpdateDoppler(DeltaTime);
	}
}

void UCWVehicleAudioComponent::StartEngineLoop()
{
	if (!EngineLoopSound)
	{
		return;
	}

	EngineAudio = UGameplayStatics::SpawnSoundAttached(
		EngineLoopSound,
		GetOwner()->GetRootComponent(),
		NAME_None,
		FVector::ZeroVector,
		EAttachLocation::KeepRelativeOffset,
		/* bStopWhenAttachedToDestroyed */ true,
		1.f,
		1.f,
		0.f,
		SharedAttenuation
	);
}

void UCWVehicleAudioComponent::UpdateEngineParameters()
{
	if (!IsValid(EngineAudio) || EngineSpeedParameterName.IsNone())
	{
		return;
	}

	const float Speed01 = FMath::Clamp(OwnerVehicle->GetCurrentSpeed() / EngineSpeedReference, 0.f, 1.f);

	EngineAudio->SetFloatParameter(EngineSpeedParameterName, Speed01);
}

void UCWVehicleAudioComponent::UpdateDoppler(float DeltaTime)
{
	const bool bHasEngine = IsValid(EngineAudio);
	const bool bHasHorn = IsValid(HornAudio);
	if (!bHasEngine && !bHasHorn)
	{
		return;
	}

	FVector ListenerLocation = FVector::ZeroVector;
	FVector ListenerVelocity = FVector::ZeroVector;
	if (!ResolveListener(ListenerLocation, ListenerVelocity))
	{
		return;
	}

	const FVector SourceLocation = GetOwner()->GetActorLocation();
	const FVector SourceToListener = (ListenerLocation - SourceLocation).GetSafeNormal();

	// 둘 사이를 잇는 선 위로 각자의 속도를 투영한다. 옆으로 스쳐 지나가는 성분은
	// 거리를 바꾸지 않으므로 음높이에도 영향이 없다.
	const float SourceTowardListener = OwnerVehicle->GetVelocity() | SourceToListener;
	const float ListenerAwayFromSource = ListenerVelocity | SourceToListener;

	const float ClosingSpeed = SourceTowardListener - ListenerAwayFromSource;

	// 분모가 0 이 되는 것은 서로 음속으로 접근할 때다. 이 게임에서는 올 수 없는 값이지만,
	// 값이 밖에서 주입되는 이상(SpeedMultiplier, 패링 가속) 나눗셈은 막아 둔다.
	const float Denominator = 1.f - (ClosingSpeed / SpeedOfSoundInAir);
	if (FMath::IsNearlyZero(Denominator))
	{
		return;
	}

	const float RawPitch = 1.f / Denominator;

	// 세기는 "1 로부터 얼마나 떨어졌나"에 곱한다. 그래야 정지 중인 차의 피치가 1 로 남는다.
	const float TargetPitch = FMath::Clamp(
		((RawPitch - 1.f) * DopplerIntensity) + 1.f,
		DopplerPitchMin,
		DopplerPitchMax
	);

	CurrentDopplerPitch = FMath::FInterpTo(CurrentDopplerPitch, TargetPitch, DeltaTime, DopplerSmoothingSpeed);

	if (bHasEngine)
	{
		EngineAudio->SetPitchMultiplier(CurrentDopplerPitch);
	}

	if (bHasHorn)
	{
		HornAudio->SetPitchMultiplier(CurrentDopplerPitch);
	}
}

bool UCWVehicleAudioComponent::ResolveListener(FVector& OutLocation, FVector& OutVelocity) const
{
	// 오디오 리스너는 기본적으로 카메라에 붙는다. 폰 위치가 아니라 카메라 위치가 기준이다.
	const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!PlayerController || !PlayerController->PlayerCameraManager)
	{
		return false;
	}

	OutLocation = PlayerController->PlayerCameraManager->GetCameraLocation();

	// 카메라 자체의 속도 대신 폰의 속도를 쓴다. 카메라는 회전·랙으로 흔들려서
	// 프레임 차분을 그대로 쓰면 제자리에 선 플레이어에게도 도플러가 걸린다.
	if (const APawn* LocalPawn = PlayerController->GetPawn())
	{
		OutVelocity = LocalPawn->GetVelocity();
	}
	else
	{
		OutVelocity = FVector::ZeroVector;
	}

	return true;
}

bool UCWVehicleAudioComponent::CanPlaySound() const
{
	const UWorld* World = GetWorld();
	if (!World || !GetOwner())
	{
		return false;
	}

	return World->GetNetMode() != NM_DedicatedServer;
}
