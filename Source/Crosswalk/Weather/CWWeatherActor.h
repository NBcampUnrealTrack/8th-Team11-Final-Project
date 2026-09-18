// Copyright Crosswalk Team. All Rights Reserved.
// CWWeatherActor.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CWWeatherActor.generated.h"

class UCWWeatherComponent;
class UCWWeatherControlComponent;
class UNiagaraComponent;

/**
 * 맵에 하나 배치하는 날씨 액터. UCWWeatherComponent(서버 권위 상태)와
 * UCWWeatherControlComponent(클라 전용 연출 반영)를 묶어서 들고 있고,
 * 실제 비/눈 Niagara 컴포넌트도 여기 붙는다.
 *
 * 레벨에 직접 배치해서 쓴다 - 인스턴스별 값(WeatherDataTable, BaseSpawnRate 등)은
 * EditDefaultsOnly라 이 클래스를 상속한 Blueprint를 만들어서 그 CDO에서 설정해야
 * 실제로 반영된다 (C++ 액터를 레벨에 직접 놓고 디테일 패널에서 바꾸는 건 안 먹는다 -
 * 이전 프로젝트에서 실제로 겪은 함정. 이 문제를 피하려면 BP_WeatherActor 같은 얇은
 * Blueprint 서브클래스를 만들어서 그걸 레벨에 배치할 것).
 */
UCLASS()
class CROSSWALK_API ACWWeatherActor : public AActor
{
	GENERATED_BODY()

public:
	ACWWeatherActor();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Weather")
	TObjectPtr<UNiagaraComponent> WeatherNiagaraComponent;

	UPROPERTY(VisibleAnywhere, Category = "Weather")
	TObjectPtr<UCWWeatherComponent> WeatherComponent;

	UPROPERTY(VisibleAnywhere, Category = "Weather")
	TObjectPtr<UCWWeatherControlComponent> WeatherControlComponent;
};
