// CWVehicleMeshVariant.h

#pragma once

#include "CoreMinimal.h"
#include "CWVehicleMeshVariant.generated.h"

class USkeletalMesh;
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
	 * 바퀴가 도는 차량용. 이쪽을 채우면 Mesh 대신 이것이 씌워진다. (둘 다 채우면 이쪽이 이긴다)
	 *
	 * 스켈레탈을 쓰는 이유는 바퀴 회전 하나뿐이다. 돌릴 것이 없는 차종은 Mesh 를 쓴다.
	 * 스켈레탈 메쉬는 정적 메쉬와 달리 인스턴싱 혜택을 못 받아서, 같은 차가 여러 대
	 * 깔려도 드로우콜이 대수만큼 는다. 트래픽은 동시 한 자릿수라 문제가 안 되지만
	 * 배경에 세워 둘 차량까지 이쪽으로 넣으면 그때부터 비용이 붙는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	TObjectPtr<USkeletalMesh> SkeletalMesh;

	/**
	 * 바퀴 반지름(cm). SkeletalMesh 를 쓸 때 회전 속도 환산에 들어간다.
	 *
	 * 굴림 조건으로 각속도를 낸다 - 바퀴가 원주만큼 나아가면 정확히 한 바퀴다.
	 * 값이 실제보다 작으면 바퀴가 과하게 돌아 미끄러지는 것처럼 보이고, 크면 끌리는
	 * 것처럼 보인다. 어느 쪽이든 주행 자체에는 영향이 없다. 순전히 눈에 보이는 값이다.
	 *
	 * 0 이면 차종 BP 의 WheelRadius 를 그대로 쓴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0"))
	float WheelRadius = 0.f;

	/**
	 * 이 메쉬일 때 쓸 콜리전 박스 익스텐트(cm).
	 *
	 * 메쉬 실측 치수의 절반을 넣되 폭은 5cm 정도 깎는다. 스치듯 지나갔는데 맞는 오차가
	 * 제일 비싸기 때문이다. 반대 방향 오차(맞을 뻔했는데 안 맞음)는 억울하지 않다.
	 *
	 * Z 도 마찬가지로 전체 높이의 절반이다. 박스는 이 값만큼 위로 올라가 바닥이 노면에
	 * 맞으므로(ACWBaseVehicle::ApplyCollisionExtent), Z 를 실제보다 낮게 넣으면 차 지붕
	 * 쪽에 판정이 비는 것이지 차가 도로에 박히는 것이 아니다.
	 *
	 * 0 이면 건드리지 않는다. 크기가 같은 변형만 넣은 차종은 BP 값을 그대로 쓰면 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FVector BoxExtent = FVector::ZeroVector;
};
