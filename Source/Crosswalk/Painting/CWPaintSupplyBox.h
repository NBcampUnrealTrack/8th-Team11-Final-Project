// Copyright Crosswalk Team. All Rights Reserved.
// CWPaintSupplyBox.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CWPaintSupplyBox.generated.h"

class UStaticMeshComponent;

/**
 * 페인트 통을 꺼내고 반납하는 보급 지점. (기획서 6-1, 6-2)
 *
 * 이 액터는 상태를 거의 갖지 않는다. 재고가 무한이기 때문이다. (기획서 8절)
 * 재고를 유한하게 두면 "전 도구 잔량 0 + 재고 0 + 정리할 오염 남음" 에서 클리어가 막힌다.
 * 그래서 여기서 하는 일은 사실상 하나뿐이다.
 *   근처인지 판정할 기준점이 된다
 *
 * 실제 소지 개수와 들기/내려놓기 판정은 UCWSupplyCarrierComponent 가 갖는다.
 * 박스가 아니라 플레이어 쪽에 두는 이유: 통은 박스를 떠나 플레이어를 따라다니는 물건이고,
 * 판정에 필요한 정보(도구를 들었는가, 몇 개 들었는가)가 전부 플레이어 쪽에 있다.
 *
 * IInterface_Interactable 을 구현하지 않는 이유:
 * 통 들기도 이제 E 를 쓰지만(전용 키 F 는 없앴다), 후보 비교는 ACWPlayerCharacter 의
 * ResolveInteractPrompt 가 도구·상호작용 대상과 함께 한 자리에서 한다.
 *
 * 인터페이스를 달면 거리 판정이 두 군데로 쪼개진다. 박스마다 InteractRadius 가 다를 수 있어
 * 판정은 UCWSupplyCarrierComponent::FindNearestBox 가 박스 자신의 반경으로 해야 하는데,
 * 인터페이스 경로는 캐릭터의 InteractRadius 로 훑은 결과를 쓴다. 두 반경이 어긋나는 순간
 * "프롬프트는 떴는데 눌러도 안 되는" 박스가 생긴다.
 *
 * 거절 이유(더 들 수 없음 / 도구를 들고 있음)를 발행하는 경로가 보급 컴포넌트에만 있는 것도
 * 같은 이유다. CanInteract 로 걸러내면 후보에서 조용히 빠져 이유가 사라진다.
 *
 * 네트워크
 *   꺼내기 판정은 전부 서버가 한다. 박스는 판정에 참여하지 않으므로 리플리케이트할 상태가 없다.
 */
UCLASS()
class CROSSWALK_API ACWPaintSupplyBox : public AActor
{
	GENERATED_BODY()

public:
	ACWPaintSupplyBox();

	/** 이 박스에서 통을 꺼내거나 반납할 수 있는 거리(cm). */
	UFUNCTION(BlueprintPure, Category = "Supply")
	float GetInteractRadius() const { return InteractRadius; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Supply")
	TObjectPtr<UStaticMeshComponent> BoxMesh;

protected:
	// 이 반경 안에서만 통을 꺼내고 반납할 수 있다. (기획서 9절 "페인트 통 박스 상호작용 거리")
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Supply", meta = (ClampMin = "0.0"))
	float InteractRadius = 150.f;
};
