// CWToolPickup.cpp

#include "CWToolPickup.h"
#include "Character/CWCharacter.h"

#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

ACWToolPickup::ACWToolPickup()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	ToolMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ToolMesh"));
	SetRootComponent(ToolMesh);
	ToolMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void ACWToolPickup::Interact_Implementation(APawn* InstigatorPawn)
{
	ACWCharacter* NewHolder = Cast<ACWCharacter>(InstigatorPawn);
	if (!NewHolder || CurrentHolder)
	{
		return;
	}

	// Switching tools: drop whatever this character was already carrying at their feet
	// so the tool they're giving up is still exactly one instance, sitting in the world.
	if (ACWToolPickup* OldTool = NewHolder->GetEquippedToolActor())
	{
		OldTool->Drop(NewHolder->GetActorLocation());
	}

	CurrentHolder = NewHolder;
	AttachToHolder(NewHolder);
	NewHolder->NotifyToolEquipped(this);
}

bool ACWToolPickup::CanInteract_Implementation(APawn* InstigatorPawn) const
{
	return CurrentHolder == nullptr;
}

void ACWToolPickup::Drop(FVector DropLocation)
{
	ACWCharacter* OldHolder = CurrentHolder;
	CurrentHolder = nullptr;
	ReturnToWorld(DropLocation);

	if (OldHolder)
	{
		OldHolder->NotifyToolUnequipped();
	}
}

void ACWToolPickup::AttachToHolder(ACWCharacter* NewHolder)
{
	if (!NewHolder)
	{
		return;
	}

	if (ToolMesh)
	{
		ToolMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	AttachToComponent(NewHolder->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		NewHolder->GetToolHandSocket());
}

void ACWToolPickup::ReturnToWorld(FVector Location)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorLocation(Location);

	if (ToolMesh)
	{
		ToolMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
}

void ACWToolPickup::OnRep_CurrentHolder(ACWCharacter* OldHolder)
{
	// Server already did the equivalent attach/detach directly (see Interact_Implementation
	// and Drop above); this only runs the visual side on remote clients.
	if (CurrentHolder)
	{
		AttachToHolder(CurrentHolder);
	}
	else
	{
		ReturnToWorld(GetActorLocation());
	}
}

void ACWToolPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACWToolPickup, CurrentHolder);
}
