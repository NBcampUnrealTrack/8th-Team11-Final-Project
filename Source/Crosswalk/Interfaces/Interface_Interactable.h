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

	// 상호작용 거리를 재는 기준점. 기본값은 액터 원점이다.
	//
	// 원점이 실제로 다가서는 지점과 멀리 떨어진 대상이 있어서 열어 둔다. 작업차량이 그렇다.
	// 차가 길어서 트렁크 앞에 서 있어도 원점까지는 3~4 m 나오고, 그러면 발밑에 있는
	// 다른 후보(도구 등)에게 우선순위를 빼앗긴다.
	UFUNCTION(BlueprintNativeEvent, Category = "Crosswalk|Interaction")
	FVector GetInteractionLocation() const;
	virtual FVector GetInteractionLocation_Implementation() const;
};
