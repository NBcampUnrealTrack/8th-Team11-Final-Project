// Copyright Crosswalk Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CWVehicleMeshVariant.generated.h"

class UStaticMesh;

/**
 * 같은 차종 안에서 외형만 바꾸는 변형 하나.
 *
 * 메쉬와 콜리전 크기를 함께 들고 있다. 박스를 하나로 고정하면 폭이 넓은 차는 옆구리
 * 판정이 비고, 좁은 차는 스치기만 해도 맞는다. 같은 팩에서 가져온 승용차라도 폭이
 * 178~253cm 로 벌어져서 하나로 덮을 수 없다.
 *
 * 거동이 다른 차(길이가 확연히 긴 밴, 느린 트럭)는 여기 넣지 말고 별도 차종 BP 로 만든다.
 * 이 구조체는 어디까지나 "같은 차종의 다른 외형" 을 위한 것이다.
 */
USTRUCT(BlueprintType)
struct FCWVehicleMeshVariant
{
	GENERATED_BODY()

	/** 씌울 메쉬. 비어 있으면 이 칸을 뽑은 차량은 BP 기본 메쉬 그대로 달린다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	TObjectPtr<UStaticMesh> Mesh;

	/**
	 * 이 메쉬일 때 쓸 콜리전 박스 익스텐트(cm).
	 *
	 * 메쉬 실측 치수의 절반을 넣되 폭은 5cm 정도 깎는다. 스치듯 지나갔는데 맞는 오차가
	 * 제일 비싸기 때문이다. 반대 방향 오차(맞을 뻔했는데 안 맞음)는 억울하지 않다.
	 *
	 * 0 이면 건드리지 않는다. 크기가 같은 변형만 넣은 차종은 BP 값을 그대로 쓰면 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FVector BoxExtent = FVector::ZeroVector;
};
