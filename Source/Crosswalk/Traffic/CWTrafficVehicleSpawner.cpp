// CWTrafficVehicleSpawner.cpp

#include "CWTrafficVehicleSpawner.h"

#include "EngineUtils.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "TimerManager.h"
#include "CWBaseVehicle.h"
#include "CWTrafficLane.h"
#include "CWTrafficSubsystem.h"
#include "Traffic/CWTrafficLog.h"

ACWTrafficVehicleSpawner::ACWTrafficVehicleSpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	/**
	 * 복제하지 않는다. 스폰은 서버 단독이고 이 액터가 클라이언트에 알릴 값이 없다.
	 *
	 * 예전에는 bReplicates 가 켜져 있었는데 복제 프로퍼티가 0 개라 스포너마다 액터 채널만
	 * 하나씩 열려 있었다. 차량의 Owner 가 이 스포너인 것은 서버 안에서만 쓰이는 관계라
	 * (관련성은 bNetUseOwnerRelevancy 가 꺼져 있어 Owner 를 보지 않는다) 클라이언트에
	 * 스포너가 없어도 차량 복제에는 영향이 없다.
	 */
	bReplicates = false;
}

void ACWTrafficVehicleSpawner::StartSpawning()
{
	if (!HasAuthority())
	{
		return;
	}

	if (!TargetLane || !HasSpawnableVehicleClass())
	{
		UE_LOG(LogCWTraffic, Warning, TEXT("[%s] TargetLane이 없거나 스폰 가능한 차종이 없어 스폰을 시작하지 않는다."), *GetName());
		return;
	}

	/**
	 * 시작했다는 것도 로그로 남긴다.
	 *
	 * 실패할 때만 찍으면 "어느 스포너가 어느 차선에 실제로 돌고 있는가" 를 알 방법이
	 * 없다. 스포너를 복제하면 TargetLane 이 원본 것을 그대로 물고 오는데, 그 상태에서는
	 * 경고가 하나도 안 뜨면서 한 차선에만 차가 나온다 - 조용히 틀리는 배치라 이 줄이 없으면
	 * 화면만 보고 원인을 찾아야 한다.
	 */
	UE_LOG(LogCWTraffic, Log, TEXT("[%s] 스폰 시작. 차선 %s / 주기 %.1f~%.1f초"),
		*GetName(), *TargetLane->GetName(), SpawnIntervalMin, SpawnIntervalMax);

	WarnOnSharedTargetLane();

	PrewarmLane();

	// 첫 스폰까지의 대기. 랜덤을 섞지 않으면 매 플레이마다 시작 그림이 똑같아진다.
	float FirstDelay = InitialDelay;
	if (bRandomizeFirstSpawn)
	{
		FirstDelay += RollSpawnInterval();
	}

	if (FirstDelay > 0.f)
	{
		GetWorldTimerManager().SetTimer(SpawnTimerHandle, this, &ACWTrafficVehicleSpawner::HandleSpawnTimer, FirstDelay, false);
	}
	else
	{
		HandleSpawnTimer();
	}
}

void ACWTrafficVehicleSpawner::StopSpawning()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpawnTimerHandle);
	}
}

ACWBaseVehicle* ACWTrafficVehicleSpawner::RequestSpawn()
{
	const TSubclassOf<ACWBaseVehicle> SpawnClass = PickVehicleClass();

	// 입구에 앞차가 아직 남아 있으면 이번 차례는 건너뛴다. 겹쳐 스폰하는 것보다 낫다.
	if (!SpawnClass || IsLaneEntranceBlocked(SpawnClass))
	{
		return nullptr;
	}

	return SpawnVehicleOfClass(SpawnClass, 0.f);
}

ACWBaseVehicle* ACWTrafficVehicleSpawner::SpawnVehicle(float StartDistance)
{
	return SpawnVehicleOfClass(PickVehicleClass(), StartDistance);
}

