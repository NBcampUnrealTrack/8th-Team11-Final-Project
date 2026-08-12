#include "Painting/CWPaintingSubsystem.h"

#include "Engine/World.h"
#include "Painting/CWWorkerComponent.h"
#include "Painting/Zone/CWBaseWorkZone.h"

UCWPaintingSubsystem* UCWPaintingSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = WorldContextObject->GetWorld();
	return World ? World->GetSubsystem<UCWPaintingSubsystem>() : nullptr;
}

void UCWPaintingSubsystem::RegisterZone(ACWBaseWorkZone* Zone)
{
	if (Zone == nullptr)
	{
		return;
	}

	// 같은 존이 두 번 들어오면 목록을 도는 쪽에서 같은 일을 두 번 하게 된다.
	if (ActiveZones.Contains(Zone))
	{
		return;
	}

	ActiveZones.Add(Zone);

	OnWorkZoneRegistered.Broadcast(Zone);
}

void UCWPaintingSubsystem::UnregisterZone(ACWBaseWorkZone* Zone)
{
	if (Zone == nullptr)
	{
		return;
	}

	// 순서는 의미가 없다. 목록을 도는 쪽은 전부 훑기만 한다.
	if (ActiveZones.RemoveSingleSwap(Zone) > 0)
	{
		OnWorkZoneUnregistered.Broadcast(Zone);
	}
}

void UCWPaintingSubsystem::RegisterWorker(UCWWorkerComponent* Worker)
{
	if (Worker == nullptr || ActiveWorkers.Contains(Worker))
	{
		return;
	}

	ActiveWorkers.Add(Worker);
}

void UCWPaintingSubsystem::UnregisterWorker(UCWWorkerComponent* Worker)
{
	if (Worker != nullptr)
	{
		ActiveWorkers.RemoveSingleSwap(Worker);
	}
}
