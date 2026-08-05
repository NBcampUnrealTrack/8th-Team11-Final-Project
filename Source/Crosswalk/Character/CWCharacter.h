// CWCharacter.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Gameplay/CWToolTypes.h"
#include "CWCharacter.generated.h"

class ACWToolPickup;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

// Base playable character for the crosswalk work site.
// Owns tool-equip state and the Interact / Use Tool / Drop Tool actions that every
// hand tool (broom, edge liner, roller, asphalt patcher) hooks into.
UCLASS()
class CROSSWALK_API ACWCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ACWCharacter();

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;

	// -- Tool state, readable by anything that cares whether this character is actively working --

	UFUNCTION(BlueprintPure, Category = "Crosswalk|Tool")
	ECWToolType GetEquippedToolType() const;

	UFUNCTION(BlueprintPure, Category = "Crosswalk|Tool")
	bool IsUsingTool() const { return bIsUsingTool; }

	UFUNCTION(BlueprintPure, Category = "Crosswalk|Tool")
	ACWToolPickup* GetEquippedToolActor() const { return EquippedToolActor; }

	UFUNCTION(BlueprintPure, Category = "Crosswalk|Tool")
	FName GetToolHandSocket() const { return ToolHandSocket; }

	// Called by ACWToolPickup (server-side) once it has attached to / detached from this character,
	// so the character's own EquippedToolActor mirror stays in sync.
	void NotifyToolEquipped(ACWToolPickup* Tool);
	void NotifyToolUnequipped();

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// -- Enhanced Input assets (assign on a Blueprint child of this class) --

	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Input")
	TObjectPtr<UInputAction> UseToolAction;

	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Input")
	TObjectPtr<UInputAction> DropToolAction;

	// How far the character reaches to interact with pickups, the signal light, etc.
	// Implemented as a distance check rather than a collision volume so it works before
	// any interactable-specific collision channel/profile has been set up in the project.
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Interaction")
	float InteractRange = 150.f;

	// Socket the currently equipped tool attaches to. Must exist on whatever skeletal mesh
	// is assigned to this character once character art is in.
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Tool")
	FName ToolHandSocket = TEXT("hand_r_socket");

private:
	void OnMove(const FInputActionValue& Value);
	void OnInteractStarted();
	void OnUseToolStarted();
	void OnUseToolCompleted();
	void OnDropToolStarted();

	UFUNCTION(Server, Reliable)
	void ServerRPC_Interact();

	UFUNCTION(Server, Reliable)
	void ServerRPC_SetUsingTool(bool bNewUsing);

	UFUNCTION(Server, Reliable)
	void ServerRPC_DropTool();

	UPROPERTY(Replicated)
	TObjectPtr<ACWToolPickup> EquippedToolActor;

	UPROPERTY(Replicated)
	bool bIsUsingTool = false;
};
