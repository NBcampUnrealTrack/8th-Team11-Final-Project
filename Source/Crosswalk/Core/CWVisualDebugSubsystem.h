#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CWVisualDebugSubsystem.generated.h"

/** Disable authored debug flags on play-world instances without resaving team assets. */
UCLASS()
class CROSSWALK_API UCWVisualDebugSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	void DisableActorDebug(AActor* Actor);
	void RemoveSpawnHandler();

private:
	FDelegateHandle ActorSpawnedHandle;
};
