// Copyright Crosswalk Team. All Rights Reserved.
// Interface_VehicleHitReceiver.h

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Data/Structs/CWVehicleHitContext.h"
#include "Interface_VehicleHitReceiver.generated.h"

UINTERFACE(BlueprintType)
class CROSSWALK_API UInterface_VehicleHitReceiver : public UInterface
{
    GENERATED_BODY()
};

class CROSSWALK_API IInterface_VehicleHitReceiver
{
    GENERATED_BODY()

public:
    // 차량 충돌 정보를 서버 권한 피격 처리로 전달
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Crosswalk|Vehicle Hit")
    void ReceiveVehicleHit(const FCWVehicleHitContext& HitContext);
};