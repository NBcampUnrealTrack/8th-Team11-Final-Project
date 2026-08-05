// CWCharacter.cpp

#include "CWCharacter.h"
#include "Gameplay/CWToolPickup.h"
#include "Interaction/CWInteractable.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"

ACWCharacter::ACWCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
}

void ACWCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (const APlayerController* PC = Cast<APlayerController>(NewController))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}
}

void ACWCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ACWCharacter::OnMove);
		}
		if (InteractAction)
		{
			EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &ACWCharacter::OnInteractStarted);
		}
		if (UseToolAction)
		{
			EnhancedInput->BindAction(UseToolAction, ETriggerEvent::Started, this, &ACWCharacter::OnUseToolStarted);
			EnhancedInput->BindAction(UseToolAction, ETriggerEvent::Completed, this, &ACWCharacter::OnUseToolCompleted);
			EnhancedInput->BindAction(UseToolAction, ETriggerEvent::Canceled, this, &ACWCharacter::OnUseToolCompleted);
		}
		if (DropToolAction)
		{
			EnhancedInput->BindAction(DropToolAction, ETriggerEvent::Started, this, &ACWCharacter::OnDropToolStarted);
		}
	}
}

void ACWCharacter::OnMove(const FInputActionValue& Value)
{
	const FVector2D MoveInput = Value.Get<FVector2D>();
	if (!Controller)
	{
		return;
	}

	// Top-down / quarter-view style movement onto world ground axes.
	// Y = forward/back (W/S), X = right/left (A/D) - matches Epic's own template convention.
	if (!FMath::IsNearlyZero(MoveInput.Y))
	{
		AddMovementInput(FVector::ForwardVector, MoveInput.Y);
	}
	if (!FMath::IsNearlyZero(MoveInput.X))
	{
		AddMovementInput(FVector::RightVector, MoveInput.X);
	}
}

void ACWCharacter::OnInteractStarted()
{
	ServerRPC_Interact();
}

void ACWCharacter::OnUseToolStarted()
{
	// TEMP DEBUG - remove once Use Tool is confirmed working end to end.
	GEngine->AddOnScreenDebugMessage(200, 3.f, FColor::Yellow, TEXT("OnUseToolStarted fired (input reached the character)"));
	ServerRPC_SetUsingTool(true);
}

void ACWCharacter::OnUseToolCompleted()
{
	GEngine->AddOnScreenDebugMessage(200, 3.f, FColor::Yellow, TEXT("OnUseToolCompleted fired"));
	ServerRPC_SetUsingTool(false);
}

void ACWCharacter::OnDropToolStarted()
{
	ServerRPC_DropTool();
}

void ACWCharacter::ServerRPC_Interact_Implementation()
{
	AActor* Best = nullptr;
	float BestDistSq = InteractRange * InteractRange;

	// Distance-sweep over all actors implementing ICWInteractable rather than a collision
	// volume: works with zero project-side collision channel setup. Fine at this project's
	// scale; swap for an overlap query if the level ever holds many interactables.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Candidate = *It;
		if (!Candidate || !Candidate->Implements<UCWInteractable>())
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(Candidate->GetActorLocation(), GetActorLocation());
		if (DistSq > BestDistSq)
		{
			continue;
		}

		if (!ICWInteractable::Execute_CanInteract(Candidate, this))
		{
			continue;
		}

		Best = Candidate;
		BestDistSq = DistSq;
	}

	if (Best)
	{
		ICWInteractable::Execute_Interact(Best, this);
	}
}

void ACWCharacter::ServerRPC_SetUsingTool_Implementation(bool bNewUsing)
{
	bIsUsingTool = bNewUsing;

	// TEMP DEBUG - remove once Use Tool is confirmed working end to end.
	GEngine->AddOnScreenDebugMessage(201, 3.f, FColor::Cyan, FString::Printf(
		TEXT("Server: %s bIsUsingTool=%s  EquippedTool=%s"),
		*GetName(), bIsUsingTool ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(GetEquippedToolType())));
}

void ACWCharacter::ServerRPC_DropTool_Implementation()
{
	if (EquippedToolActor)
	{
		EquippedToolActor->Drop(GetActorLocation());
	}
}

ECWToolType ACWCharacter::GetEquippedToolType() const
{
	return EquippedToolActor ? EquippedToolActor->GetToolType() : ECWToolType::None;
}

void ACWCharacter::NotifyToolEquipped(ACWToolPickup* Tool)
{
	EquippedToolActor = Tool;
}

void ACWCharacter::NotifyToolUnequipped()
{
	EquippedToolActor = nullptr;
	bIsUsingTool = false;
}

void ACWCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACWCharacter, EquippedToolActor);
	DOREPLIFETIME(ACWCharacter, bIsUsingTool);
}
