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

	if (!TargetLane || !HasSpawnableVehicleClass())
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] TargetLane이 없거나 스폰 가능한 차종이 없어 스폰을 시작하지 않는다."), *GetName());
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
	return SpawnVehicleOfClass(PickVehicleClass(), StartDistance);
}

TSubclassOf<ABaseVehicle> ATrafficVehicleSpawner::PickVehicleClass() const
{
	float TotalWeight = 0.f;
	for (const FTrafficVehicleEntry& Entry : VehicleMix)
	{
		if (Entry.VehicleClass)
		{
			TotalWeight += FMath::Max(0.f, Entry.Weight);
		}
	}

	// 목록이 비었거나 가중치가 전부 0이면 단일 차종으로 떨어진다.
	if (TotalWeight <= 0.f)
	{
		return VehicleClass;
	}

	float Roll = FMath::FRandRange(0.f, TotalWeight);
	for (const FTrafficVehicleEntry& Entry : VehicleMix)
	{
		if (!Entry.VehicleClass || Entry.Weight <= 0.f)
		{
			continue;
		}

		Roll -= Entry.Weight;
		if (Roll <= 0.f)
		{
			return Entry.VehicleClass;
		}
	}

	// 부동소수점 누적 오차로 끝까지 도달한 경우.
	return VehicleMix.Last().VehicleClass;
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
	const TSubclassOf<ABaseVehicle> SpawnClass = PickVehicleClass();

	// 입구에 앞차가 아직 남아 있으면 이번 차례는 건너뛴다. 겹쳐 스폰하는 것보다 낫다.
	if (SpawnClass && !IsLaneEntranceBlocked(SpawnClass))
	{
		SpawnVehicleOfClass(SpawnClass, 0.f);
	}

	GetWorldTimerManager().SetTimer(SpawnTimerHandle, this, &ATrafficVehicleSpawner::HandleSpawnTimer, RollSpawnInterval(), false);
}

ABaseVehicle* ATrafficVehicleSpawner::SpawnVehicleOfClass(TSubclassOf<ABaseVehicle> SpawnClass, float StartDistance)
{
	if (!HasAuthority() || !TargetLane || !SpawnClass)
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

	// 스폰에도 위치와 회전만 넘긴다. 트랜스폼째로 넘기면 차선 액터의 스케일이 그대로 박혀서
	// 차량 BP 가 정한 크기가 무시된다. (자세한 이유는 ABaseVehicle::UpdateTransformOnLane 참고)
	const FTransform LaneTransform = TargetLane->GetTransformAtDistance(StartDistance);

	ABaseVehicle* Vehicle = World->SpawnActor<ABaseVehicle>(
		SpawnClass, LaneTransform.GetLocation(), LaneTransform.Rotator(), SpawnParams);
	if (Vehicle)
	{
		Vehicle->InitializeOnLane(TargetLane, RollSpawnParams(StartDistance));
		LastSpawnedVehicle = Vehicle;
	}

	return Vehicle;
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

	// 어떤 차종 조합이 뽑힐지 미리 알 수 없으므로 가장 큰 차종 기준으로 간격을 잡는다.
	// 승용차만 나오면 다소 헐거워 보이지만 겹치는 것보다 낫다.
	const float LargestLength = GetLargestVehicleLength();
	const float RequiredGap = GetRequiredCenterGap(LargestLength, LargestLength);

	// 차선 길이에 물리적으로 들어갈 수 있는 대수. 초과 요청은 겹침으로 이어지므로 잘라낸다.
	const int32 MaxCount = (RequiredGap > 0.f) ? FMath::FloorToInt(LaneLength / RequiredGap) - 1 : PrewarmVehicleCount;
	const int32 Count = FMath::Clamp(PrewarmVehicleCount, 0, FMath::Max(0, MaxCount));

	if (Count < PrewarmVehicleCount)
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] 차선 길이 %.0fcm에는 %d대까지만 배치 가능하다. %d대 요청 중 %d대만 배치한다."),
			*GetName(), LaneLength, Count, PrewarmVehicleCount, Count);
	}

	if (Count <= 0)
	{
		return;
	}

	const float SlotSize = LaneLength / (Count + 1);
	// 지터가 인접 슬롯의 최소 간격을 침범하면 차들이 겹친다. 그만큼만 흔든다.
	const float MaxJitter = FMath::Max(0.f, (SlotSize - RequiredGap) * 0.5f);

	// 먼 쪽부터 스폰해 LastSpawnedVehicle이 입구에 가장 가까운 차가 되게 한다.
	for (int32 Index = Count; Index >= 1; --Index)
	{
		const float SlotCenter = SlotSize * Index;
		const float Distance = SlotCenter + FMath::FRandRange(-MaxJitter, MaxJitter);

		SpawnVehicle(FMath::Clamp(Distance, 0.f, LaneLength));
	}
}

float ATrafficVehicleSpawner::RollSpawnInterval() const
{
	const float Min = FMath::Min(SpawnIntervalMin, SpawnIntervalMax);
	const float Max = FMath::Max(SpawnIntervalMin, SpawnIntervalMax);

	return FMath::FRandRange(Min, Max);
}

bool ATrafficVehicleSpawner::HasSpawnableVehicleClass() const
{
	if (VehicleClass)
	{
		return true;
	}

	for (const FTrafficVehicleEntry& Entry : VehicleMix)
	{
		if (Entry.VehicleClass && Entry.Weight > 0.f)
		{
			return true;
		}
	}

	return false;
}

float ATrafficVehicleSpawner::GetVehicleLengthOf(TSubclassOf<ABaseVehicle> InClass) const
{
	if (!InClass)
	{
		return 0.f;
	}

	// CDO에서 읽으면 차종 BP의 콜리전 크기가 그대로 반영된다.
	const ABaseVehicle* VehicleTemplate = InClass->GetDefaultObject<ABaseVehicle>();

	return VehicleTemplate ? VehicleTemplate->GetVehicleLength() : 0.f;
}

float ATrafficVehicleSpawner::GetLargestVehicleLength() const
{
	float Largest = 0.f;
	for (const FTrafficVehicleEntry& Entry : VehicleMix)
	{
		if (Entry.Weight > 0.f)
		{
			Largest = FMath::Max(Largest, GetVehicleLengthOf(Entry.VehicleClass));
		}
	}

	// 목록이 비었거나 전부 가중치 0이면 단일 차종이 나온다.
	return (Largest > 0.f) ? Largest : GetVehicleLengthOf(VehicleClass);
}

float ATrafficVehicleSpawner::GetRequiredCenterGap(float LengthA, float LengthB) const
{
	// 중심 사이 거리이므로 각 차량의 절반씩만 차지한다.
	return (LengthA + LengthB) * 0.5f + SpawnClearance;
}

bool ATrafficVehicleSpawner::IsLaneEntranceBlocked(TSubclassOf<ABaseVehicle> IncomingClass) const
{
	// 차간 거리 유지 로직 덕에 추월이 없으므로, 가장 최근 스폰 차량이 항상 맨 뒤에 있다.
	const ABaseVehicle* Rearmost = LastSpawnedVehicle.Get();
	if (!Rearmost)
	{
		return false;
	}

	const float RequiredGap = GetRequiredCenterGap(Rearmost->GetVehicleLength(), GetVehicleLengthOf(IncomingClass));

	return Rearmost->GetDistanceAlongLane() < RequiredGap;
}
