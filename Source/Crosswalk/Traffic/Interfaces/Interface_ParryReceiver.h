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
    // 서버 권한 패링 판정을 위해 패링 시도 정보를 전달
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Crosswalk|Parry")
    void ReceiveParryAttempt(const FCWParryAttemptContext& ParryAttemptContext);
};