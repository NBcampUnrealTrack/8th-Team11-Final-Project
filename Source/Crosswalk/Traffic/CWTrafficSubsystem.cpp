// Copyright Crosswalk Team. All Rights Reserved.

#include "CWTrafficSubsystem.h"

#include "Engine/World.h"
#include "CWBaseVehicle.h"
#include "CWTrafficDirector.h"
#include "CWTrafficLane.h"
#include "CWTrafficStopLine.h"
#include "Interfaces/Interface_ParryReceiver.h"

#include "Engine/NetDriver.h"
#include "Engine/PackageMapClient.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"

namespace
{
	/**
	 * 동기화 진단 로그. **기본값이 켜짐인 것은 의도다.**
	 *
	 * 이 로그는 다른 팀원과 2인 테스트를 할 때 쓰는데, 받는 사람이 설정을 먼저 해야 하면
	 * 그 단계에서 테스트가 끊긴다. 켜는 쪽이 아니라 끄는 쪽이 한 줄이어야 한다.
	 *
	 * 사건이 일어난 순간에만 찍으므로(정지·출발·색 변경) 켜 두어도 로그가 흐르지 않는다.
	 */
	TAutoConsoleVariable<bool> CVarTrafficLogSync(
		TEXT("cw.Traffic.LogSync"),
		true,
		TEXT("정지선·신호 동기화 진단 로그를 찍는다. 0 이면 끈다."),
		ECVF_Default);
}

bool UCWTrafficSubsystem::IsSyncLoggingEnabled()
{
	return CVarTrafficLogSync.GetValueOnAnyThread();
}

const TCHAR* UCWTrafficSubsystem::GetNetRoleTag(const AActor* Actor)
{
	if (!Actor)
	{
		return TEXT("?");
	}

	return Actor->HasAuthority() ? TEXT("서버") : TEXT("클라");
}

FString UCWTrafficSubsystem::GetNetIdTag(const AActor* Actor)
{
	if (!Actor)
	{
		return TEXT("(없음)");
	}

	/**
	 * 액터 이름이 아니라 네트워크 GUID 를 쓴다.
	 *
	 * 런타임에 스폰된 차량은 서버와 클라이언트에서 이름이 다르게 붙는다. 이름으로 찍으면
	 * 두 로그를 나란히 놓아도 **어느 줄이 같은 차인지 짚을 수가 없어서**, 이 로그의 목적
	 * 자체가 사라진다. GUID 는 양쪽이 같은 값을 갖는다.
	 */
	if (UNetDriver* NetDriver = Actor->GetNetDriver())
	{
		if (NetDriver->GuidCache.IsValid())
		{
			const FNetworkGUID NetGuid = NetDriver->GuidCache->GetNetGUID(Actor);
			if (NetGuid.IsValid())
			{
				return FString::Printf(TEXT("#%s"), *NetGuid.ToString());
			}
		}
	}

	// 네트워크가 없으면(싱글 PIE) 이름으로 충분하다. 맞춰 볼 상대 로그가 없다.
	return Actor->GetName();
}

UCWTrafficSubsystem* UCWTrafficSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = WorldContextObject->GetWorld();
	return World ? World->GetSubsystem<UCWTrafficSubsystem>() : nullptr;
}

void UCWTrafficSubsystem::RegisterVehicle(ACWBaseVehicle* Vehicle)
{
	if (Vehicle)
	{
		ActiveVehicles.AddUnique(Vehicle);
	}
}

void UCWTrafficSubsystem::UnregisterVehicle(ACWBaseVehicle* Vehicle)
{
	if (Vehicle)
	{
		ActiveVehicles.RemoveSingleSwap(Vehicle);
	}
}

ACWBaseVehicle* UCWTrafficSubsystem::FindVehicleAhead(const ACWBaseVehicle* Vehicle) const
{
	const ACWTrafficLane* Lane = Vehicle ? Vehicle->GetCurrentLane() : nullptr;
	if (!Lane)
	{
		return nullptr;
	}

	const float MyDistance = Vehicle->GetDistanceAlongLane();

	ACWBaseVehicle* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();

	for (const TWeakObjectPtr<ACWBaseVehicle>& VehiclePtr : ActiveVehicles)
	{
		ACWBaseVehicle* Other = VehiclePtr.Get();
		if (!Other || Other == Vehicle || Other->GetCurrentLane() != Lane)
		{
			continue;
		}

		// 나보다 앞서 있는 차량 중 가장 가까운 것.
		const float OtherDistance = Other->GetDistanceAlongLane();
		if (OtherDistance > MyDistance && OtherDistance < NearestDistance)
		{
			NearestDistance = OtherDistance;
			Nearest = Other;
		}
	}

	return Nearest;
}

