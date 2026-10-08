// Copyright Crosswalk Team. All Rights Reserved.
// CWWeatherControlComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Structs/CWWeatherEventPayload.h"
#include "CWWeatherControlComponent.generated.h"

class UCWWeatherComponent;
class UNiagaraComponent;

/**
 * 날씨 연출을 실제로 반영하는 클라이언트 전용 컴포넌트. Replicated로 두지 않는다 -
 * 서버가 확정한 상태(UCWWeatherComponent)를 구독만 하고, 값 자체는 각 클라이언트가
 * 로컬로 Lerp해서 Niagara 파라미터에 반영한다. 서버 판정과 클라 연출을 분리하는
 * 이 프로젝트의 일반 원칙을 그대로 따른 것 - 여기서 하는 일은 전부 "연출"이라
 * 클라이언트마다 살짝 달라도 게임플레이에 영향이 없다.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWWeatherControlComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWWeatherControlComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// 값을 반영할 Niagara 컴포넌트를 코드에서 배정 (같은 액터가 생성자에서 미리 묶어줄 때 사용)
	void SetTargetNiagaraComponent(UNiagaraComponent* InNiagaraComponent)
	{
		TargetNiagaraComponent = InNiagaraComponent;
	}

private:
	// UCWWeatherComponent::OnWeatherStateChanged 구독 콜백 - 새 목표값으로 Lerp를 시작
	UFUNCTION()
	void HandleWeatherStateChanged(const FCWWeatherEventPayload& Payload);

	// 매 틱: 카메라 위치 추종 + 카메라 하향 기울임 계산
	void UpdateCameraFollow();

	// 매 틱: 블렌드 진행 + Niagara User Parameter 반영
	void UpdateNiagaraParameters(float DeltaTime);

protected:
	// 값을 실제로 반영할 Niagara 컴포넌트. 액터/블루프린트에서 배정한다 (이 컴포넌트가 직접 만들지 않음)
	UPROPERTY(EditAnywhere, Category = "Weather")
	TObjectPtr<UNiagaraComponent> TargetNiagaraComponent;

	// 상태를 구독할 대상. 비워두면 같은 액터에서 자동으로 찾는다
	UPROPERTY(EditAnywhere, Category = "Weather")
	TObjectPtr<UCWWeatherComponent> WeatherComponent;

	// WeatherIntensity 1.0 기준 SpawnRate. Niagara 시스템 애셋의 User.SpawnRate 기본값과 반드시 맞춰야 한다
	// (에셋 쪽 값을 바꾸면 여기도 같이 바꿀 것 - 자동으로 동기화되지 않는다).
	UPROPERTY(EditDefaultsOnly, Category = "Weather")
	float BaseSpawnRate = 200.f;

	UPROPERTY(EditDefaultsOnly, Category = "Weather")
	float MinFallSpeedScale = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Weather")
	float MaxFallSpeedScale = 1.4f;

	UPROPERTY(EditDefaultsOnly, Category = "Weather")
	float SwayStrengthPerWindSpeed = 0.05f;

	UPROPERTY(EditDefaultsOnly, Category = "Weather")
	float WindPushPerWindSpeed = 0.3f;

	// 카메라가 아래를 볼수록 비가 카메라 진행방향으로 기우는 정도 (각도 1도당)
	UPROPERTY(EditDefaultsOnly, Category = "Weather")
	float CameraTiltPerPitchDegree = 1.5f;

	// 카메라를 따라다닐 때 Z(높이)는 고정한다. BeginPlay에서 액터의 원래 높이로 캐싱
	UPROPERTY(EditDefaultsOnly, Category = "Weather")
	bool bFollowLocalCamera = true;

	/**
	 * 스폰 중심을 카메라 위치가 아니라 "카메라가 바라보는 방향으로 이만큼 앞"의 지점으로 잡는다.
	 *
	 * 이 프로젝트 카메라는 CameraBoom(TargetArmLength=1200)으로 캐릭터 뒤에서 내려다보는
	 * 고정 쿼터뷰라, 카메라 자신의 위치(눈 위치)에 스폰 구체를 중심 잡으면 실제 보이는
	 * 화면(카메라가 바라보는 앞쪽)과 어긋나 비가 한쪽으로 쏠려 보인다 - 카메라 눈 위치가
	 * 화면 프러스텀의 꼭짓점이지 중심이 아니기 때문. 카메라 앞으로 이 거리만큼 투영한
	 * 지점을 중심으로 잡아야 실제 보이는 영역과 맞는다. 기본값은 TargetArmLength와 동일하게
	 * 맞춰뒀다 - 카메라 세팅이 바뀌면 이 값도 같이 맞출 것.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Weather")
	float CameraFollowForwardOffset = 1200.f;

private:
	float CurrentWindSpeed = 0.f;
	float CurrentIntensity = 0.f;

	float TargetWindSpeed = 0.f;
	float TargetIntensity = 0.f;

	float BlendElapsed = 0.f;
	float BlendDuration = 0.f;

	float BlendStartWindSpeed = 0.f;
	float BlendStartIntensity = 0.f;

	float FollowHeightZ = 0.f;
};
