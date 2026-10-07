#include "Painting/CWPaintingSubsystem.h"

#include "Engine/World.h"
#include "Painting/Zone/CWBaseWorkZone.h"
#include "Painting/Zone/CWWorkZone_Cells.h"


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

FVector UCWPaintingSubsystem::ProjectToGround(const FVector& Location, const AActor* IgnoredActor) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return Location;
	}

	// 캐릭터 키 정도만 위아래로 훑으면 인도든 도로든 잡힌다.
	constexpr float GroundTraceHalfHeight = 200.f;

	FCollisionQueryParams QueryParams;

	if (IgnoredActor != nullptr)
	{
		QueryParams.AddIgnoredActor(IgnoredActor);
	}

	// 작업 존은 전부 뺀다. 존의 Area 박스가 ECC_Visibility 를 막고 있어서
	// 빼지 않으면 지면 대신 박스 윗면이 잡히고, 바닥에 붙어야 할 것이 공중에 뜬다.
	for (const TWeakObjectPtr<ACWBaseWorkZone>& WeakZone : ActiveZones)
	{
		if (const ACWBaseWorkZone* Zone = WeakZone.Get())
		{
			QueryParams.AddIgnoredActor(Zone);
		}
	}

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(
		Hit,
		Location + FVector(0.f, 0.f, GroundTraceHalfHeight),
		Location - FVector(0.f, 0.f, GroundTraceHalfHeight),
		ECC_Visibility,
		QueryParams))
	{
		return Hit.ImpactPoint;
	}

	return Location;
}

void UCWPaintingSubsystem::BroadcastBrushStroke(
	ACWWorkZone_Cells* SourceZone, UCWWorkerComponent* Worker, const FVector& Center, float Radius)
{
	if (Worker == nullptr)
	{
		return;
	}

	for (const TWeakObjectPtr<ACWBaseWorkZone>& WeakZone : ActiveZones)
	{
		ACWWorkZone_Cells* CellsZone = Cast<ACWWorkZone_Cells>(WeakZone.Get());
		if (CellsZone == nullptr || CellsZone == SourceZone)
		{
			continue;
		}

		// 받을 수 있는지, 무엇으로 처리할지는 받는 존이 스스로 판단한다.
		CellsZone->ReceiveForwardedBrush(Worker, Center, Radius);
	}
}

