// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrafficSubsystem.h"

#include "Engine/World.h"
#include "BaseVehicle.h"
#include "TrafficLane.h"

UTrafficSubsystem* UTrafficSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = WorldContextObject->GetWorld();
	return World ? World->GetSubsystem<UTrafficSubsystem>() : nullptr;
}

void UTrafficSubsystem::RegisterVehicle(ABaseVehicle* Vehicle)
{
	if (Vehicle)
	{
		ActiveVehicles.AddUnique(Vehicle);
	}
}

void UTrafficSubsystem::UnregisterVehicle(ABaseVehicle* Vehicle)
{
	if (Vehicle)
	{
		ActiveVehicles.RemoveSingleSwap(Vehicle);
	}
}

ABaseVehicle* UTrafficSubsystem::FindVehicleAhead(const ABaseVehicle* Vehicle) const
{
	const ATrafficLane* Lane = Vehicle ? Vehicle->GetCurrentLane() : nullptr;
	if (!Lane)
	{
		return nullptr;
	}

	const float MyDistance = Vehicle->GetDistanceAlongLane();

	ABaseVehicle* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();

	for (const TWeakObjectPtr<ABaseVehicle>& VehiclePtr : ActiveVehicles)
	{
		ABaseVehicle* Other = VehiclePtr.Get();
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

ABaseVehicle* UTrafficSubsystem::FindImminentThreat(const AActor* Target, float Window, float& OutTimeToImpact) const
{
	OutTimeToImpact = 0.f;

	if (!Target)
	{
		return nullptr;
	}

	ABaseVehicle* NearestVehicle = nullptr;
	float NearestTime = TNumericLimits<float>::Max();

	for (const TWeakObjectPtr<ABaseVehicle>& VehiclePtr : ActiveVehicles)
	{
		ABaseVehicle* Vehicle = VehiclePtr.Get();
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

ABaseVehicle* UTrafficSubsystem::TryParryNearestThreat(AActor* Parrier)
{
	float TimeToImpact = 0.f;
	ABaseVehicle* Threat = FindImminentThreat(Parrier, 0.f, TimeToImpact);

	if (Threat && Threat->TryParry(Parrier))
	{
		return Threat;
	}

	return nullptr;
}

int32 UTrafficSubsystem::GetActiveVehicleCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<ABaseVehicle>& VehiclePtr : ActiveVehicles)
	{
		if (VehiclePtr.IsValid())
		{
			++Count;
		}
	}

	return Count;
}
