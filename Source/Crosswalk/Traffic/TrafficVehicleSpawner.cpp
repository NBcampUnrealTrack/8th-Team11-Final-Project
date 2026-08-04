// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrafficVehicleSpawner.h"

#include "Engine/World.h"
#include "TimerManager.h"
#include "BaseVehicle.h"
#include "TrafficLane.h"

ATrafficVehicleSpawner::ATrafficVehicleSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
}

void ATrafficVehicleSpawner::StartSpawning()
{
	if (!HasAuthority())
	{
		return;
	}

	if (!TargetLane || !VehicleClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] TargetLane 또는 VehicleClass가 비어 있어 스폰을 시작하지 않는다."), *GetName());
		return;
	}

	PrewarmLane();

	// 첫 스폰까지의 대기. 랜덤을 섞지 않으면 매 플레이마다 시작 그림이 똑같아진다.
	float FirstDelay = InitialDelay;
	if (bRandomizeFirstSpawn)
	{
		FirstDelay += RollSpawnInterval();
	}

	if (FirstDelay > 0.f)
	{
		GetWorldTimerManager().SetTimer(SpawnTimerHandle, this, &ATrafficVehicleSpawner::HandleSpawnTimer, FirstDelay, false);
	}
	else
	{
		HandleSpawnTimer();
	}
}

void ATrafficVehicleSpawner::StopSpawning()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpawnTimerHandle);
	}
}

ABaseVehicle* ATrafficVehicleSpawner::SpawnVehicle(float StartDistance)
{
	if (!HasAuthority() || !TargetLane || !VehicleClass)
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	// 차선 시작점이 다른 액터와 겹쳐도 트래픽 흐름이 끊기지 않도록 그대로 스폰한다.
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ABaseVehicle* Vehicle = World->SpawnActor<ABaseVehicle>(VehicleClass, TargetLane->GetTransformAtDistance(StartDistance), SpawnParams);
	if (Vehicle)
	{
		Vehicle->InitializeOnLane(TargetLane, RollSpawnParams(StartDistance));
	}

	return Vehicle;
}

void ATrafficVehicleSpawner::BeginPlay()
{
	Super::BeginPlay();

	// 스폰은 서버 단독. 클라가 자기 차량을 만들면 화면마다 트래픽이 갈린다.
	if (HasAuthority() && bAutoStart)
	{
		StartSpawning();
	}
}

void ATrafficVehicleSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopSpawning();

	Super::EndPlay(EndPlayReason);
}

void ATrafficVehicleSpawner::HandleSpawnTimer()
{
	SpawnVehicle();

	GetWorldTimerManager().SetTimer(SpawnTimerHandle, this, &ATrafficVehicleSpawner::HandleSpawnTimer, RollSpawnInterval(), false);
}

FTrafficVehicleSpawnParams ATrafficVehicleSpawner::RollSpawnParams(float StartDistance) const
{
	FTrafficVehicleSpawnParams Params;
	Params.StartDistance = StartDistance;
	Params.SpeedMultiplier = 1.f + FMath::FRandRange(-SpeedJitter, SpeedJitter);
	Params.LateralOffset = FMath::FRandRange(-LaneOffsetRange, LaneOffsetRange);

	return Params;
}

void ATrafficVehicleSpawner::PrewarmLane()
{
	if (PrewarmVehicleCount <= 0 || !TargetLane)
	{
		return;
	}

	const float LaneLength = TargetLane->GetLaneLength();
	const float SlotSize = LaneLength / (PrewarmVehicleCount + 1);

	for (int32 Index = 0; Index < PrewarmVehicleCount; ++Index)
	{
		// 차선을 균등 분할한 지점에 지터를 섞어, 미리 깔린 차들이 등간격으로 보이지 않게 한다.
		const float SlotCenter = SlotSize * (Index + 1);
		const float Jitter = FMath::FRandRange(-SlotSize * 0.4f, SlotSize * 0.4f);

		SpawnVehicle(FMath::Clamp(SlotCenter + Jitter, 0.f, LaneLength));
	}
}

float ATrafficVehicleSpawner::RollSpawnInterval() const
{
	const float Min = FMath::Min(SpawnIntervalMin, SpawnIntervalMax);
	const float Max = FMath::Max(SpawnIntervalMin, SpawnIntervalMax);

	return FMath::FRandRange(Min, Max);
}
