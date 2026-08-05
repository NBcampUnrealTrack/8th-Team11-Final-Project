// CWToolTypes.h

#pragma once

#include "CoreMinimal.h"
#include "CWToolTypes.generated.h"

// The hand tools that exist on the crosswalk work site.
// Design rule: each tool type has exactly one physical ACWToolPickup instance in the
// world at a time, so only one character can hold a given type at once.
UENUM(BlueprintType)
enum class ECWToolType : uint8
{
	None			UMETA(DisplayName = "None"),
	Broom			UMETA(DisplayName = "Broom"),
	AsphaltPatcher	UMETA(DisplayName = "Asphalt Patcher"),
	EdgeLiner		UMETA(DisplayName = "Edge Liner"),
	Roller			UMETA(DisplayName = "Roller")
};
