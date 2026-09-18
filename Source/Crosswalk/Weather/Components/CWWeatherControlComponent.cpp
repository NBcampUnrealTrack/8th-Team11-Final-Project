// Copyright Crosswalk Team. All Rights Reserved.
// CWWeatherControlComponent.cpp

#include "Weather/Components/CWWeatherControlComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraComponent.h"
#include "Weather/Components/CWWeatherComponent.h"

UCWWeatherControlComponent::UCWWeatherControlComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(false);
}

void UCWWeatherControlComponent::BeginPlay()
{
	Super::BeginPlay();

	if (WeatherComponent == nullptr)
	{
		WeatherComponent = GetOwner()->FindComponentByClass<UCWWeatherComponent>();
	}

	if (WeatherComponent != nullptr)
	{
		WeatherComponent->OnWeatherStateChanged.AddDynamic(this, &UCWWeatherControlComponent::HandleWeatherStateChanged);
	}

	if (TargetNiagaraComponent != nullptr)
	{
		FollowHeightZ = TargetNiagaraComponent->GetComponentLocation().Z;
	}
}

void UCWWeatherControlComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (TargetNiagaraComponent == nullptr)
	{
		return;
	}

	if (bFollowLocalCamera)
	{
		UpdateCameraFollow();
	}

	UpdateNiagaraParameters(DeltaTime);
}

void UCWWeatherControlComponent::HandleWeatherStateChanged(const FCWWeatherEventPayload& Payload)
{
	// 지금까지 블렌드해온 값에서부터 새 목표로 다시 Lerp를 시작한다 - 중간에 상태가 또
	// 바뀌어도 값이 끊기지 않고 이어서 흐른다.
	BlendStartWindSpeed = CurrentWindSpeed;
	BlendStartIntensity = CurrentIntensity;

	TargetWindSpeed = Payload.WindSpeed;
	TargetIntensity = Payload.WeatherIntensity;

	BlendDuration = FMath::Max(0.01f, Payload.BlendTime);
	BlendElapsed = 0.f;
}

void UCWWeatherControlComponent::UpdateCameraFollow()
{
	const APlayerController* LocalController = GetWorld()->GetFirstPlayerController();

	if (LocalController == nullptr || LocalController->PlayerCameraManager == nullptr)
	{
		return;
	}

	const FVector CameraLocation = LocalController->PlayerCameraManager->GetCameraLocation();
	const FRotator CameraRotation = LocalController->PlayerCameraManager->GetCameraRotation();

	// 카메라 "눈" 위치가 아니라 카메라가 실제로 바라보는 지점(화면 중심 근방)에 스폰 중심을
	// 맞춘다 - 눈 위치는 화면 프러스텀의 꼭짓점이라 거기 중심을 잡으면 화면과 어긋난다.
	const FVector FollowCenter = CameraLocation + CameraRotation.Vector() * CameraFollowForwardOffset;
	TargetNiagaraComponent->SetWorldLocation(FVector(FollowCenter.X, FollowCenter.Y, FollowHeightZ));

	// 카메라가 아래를 볼수록, 보는 방향으로 비가 기울어 보이게 한다 (연출용 - 게임플레이 무관)
	const FVector2D CameraForwardHorizontal = FVector2D(CameraRotation.Vector().X, CameraRotation.Vector().Y).GetSafeNormal();
	const float DownwardPitch = FMath::Max(-CameraRotation.Pitch, 0.f);
	const FVector2D CameraTilt = CameraForwardHorizontal * (DownwardPitch * CameraTiltPerPitchDegree);

	TargetNiagaraComponent->SetVariableFloat(FName("CameraTiltX"), CameraTilt.X);
	TargetNiagaraComponent->SetVariableFloat(FName("CameraTiltY"), CameraTilt.Y);
}

void UCWWeatherControlComponent::UpdateNiagaraParameters(float DeltaTime)
{
	BlendElapsed = FMath::Min(BlendElapsed + DeltaTime, BlendDuration);
	const float Alpha = BlendDuration > 0.f ? BlendElapsed / BlendDuration : 1.f;

	CurrentWindSpeed = FMath::Lerp(BlendStartWindSpeed, TargetWindSpeed, Alpha);
	CurrentIntensity = FMath::Lerp(BlendStartIntensity, TargetIntensity, Alpha);

	const float SpawnRate = BaseSpawnRate * CurrentIntensity;
	const float FallSpeedScale = FMath::Lerp(MinFallSpeedScale, MaxFallSpeedScale, CurrentIntensity);
	const float SwayStrength = CurrentWindSpeed * SwayStrengthPerWindSpeed;
	const float WindPush = CurrentWindSpeed * WindPushPerWindSpeed;

	TargetNiagaraComponent->SetVariableFloat(FName("SpawnRate"), SpawnRate);
	TargetNiagaraComponent->SetVariableFloat(FName("FallSpeedScale"), FallSpeedScale);
	TargetNiagaraComponent->SetVariableFloat(FName("SwayStrength"), SwayStrength);
	TargetNiagaraComponent->SetVariableFloat(FName("WindPush"), WindPush);
}
