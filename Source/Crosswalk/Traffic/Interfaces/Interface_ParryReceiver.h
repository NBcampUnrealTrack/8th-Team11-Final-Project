// Copyright Crosswalk Team. All Rights Reserved.
// Interface_ParryReceiver.h

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Data/Structs/CWParryAttemptContext.h"
#include "Interface_ParryReceiver.generated.h"

UINTERFACE(BlueprintType)
class CROSSWALK_API UInterface_ParryReceiver : public UInterface
{
    GENERATED_BODY()
};

class CROSSWALK_API IInterface_ParryReceiver
{
    GENERATED_BODY()

public:
    // 서버 권한 패링 판정을 위해 패링 시도 정보를 전달. 반환값은 판정 통과 여부다
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Crosswalk|Parry")
    bool ReceiveParryAttempt(const FCWParryAttemptContext& ParryAttemptContext);
};