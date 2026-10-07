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

/**
 * 차에 치일 수 있는 대상이 피격을 받는 창구.
 *
 * 차량 쪽(UCWVehicleHitDetectionComponent)이 캐릭터 구현 클래스를 직접 include 하지 않고
 * 피격을 넘기려고 둔 자리다. Traffic 은 Characters 를 모르고, 캐릭터가 이 인터페이스를 구현한다.
 * (Interface_ParryGate 가 반대 방향으로 하는 일과 같은 구조)
 */
class CROSSWALK_API IInterface_VehicleHitReceiver
{
	GENERATED_BODY()

public:
	// 차량 충돌 정보를 서버 권한 피격 처리로 전달
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Traffic|Hit")
	void ReceiveVehicleHit(const FCWVehicleHitContext& HitContext);
};