TSubclassOf<ACWBaseVehicle> ACWTrafficVehicleSpawner::PickVehicleClass() const
{
	float TotalWeight = 0.f;
	for (const FCWTrafficVehicleEntry& Entry : VehicleMix)
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
	for (const FCWTrafficVehicleEntry& Entry : VehicleMix)
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

void ACWTrafficVehicleSpawner::WarnOnSharedTargetLane() const
{
	const UWorld* World = GetWorld();
	if (!World || !TargetLane)
	{
		return;
	}

	/**
	 * 같은 차선을 보는 스포너가 또 있으면 알린다.
	 *
	 * 스포너를 복제해서 만들면 TargetLane 이 원본 것을 그대로 가리킨다. 그러면 두 스포너가
	 * 같은 차선에 차를 밀어넣는데, IsLaneEntranceBlocked 가 겹치는 것만 막아 주므로
	 * 크래시도 겹침도 없다 - 대신 다른 차선은 영원히 비어 있고 "저 줄에만 차가 나온다" 로 보인다.
	 */
	for (TActorIterator<ACWTrafficVehicleSpawner> It(World); It; ++It)
	{
		const ACWTrafficVehicleSpawner* Other = *It;
		if (Other == this || !Other || Other->TargetLane != TargetLane)
		{
			continue;
		}

		UE_LOG(LogCWTraffic, Warning,
			TEXT("[%s] TargetLane(%s)이 %s 와 같다. 두 스포너가 한 차선에 차를 넣으면 다른 차선은 비게 된다. 복제할 때 차선을 다시 지정하지 않은 것은 아닌지 확인해야 한다."),
			*GetName(), *TargetLane->GetName(), *Other->GetName());

		return;
	}
}

void ACWTrafficVehicleSpawner::SetSpawnInterval(float NewMin, float NewMax)
{
	// 하한이 상한보다 크게 들어와도 RollSpawnInterval 이 정리하므로 그대로 받는다.
	SpawnIntervalMin = FMath::Max(0.1f, NewMin);
	SpawnIntervalMax = FMath::Max(0.1f, NewMax);
}

void ACWTrafficVehicleSpawner::SetVehicleMix(const TArray<FCWTrafficVehicleEntry>& NewMix)
{
	if (NewMix.Num() == 0)
	{
		return;
	}

	VehicleMix = NewMix;
}

void ACWTrafficVehicleSpawner::BeginPlay()
{
	Super::BeginPlay();

	// 스폰은 서버 단독. 클라가 자기 차량을 만들면 화면마다 트래픽이 갈린다.
	if (HasAuthority() && bAutoStart)
	{
		StartSpawning();
	}
}

void ACWTrafficVehicleSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopSpawning();

	Super::EndPlay(EndPlayReason);
}

void ACWTrafficVehicleSpawner::HandleSpawnTimer()
{
	RequestSpawn();

	GetWorldTimerManager().SetTimer(SpawnTimerHandle, this, &ACWTrafficVehicleSpawner::HandleSpawnTimer, RollSpawnInterval(), false);
}

ACWBaseVehicle* ACWTrafficVehicleSpawner::SpawnVehicleOfClass(TSubclassOf<ACWBaseVehicle> SpawnClass, float StartDistance)
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
	// 차량 BP 가 정한 크기가 무시된다. (자세한 이유는 ACWBaseVehicle::UpdateTransformOnLane 참고)
	const FTransform LaneTransform = TargetLane->GetTransformAtDistance(StartDistance);

	ACWBaseVehicle* Vehicle = World->SpawnActor<ACWBaseVehicle>(
		SpawnClass, LaneTransform.GetLocation(), LaneTransform.Rotator(), SpawnParams);
	if (Vehicle)
	{
		Vehicle->InitializeOnLane(TargetLane, RollSpawnParams(StartDistance));
		LastSpawnedVehicle = Vehicle;

		// 차선 위로 옮겨 놓은 뒤에 잰다. 스폰 직후 위치는 아직 차선 좌표가 아니다.
		LogSpawnRelevancy(*Vehicle);
	}

	return Vehicle;
}

void ACWTrafficVehicleSpawner::LogSpawnRelevancy(const ACWBaseVehicle& Vehicle) const
{
	if (!UCWTrafficSubsystem::IsNetRelevancyLoggingEnabled())
	{
		return;
	}

	UNetDriver* NetDriver = GetNetDriver();
	if (!NetDriver || NetDriver->ClientConnections.Num() == 0)
	{
		return;
	}

	const FVector SpawnLocation = Vehicle.GetActorLocation();
	const float CullDistance = FMath::Sqrt(Vehicle.GetNetCullDistanceSquared());

	for (UNetConnection* Connection : NetDriver->ClientConnections)
	{
		if (!Connection)
		{
			continue;
		}

		FString PlayerTag = GetNameSafe(Connection->OwningActor);
		if (const APlayerController* Controller = Cast<APlayerController>(Connection->OwningActor))
		{
			if (const APlayerState* State = Controller->PlayerState)
			{
				PlayerTag = State->GetPlayerName();
			}
		}

		/**
		 * 뷰타깃이 없는 커넥션은 건너뛰지 않고 그 사실을 남긴다.
		 *
		 * 아직 폰을 못 받은 상태(로딩·리스폰 중)라는 뜻인데, 그 구간에 스폰된 차량이
		 * 통째로 빠지는 것이 바로 의심하는 그림 중 하나다. 줄이 없으면 "그 시각에
		 * 스폰이 없었다" 와 구분이 안 된다.
		 *
		 * FNetViewer 는 뷰타깃을 확인하지 않고 바로 위치를 읽으므로 여기서 걸러야 한다.
		 */
		if (!Connection->OwningActor || !Connection->ViewTarget)
		{
			UE_LOG(LogCWTraffic, Log, TEXT("[TrafficNet] 스폰 %s / %s 시점없음(로딩·리스폰 중)"),
				*Vehicle.GetName(), *PlayerTag);
			continue;
		}

		const FNetViewer Viewer(Connection, 0.f);
		const float Distance = FVector::Dist(SpawnLocation, Viewer.ViewLocation);
		const bool bRelevant = Vehicle.IsNetRelevantFor(Viewer.InViewer, Viewer.ViewTarget, Viewer.ViewLocation);

		UE_LOG(LogCWTraffic, Log, TEXT("[TrafficNet] 스폰 %s / 컬거리 %.0fcm / %s 거리 %.0fcm / 관련 %s"),
			*Vehicle.GetName(), CullDistance, *PlayerTag, Distance, bRelevant ? TEXT("O") : TEXT("X"));
	}
}

