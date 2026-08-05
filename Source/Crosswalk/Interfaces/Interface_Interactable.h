// Interface_Interactable.h

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interface_Interactable.generated.h"

// Implemented by anything a character can act on via the Interact input:
// tool pickups, the signal light, the store counter, etc.
UINTERFACE(BlueprintType, MinimalAPI)
class UInterface_Interactable : public UInterface
{
	GENERATED_BODY()
};

class CROSSWALK_API IInterface_Interactable
{
	GENERATED_BODY()

public:
	// Runs on the server when a character presses Interact and this is the closest valid target.
	UFUNCTION(BlueprintNativeEvent, Category = "Crosswalk|Interaction")
	void Interact(APawn* Instigator);

	// Lets the server filter out targets that look close enough but aren't actually usable
	// right now (e.g. a tool already held by someone else).
	UFUNCTION(BlueprintNativeEvent, Category = "Crosswalk|Interaction")
	bool CanInteract(APawn* Instigator) const;
};
