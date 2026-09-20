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

	SpawnHorn();

	LastHornTime = Now;

	return true;
}

void UCWVehicleAudioComponent::SpawnHorn()
{
	/**
	 * 경적의 피치는 여기서 한 번 정하고 소리가 끝날 때까지 그대로 간다.
	 *
	 * CurrentDopplerPitch(엔진 루프용 보간 상태)를 쓰지 않는 이유가 둘이다.
	 * 하나는 엔진 루프가 없으면 그 값이 갱신될 일이 없어 초기값 1.0 에 얼어 있다는 것,
	 * 다른 하나는 여기서 덮어쓰면 엔진 쪽 보간이 튄다는 것이다.
	 *
	 * 실패하면 1.0 으로 둔다. 도플러를 못 구한 것이지 경적을 거를 일은 아니다.
	 */
	float HornPitch = 1.f;
	if (bEnableDoppler)
	{
		ComputeDopplerPitch(DopplerIntensity, HornPitch);
	}

	UGameplayStatics::SpawnSoundAttached(
		HornSound,
		GetOwner()->GetRootComponent(),
		NAME_None,
		FVector::ZeroVector,
		EAttachLocation::KeepRelativeOffset,
		/* bStopWhenAttachedToDestroyed */ true,
		1.f,
		HornPitch,
		0.f,
		SharedAttenuation
	);
}

void UCWVehicleAudioComponent::UpdateApproachWarning()
{
	// 한 번 울렸으면 끝이다. 플레이어가 경로를 들락거려도 다시 울리지 않는다.
	if (bWarnedLocalPlayer || !HornSound)
	{
		return;
	}

	const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!PlayerController)
	{
		return;
	}

	/**
	 * 카메라가 아니라 폰을 넘긴다.
	 *
	 * 리스너(소리를 듣는 자리)는 카메라지만, 차가 칠 대상은 캐릭터다. 탑뷰라 카메라가
	 * 캐릭터보다 688cm 뒤에 있어서, 카메라로 재면 기본 속도에서도 0.9 초쯤 늦게 걸린다.
	 */
	const APawn* LocalPawn = PlayerController->GetPawn();
	if (!LocalPawn)
	{
		return;
	}

	// 되돌리기는 0 이다. 랙 보정은 서버가 과거의 클라이언트 입력을 판정할 때 쓰는 값인데,
	// 이 판정은 각자 자기 화면에서 지금 상황으로 내리는 것이라 되돌릴 시점이 없다.
	float TimeToImpact = 0.f;
	if (!OwnerVehicle->PredictTimeToImpact(LocalPawn, TimeToImpact, /* ViewRewindSeconds */ 0.f))
	{
		return;
	}

	if (TimeToImpact > ApproachWarningLeadSeconds)
	{
		return;
	}

	/**
	 * 소리가 실제로 났는지와 무관하게 표시한다.
	 *
	 * 성공했을 때만 표시하면, 애셋이 없거나 하는 이유로 실패하는 동안 매 틱 다시 시도한다.
	 * "임계값을 넘었다" 는 사실 자체가 한 번뿐인 사건이므로 그 시점에 닫는다.
	 */
	bWarnedLocalPlayer = true;

	SpawnHorn();
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

	StartLoopSounds();
}

void UCWVehicleAudioComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 차량이 파괴되면 붙은 소리도 같이 사라지지만, 레벨 전환처럼 파괴가 아닌 종료도 있다.
	if (IsValid(EngineAudio))
	{
		EngineAudio->Stop();
	}

	// 사이렌은 루프라 여기서 세우지 않으면 끝날 계기가 없다.
	if (IsValid(SirenAudio))
	{
		SirenAudio->Stop();
	}

	EngineAudio = nullptr;
	SirenAudio = nullptr;

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

	if (bEnableApproachWarning)
	{
		UpdateApproachWarning();
	}

	if (bEnableDoppler)
	{
		UpdateDoppler(DeltaTime);
	}
}

void UCWVehicleAudioComponent::StartLoopSounds()
{
	EngineAudio = SpawnAttachedLoop(EngineLoopSound);
	SirenAudio = SpawnAttachedLoop(SirenSound);
}

UAudioComponent* UCWVehicleAudioComponent::SpawnAttachedLoop(USoundBase* Sound) const
{
	if (!Sound)
	{
		return nullptr;
	}

	/**
	 * 페이드는 걸지 않는다. 루프 사운드의 뚝 소리는 코드가 아니라 애셋 설정 문제였다
	 * (2026-09-20 실기):
	 *
	 *   - 루프 한 바퀴마다 나는 소리 → SoundWave 의 압축을 PCM 으로. Bink 의 프라이밍
	 *     샘플이 샘플 단위로 맞춰 둔 루프 지점을 어긋나게 하고 있었다
	 *   - 멀어질 때 끊기는 소리 → 감쇠의 최대 어테뉴에이션이 -45dB 이라 그 음량에서
	 *     그냥 잘리고 있었다. -60 으로 내리면 잘려도 안 들린다
	 *
	 * UAudioComponent::FadeIn 은 여기서 쓰면 안 된다. PlayInternal 을 불러 재생을 처음부터
	 * 다시 시작하는데, SpawnSoundAttached 가 이미 틀어 놓은 뒤라 없던 뚝 소리를 만든다.
	 * 램프가 정말 필요해지면 AdjustVolume 을 쓴다.
	 */
	return UGameplayStatics::SpawnSoundAttached(
		Sound,
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

bool UCWVehicleAudioComponent::ComputeDopplerPitch(float Intensity, float& OutPitch) const
{
	FVector ListenerLocation = FVector::ZeroVector;
	FVector ListenerVelocity = FVector::ZeroVector;
	if (!ResolveListener(ListenerLocation, ListenerVelocity))
	{
		return false;
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
		return false;
	}

	const float RawPitch = 1.f / Denominator;

	// 세기는 "1 로부터 얼마나 떨어졌나"에 곱한다. 그래야 정지 중인 차의 피치가 1 로 남는다.
	OutPitch = FMath::Clamp(
		((RawPitch - 1.f) * Intensity) + 1.f,
		DopplerPitchMin,
		DopplerPitchMax
	);

	return true;
}

void UCWVehicleAudioComponent::UpdateDoppler(float DeltaTime)
{
	// 경적은 여기서 제외한다. 시작 피치를 SpawnHorn 이 정하고 끝까지 그대로 간다.
	ApplyLoopDoppler(EngineAudio, DopplerIntensity, CurrentDopplerPitch, DeltaTime);
	ApplyLoopDoppler(SirenAudio, SirenDopplerIntensity, CurrentSirenDopplerPitch, DeltaTime);
}

void UCWVehicleAudioComponent::ApplyLoopDoppler(UAudioComponent* LoopAudio, float Intensity, float& InOutPitch, float DeltaTime)
{
	if (!IsValid(LoopAudio))
	{
		return;
	}

	float TargetPitch = InOutPitch;
	if (!ComputeDopplerPitch(Intensity, TargetPitch))
	{
		return;
	}

	InOutPitch = FMath::FInterpTo(InOutPitch, TargetPitch, DeltaTime, DopplerSmoothingSpeed);

	LoopAudio->SetPitchMultiplier(InOutPitch);
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