ACWBaseVehicle* UCWTrafficSubsystem::TryParryWithAttempt(const FCWParryAttemptContext& ParryAttemptContext)
{
	// 시도한 액터가 없으면 충돌 예측 기준점이 없어 판정 자체가 성립하지 않는다.
	if (ParryAttemptContext.Parrier == nullptr)
	{
		return nullptr;
	}

	ACWBaseVehicle* BestVehicle = nullptr;

	if (ACWBaseVehicle* DesignatedVehicle = Cast<ACWBaseVehicle>(ParryAttemptContext.TargetVehicle))
	{
		// 대상을 지정해 보냈어도 필터는 똑같이 통과해야 한다. 지정 자체는 클라가 보낸 값이다.
		float TimeToImpact = 0.f;
		if (!PassesAttemptFilter(*DesignatedVehicle, ParryAttemptContext, TimeToImpact))
		{
			return nullptr;
		}

		BestVehicle = DesignatedVehicle;
	}
	else
	{
		float BestTimeToImpact = TNumericLimits<float>::Max();

		for (const TWeakObjectPtr<ACWBaseVehicle>& VehiclePtr : ActiveVehicles)
		{
			ACWBaseVehicle* Vehicle = VehiclePtr.Get();

			float TimeToImpact = 0.f;
			if (!Vehicle || !PassesAttemptFilter(*Vehicle, ParryAttemptContext, TimeToImpact))
			{
				continue;
			}

			// 여러 대가 조건에 걸리면 가장 먼저 닿을 차를 친다.
			if (TimeToImpact < BestTimeToImpact)
			{
				BestTimeToImpact = TimeToImpact;
				BestVehicle = Vehicle;
			}
		}
	}

	if (BestVehicle == nullptr)
	{
		return nullptr;
	}

	// 수신 측이 차종 분기를 할 수 있도록 서버가 확정한 대상 정보를 채워서 넘긴다.
	FCWParryAttemptContext ResolvedContext = ParryAttemptContext;
	ResolvedContext.TargetVehicle = BestVehicle;
	ResolvedContext.VehicleType = BestVehicle->GetVehicleType();

	if (!IInterface_ParryReceiver::Execute_ReceiveParryAttempt(BestVehicle, ResolvedContext))
	{
		return nullptr;
	}

	return BestVehicle;
}

bool UCWTrafficSubsystem::PassesAttemptFilter(ACWBaseVehicle& Vehicle, const FCWParryAttemptContext& ParryAttemptContext, float& OutTimeToImpact) const
{
	OutTimeToImpact = 0.f;

	if (!Vehicle.CanBeParried())
	{
		UE_LOG(LogTemp, Log, TEXT("[Parry]   %s: 패링 불가 차종"), *Vehicle.GetName());
		return false;
	}

	// 손이 닿는 거리. 차체 표면 기준이라 차 길이가 길어도 판정이 불리해지지 않는다.
	//
	// 이 거리는 랙 보정을 하지 않고 서버의 지금 위치로 잰다. 기본값이 0(제한 없음)이라
	// 판정에 참여하지 않고, 되돌릴 거리도 윈도우 판정보다 훨씬 작다. 반경을 실제로
	// 쓰기로 하면 여기도 되돌린 위치로 재야 한다.
	const float SurfaceDistance = Vehicle.GetDistanceToPoint(ParryAttemptContext.AttemptLocation);
	if (ParryAttemptContext.ParryRadius > 0.f && SurfaceDistance > ParryAttemptContext.ParryRadius)
	{
		UE_LOG(LogTemp, Log, TEXT("[Parry]   %s: 반경 밖 (거리 %.0f > %.0f)"),
			*Vehicle.GetName(), SurfaceDistance, ParryAttemptContext.ParryRadius);
		return false;
	}

	// 커서가 향한 쪽만 친다. 쿼터뷰라 높이 차이는 보지 않는다.
	const FVector AttemptDirection = ParryAttemptContext.AttemptDirection.GetSafeNormal2D();
	const FVector ToVehicle = (Vehicle.GetActorLocation() - ParryAttemptContext.AttemptLocation).GetSafeNormal2D();
	if (!AttemptDirection.IsNearlyZero() && !ToVehicle.IsNearlyZero())
	{
		// ParryAngle 은 부채꼴 전체 각이므로 절반을 기준선으로 쓴다.
		const float HalfAngle = FMath::Clamp(ParryAttemptContext.ParryAngle, 0.f, 360.f) * 0.5f;
		const float Dot = FVector::DotProduct(AttemptDirection, ToVehicle);
		if (Dot < FMath::Cos(FMath::DegreesToRadians(HalfAngle)))
		{
			UE_LOG(LogTemp, Log, TEXT("[Parry]   %s: 각도 밖 (커서와 %.0f도 차이, 허용 %.0f도)"),
				*Vehicle.GetName(), FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f))), HalfAngle);
			return false;
		}
	}

	// 충돌 직전 윈도우 안이어야 한다. 윈도우 값은 차량이 단일 관리한다.
	// 시도한 사람이 화면에서 본 시점으로 되돌려 판정한다. (ViewRewindSeconds)
	float TimeToImpact = 0.f;
	if (!Vehicle.PredictTimeToImpact(ParryAttemptContext.Parrier, TimeToImpact, ParryAttemptContext.ViewRewindSeconds))
	{
		// 진행 경로 밖이거나 이미 지나친 차량이다. 도로 위에 서 있지 않으면 여기서 전부 걸린다.
		UE_LOG(LogTemp, Log, TEXT("[Parry]   %s: 진행 경로 밖 (또는 이미 지나침)"), *Vehicle.GetName());
		return false;
	}

	if (TimeToImpact > Vehicle.GetParryWindow())
	{
		UE_LOG(LogTemp, Log, TEXT("[Parry]   %s: 타이밍 이릅 (충돌까지 %.2f초 > 윈도우 %.2f초, 보정 %.0fms)"),
			*Vehicle.GetName(), TimeToImpact, Vehicle.GetParryWindow(), ParryAttemptContext.ViewRewindSeconds * 1000.f);
		return false;
	}

	OutTimeToImpact = TimeToImpact;

	return true;
}

