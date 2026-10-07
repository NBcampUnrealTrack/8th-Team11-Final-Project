#include "Core/CWVisualDebugSubsystem.h"

#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/UnrealType.h"

namespace
{
	void DisableDebugFlags(UObject* Object)
	{
		if (!IsValid(Object)) { return; }
		for (TFieldIterator<FBoolProperty> It(Object->GetClass()); It; ++It)
		{
			const FString Name = It->GetName();
			// Include inherited flags and Blueprint overrides; keep gameplay highlighting intact.
			if (Name.StartsWith(TEXT("bShowDebug")) || Name.StartsWith(TEXT("bDrawDebug")))
			{
				It->SetPropertyValue_InContainer(Object, false);
			}
		}
	}
}

bool UCWVisualDebugSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCWVisualDebugSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	for (TActorIterator<AActor> It(&InWorld); It; ++It) { DisableActorDebug(*It); }
	// Player pawns spawned after world startup need the same flags cleared.
	ActorSpawnedHandle = InWorld.AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &UCWVisualDebugSubsystem::DisableActorDebug));
}

void UCWVisualDebugSubsystem::DisableActorDebug(AActor* Actor)
{
	if (!IsValid(Actor)) { return; }
	DisableDebugFlags(Actor);
	TInlineComponentArray<UActorComponent*> Components(Actor);
	for (UActorComponent* Component : Components) { DisableDebugFlags(Component); }
}

void UCWVisualDebugSubsystem::RemoveSpawnHandler()
{
	if (ActorSpawnedHandle.IsValid() && GetWorld())
	{
		GetWorld()->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	}
	ActorSpawnedHandle.Reset();
}

void UCWVisualDebugSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	RemoveSpawnHandler();
	Super::OnWorldEndPlay(InWorld);
}

void UCWVisualDebugSubsystem::Deinitialize()
{
	RemoveSpawnHandler();
	Super::Deinitialize();
}
