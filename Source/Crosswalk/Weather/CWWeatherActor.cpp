// Copyright Crosswalk Team. All Rights Reserved.
// CWWeatherActor.cpp

#include "Weather/CWWeatherActor.h"

#include "NiagaraComponent.h"
#include "Weather/Components/CWWeatherComponent.h"
#include "Weather/Components/CWWeatherControlComponent.h"

ACWWeatherActor::ACWWeatherActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// 상태 하나(CurrentWeatherState)만 복제되면 되므로 액터 자체도 복제만 켜면 된다 -
	// 이동/회전 등을 복제할 필요가 없어 bReplicateMovement는 그대로 false(기본값)로 둔다.
	bReplicates = true;

	WeatherNiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("WeatherNiagaraComponent"));
	SetRootComponent(WeatherNiagaraComponent);

	WeatherComponent = CreateDefaultSubobject<UCWWeatherComponent>(TEXT("WeatherComponent"));

	WeatherControlComponent = CreateDefaultSubobject<UCWWeatherControlComponent>(TEXT("WeatherControlComponent"));
	WeatherControlComponent->SetTargetNiagaraComponent(WeatherNiagaraComponent);
}