FCWTrafficVehicleSpawnParams ACWTrafficVehicleSpawner::RollSpawnParams(float StartDistance) const
{
	FCWTrafficVehicleSpawnParams Params;
	Params.StartDistance = StartDistance;
	Params.LateralOffset = FMath::FRandRange(-LaneOffsetRange, LaneOffsetRange);

	// 속도는 여기서 굴리지 않고 표만 넘긴다. 어느 차체가 뽑힐지는 차량이 정하고,
	// 차종 이름은 그 차체가 들고 있기 때문이다. (ACWBaseVehicle::RollSpeedForVariant)
	Params.SpeedTable = VehicleSpeedTable;

	return Params;
}

void ACWTrafficVehicleSpawner::PrewarmLane()
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
		UE_LOG(LogCWTraffic, Warning, TEXT("[%s] 차선 길이 %.0fcm에는 %d대까지만 배치 가능하다. %d대 요청 중 %d대만 배치한다."),
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

float ACWTrafficVehicleSpawner::RollSpawnInterval() const
{
	const float Min = FMath::Min(SpawnIntervalMin, SpawnIntervalMax);
	const float Max = FMath::Max(SpawnIntervalMin, SpawnIntervalMax);

	return FMath::FRandRange(Min, Max);
}

bool ACWTrafficVehicleSpawner::HasSpawnableVehicleClass() const
{
	if (VehicleClass)
	{
		return true;
	}

	for (const FCWTrafficVehicleEntry& Entry : VehicleMix)
	{
		if (Entry.VehicleClass && Entry.Weight > 0.f)
		{
			return true;
		}
	}

	return false;
}

float ACWTrafficVehicleSpawner::GetVehicleLengthOf(TSubclassOf<ACWBaseVehicle> InClass) const
{
	if (!InClass)
	{
		return 0.f;
	}

	// CDO에서 읽으면 차종 BP의 콜리전 크기가 그대로 반영된다.
	const ACWBaseVehicle* VehicleTemplate = InClass->GetDefaultObject<ACWBaseVehicle>();

	return VehicleTemplate ? VehicleTemplate->GetVehicleLength() : 0.f;
}

float ACWTrafficVehicleSpawner::GetLargestVehicleLength() const
{
	float Largest = 0.f;
	for (const FCWTrafficVehicleEntry& Entry : VehicleMix)
	{
		if (Entry.Weight > 0.f)
		{
			Largest = FMath::Max(Largest, GetVehicleLengthOf(Entry.VehicleClass));
		}
	}

	// 목록이 비었거나 전부 가중치 0이면 단일 차종이 나온다.
	return (Largest > 0.f) ? Largest : GetVehicleLengthOf(VehicleClass);
}

float ACWTrafficVehicleSpawner::GetRequiredCenterGap(float LengthA, float LengthB) const
{
	// 중심 사이 거리이므로 각 차량의 절반씩만 차지한다.
	return (LengthA + LengthB) * 0.5f + SpawnClearance;
}

bool ACWTrafficVehicleSpawner::IsLaneEntranceBlocked(TSubclassOf<ACWBaseVehicle> IncomingClass) const
{
	// 차간 거리 유지 로직 덕에 추월이 없으므로, 가장 최근 스폰 차량이 항상 맨 뒤에 있다.
	const ACWBaseVehicle* Rearmost = LastSpawnedVehicle.Get();
	if (!Rearmost)
	{
		return false;
	}

	/**
	 * 차선을 벗어난 차량은 입구를 막지 않는다.
	 *
	 * 패링당한 차량은 차선에서 빠지면서 날아가는데, 진행 거리는 쳐낸 순간의 값에 멈춰 있다.
	 * 그 값만 보면 입구 근처에서 쳐낸 차가 소멸할 때까지 스폰이 계속 건너뛰어진다.
	 * 실제로는 차가 이미 날아가 버려서 입구가 비어 있다.
	 */
	if (Rearmost->GetCurrentLane() != TargetLane)
	{
		return false;
	}

	const float RequiredGap = GetRequiredCenterGap(Rearmost->GetVehicleLength(), GetVehicleLengthOf(IncomingClass));

	return Rearmost->GetDistanceAlongLane() < RequiredGap;
}
