// CWVehicleVariantPick.h

#pragma once

#include "CoreMinimal.h"
#include "CWVehicleVariantPick.generated.h"

/**
 * 서버가 뽑은 외형 한 벌. 차체를 먼저 고르고 그 안에서 색을 고른다.
 *
 * 두 인덱스를 따로 복제하지 않고 한 구조체로 묶는 이유
 *   프로퍼티가 둘이면 도착 순서가 보장되지 않아 OnRep 도 각각 뜬다. 차체만 먼저 닿은
 *   순간에는 색 인덱스가 아직 직전 값이라, 그 프레임에만 엉뚱한 색이 씌워진다.
 *   한 프로퍼티면 둘이 같이 오고 알림도 한 번이다.
 *
 * 2단으로 뽑는 이유는 확률이다. 94개 메쉬를 평면으로 균등 추첨하면 색이 4가지인 차체가
 * 1가지인 차체보다 네 배 자주 나온다. 그 4:1 은 아무도 정한 적이 없는, 에셋 팩이 어떤
 * 차를 몇 가지 색으로 냈는지에 불과하다. 차체를 먼저 균등하게 뽑으면 등장 빈도는
 * 스포너의 VehicleMix 가중치와 차종 BP 의 변형 목록으로만 정해진다.
 */
USTRUCT(BlueprintType)
struct FCWVehicleVariantPick
{
	GENERATED_BODY()

	/**
	 * MeshVariants 인덱스. NotPicked 면 아직 뽑지 않았다는 뜻이다.
	 *
	 * 기본값을 0 이 아니라 유효 범위 밖에 두는 이유
	 *   초기 복제에서는 CDO 기본값과 같은 값이 아예 전송되지 않고, 전송이 없으면
	 *   RepNotify 도 없다. 기본값이 0 이면 0번 차체가 뽑힌 차량만 클라이언트에서
	 *   메쉬가 바뀌지 않는다. 어떤 유효 인덱스와도 다른 값이어야 전송과 알림이 보장된다.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Traffic")
	uint8 Family = NotPicked;

	/** 그 차체의 ColorMeshes 인덱스. 색이 하나뿐인 차체는 언제나 0 이다. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Traffic")
	uint8 Color = NotPicked;

	/** 아직 뽑지 않은 상태. 한 차종에 차체를 255개 넣을 일은 없다. */
	static constexpr uint8 NotPicked = 255;

	bool IsPicked() const { return Family != NotPicked && Color != NotPicked; }
};
