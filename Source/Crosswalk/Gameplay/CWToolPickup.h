// CWToolPickup.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CWInteractable.h"
#include "Gameplay/CWToolTypes.h"
#include "CWToolPickup.generated.h"

class ACWCharacter;
class UStaticMeshComponent;

// A single physical instance of one tool type (broom, roller, edge liner, patcher).
// Design rule: exactly one of these exists per tool type in the world, so only one
// character can hold a given tool at a time.
//
// Vehicle knockback isn't implemented yet; once it is, the collision-response code
// should call Drop(ScatterLocation) on whatever tool the hit character is holding,
// same as a voluntary drop.
UCLASS()
class CROSSWALK_API ACWToolPickup : public AActor, public ICWInteractable
{
	GENERATED_BODY()

public:
	ACWToolPickup();

	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual bool CanInteract_Implementation(APawn* InstigatorPawn) const override;

	UFUNCTION(BlueprintPure, Category = "Crosswalk|Tool")
	ECWToolType GetToolType() const { return ToolType; }

	UFUNCTION(BlueprintPure, Category = "Crosswalk|Tool")
	ACWCharacter* GetHolder() const { return CurrentHolder; }

	// Detaches from the current holder (if any) and places this tool in the world at
	// DropLocation. Server-only; call from the interact flow (voluntary drop) or from
	// vehicle-collision code (knocked out of a character's hands).
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Tool")
	void Drop(FVector DropLocation);

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, Category = "Crosswalk|Tool")
	TObjectPtr<UStaticMeshComponent> ToolMesh;

	UPROPERTY(EditAnywhere, Category = "Crosswalk|Tool")
	ECWToolType ToolType = ECWToolType::None;

private:
	void AttachToHolder(ACWCharacter* NewHolder);
	void ReturnToWorld(FVector Location);

	UFUNCTION()
	void OnRep_CurrentHolder(ACWCharacter* OldHolder);

	UPROPERTY(ReplicatedUsing = OnRep_CurrentHolder)
	TObjectPtr<ACWCharacter> CurrentHolder;
};