void UCWTrafficSubsystem::GetVehiclesNearPoint(const FVector& Point, float Radius, TArray<ACWBaseVehicle*>& OutVehicles) const
{
	OutVehicles.Reset();

	for (const TWeakObjectPtr<ACWBaseVehicle>& VehiclePtr : ActiveVehicles)
	{
		ACWBaseVehicle* Vehicle = VehiclePtr.Get();
		if (Vehicle == nullptr)
		{
			continue;
		}

		if (Vehicle->GetDistanceToPoint(Point) <= Radius)
		{
			OutVehicles.Add(Vehicle);
		}
	}
}

void UCWTrafficSubsystem::RegisterSignalDirector(ACWTrafficDirector* Director)
{
	if (Director)
	{
		SignalDirectors.AddUnique(Director);
	}
}

void UCWTrafficSubsystem::UnregisterSignalDirector(ACWTrafficDirector* Director)
{
	SignalDirectors.RemoveAllSwap([Director](const TWeakObjectPtr<ACWTrafficDirector>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Director;
	});
}

void UCWTrafficSubsystem::RegisterStopLine(ACWTrafficStopLine* StopLine)
{
	if (StopLine)
	{
		StopLines.AddUnique(StopLine);
	}
}

void UCWTrafficSubsystem::UnregisterStopLine(ACWTrafficStopLine* StopLine)
{
	StopLines.RemoveAllSwap([StopLine](const TWeakObjectPtr<ACWTrafficStopLine>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == StopLine;
	});
}

bool UCWTrafficSubsystem::FindActiveStopLine(const ACWTrafficLane* Lane, float FromDistance, float& OutStopLineDistance) const
{
	if (!Lane)
	{
		return false;
	}

	/**
	 * 두 가지를 각자 다른 곳에 물어본다.
	 *
	 *   "여기가 정지선이다"      -> 정지선 액터 (차선에 붙는다)
	 *   "지금 빨간불인가"        -> 신호 제어기 (페이즈를 돌린다)
	 *
	 * 정지선은 차선의 속성이고 신호는 페이즈의 속성이라 소유자가 다르다.
	 * 둘을 한 곳에 묶으면 같은 차선이 여러 페이즈에 걸릴 때 정지선이 중복된다.
	 */
	bool bLaneControlled = false;
	for (const TWeakObjectPtr<ACWTrafficDirector>& DirectorPtr : SignalDirectors)
	{
		const ACWTrafficDirector* Director = DirectorPtr.Get();
		if (!Director || !Director->ControlsLane(Lane))
		{
			continue;
		}

		// 통행 신호(초록·황색)면 세우지 않는다. 제어기가 둘이면 하나라도 통행이면 지나간다.
		if (Director->IsLanePassable(Lane))
		{
			return false;
		}

		bLaneControlled = true;
	}

	// 신호가 없는 차선에 정지선만 놓아도 세우지 않는다. 세울 근거가 없다.
	if (!bLaneControlled)
	{
		return false;
	}

	bool bFound = false;
	float Nearest = 0.f;

	for (const TWeakObjectPtr<ACWTrafficStopLine>& StopLinePtr : StopLines)
	{
		const ACWTrafficStopLine* StopLine = StopLinePtr.Get();
		if (!StopLine || !StopLine->HasLane(Lane))
		{
			continue;
		}

		const float Distance = StopLine->GetDistanceAlongLane(Lane);

		// 이미 지난 정지선은 뒤로 끌어당기지 않는다. 지나갔으면 그냥 가는 것이 맞다.
		if (Distance <= FromDistance)
		{
			continue;
		}

		if (!bFound || Distance < Nearest)
		{
			Nearest = Distance;
			bFound = true;
		}
	}

	OutStopLineDistance = Nearest;

	return bFound;
}

int32 UCWTrafficSubsystem::GetActiveVehicleCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<ACWBaseVehicle>& VehiclePtr : ActiveVehicles)
	{
		if (VehiclePtr.IsValid())
		{
			++Count;
		}
	}

	return Count;
}
