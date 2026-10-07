// Copyright Crosswalk Team. All Rights Reserved.
// Interface_ParryGate.h

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interface_ParryGate.generated.h"

UINTERFACE(NotBlueprintable)
class CROSSWALK_API UInterface_ParryGate : public UInterface
{
	GENERATED_BODY()
};

/**
 * 패링을 시도하는 주체가 "지금 패링할 수 있는 상태인가" 를 답한다.
 *
 * 서버 패링 판정(UCWParryComponent)이 캐릭터 구현 클래스를 직접 include 하지 않고
 * 상태를 물어보려고 둔 자리다. Traffic 은 Characters 를 모르고, 캐릭터가 이 인터페이스를
 * 구현해서 답한다. (Interface_VehicleHitReceiver 가 반대 방향으로 하는 일과 같은 구조)
 *
 * 서버가 스스로 아는 상태만 답에 넣는다. 클라이언트에만 있는 상태(작업 중 차징 등)를
 * 서버에서 물으면 항상 "가능" 으로 나와서 막는 척만 하게 된다.
 */
class CROSSWALK_API IInterface_ParryGate
{
	GENERATED_BODY()

public:
	virtual bool CanParry() const = 0;
};
