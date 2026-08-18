// Copyright Epic Games, Inc. All Rights Reserved.

#include "CWTrafficSubsystem.h"

#include "Engine/World.h"
#include "CWBaseVehicle.h"
#include "CWTrafficLane.h"
#include "Interfaces/Interface_ParryReceiver.h"

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

ACWBaseVehicle* UCWTrafficSubsystem::FindImminentThreat(const AActor* Target, float Window, float& OutTimeToImpact) const
{
	OutTimeToImpact = 0.f;

	if (!Target)
	{
		return nullptr;
	}

	ACWBaseVehicle* NearestVehicle = nullptr;
	float NearestTime = TNumericLimits<float>::Max();

	for (const TWeakObjectPtr<ACWBaseVehicle>& VehiclePtr : ActiveVehicles)
	{
		ACWBaseVehicle* Vehicle = VehiclePtr.Get();
		if (!Vehicle || !Vehicle->CanBeParried())
		{
			continue;
		}

		float TimeToImpact = 0.f;
		if (!Vehicle->PredictTimeToImpact(Target, TimeToImpact))
		{
			continue;
		}

		// Window를 넘기지 않으면 차량별 기획 값을 쓴다.
		const float EffectiveWindow = (Window > 0.f) ? Window : Vehicle->GetParryWindow();
		if (TimeToImpact > EffectiveWindow)
		{
			continue;
		}

		if (TimeToImpact < NearestTime)
		{
			NearestTime = TimeToImpact;
			NearestVehicle = Vehicle;
		}
	}

	if (NearestVehicle)
	{
		OutTimeToImpact = NearestTime;
	}

	return NearestVehicle;
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
	float TimeToImpact = 0.f;
	if (!Vehicle.PredictTimeToImpact(ParryAttemptContext.Parrier, TimeToImpact))
	{
		// 진행 경로 밖이거나 이미 지나친 차량이다. 도로 위에 서 있지 않으면 여기서 전부 걸린다.
		UE_LOG(LogTemp, Log, TEXT("[Parry]   %s: 진행 경로 밖 (또는 이미 지나침)"), *Vehicle.GetName());
		return false;
	}

	if (TimeToImpact > Vehicle.GetParryWindow())
	{
		UE_LOG(LogTemp, Log, TEXT("[Parry]   %s: 타이밍 이름 (충돌까지 %.2f초 > 윈도우 %.2f초)"),
			*Vehicle.GetName(), TimeToImpact, Vehicle.GetParryWindow());
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
