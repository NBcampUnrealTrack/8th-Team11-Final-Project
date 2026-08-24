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
 * 그래서 여기서 하는 일은 사실상 두 가지뿐이다.
 *   1. 근처인지 판정할 기준점이 된다
 *   2. 통을 꺼낼 때 흔들리는 피드백을 전원에게 뿌린다
 *
 * 실제 소지 개수와 들기/내려놓기 판정은 UCWSupplyCarrierComponent 가 갖는다.
 * 박스가 아니라 플레이어 쪽에 두는 이유: 통은 박스를 떠나 플레이어를 따라다니는 물건이고,
 * 판정에 필요한 정보(도구를 들었는가, 몇 개 들었는가)가 전부 플레이어 쪽에 있다.
 *
 * IInterface_Interactable 을 구현하지 않는 이유:
 * 통 들기는 E(도구 집기)가 아니라 전용 키를 쓴다. (기획서 6-1) 인터페이스까지 구현하면
 * 박스 앞에서 E 를 눌렀을 때 도구를 집는 건지 통을 꺼내는 건지 갈린다.
 *
 * 네트워크
 *   꺼내기 판정은 전부 서버가 한다. 박스는 판정에 참여하지 않으므로 리플리케이트할 상태가 없고,
 *   흔들림만 멀티캐스트로 뿌린다. 놓쳐도 연출 하나가 빠질 뿐이라 Unreliable 로 둔다.
 */
UCLASS()
class CROSSWALK_API ACWPaintSupplyBox : public AActor
{
	GENERATED_BODY()

public:
	ACWPaintSupplyBox();

	/**
	 * 통을 하나 꺼냈다. 흔들림 피드백을 전원에게 뿌린다. 서버에서만 유효하다.
	 * 개수 판정은 이미 UCWSupplyCarrierComponent 가 끝낸 뒤에 불린다.
	 */
	void NotifyCanDispensed();

	/** 이 박스에서 통을 꺼내거나 반납할 수 있는 거리(cm). */
	UFUNCTION(BlueprintPure, Category = "Supply")
	float GetInteractRadius() const { return InteractRadius; }

protected:
	/**
	 * 통을 꺼내는 순간. 서버와 클라이언트 양쪽에서 호출된다.
	 * BP 자식에서 0.4초짜리 흔들림과 사운드를 넣는 자리. (기획서 6-1 처리 1항)
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Supply")
	void OnCanDispensed(float InShakeDuration);

private:
	/** 흔들림은 연출이라 놓쳐도 게임플레이에 영향이 없다. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayDispense();

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Supply")
	TObjectPtr<UStaticMeshComponent> BoxMesh;

protected:
	// 이 반경 안에서만 통을 꺼내고 반납할 수 있다. (기획서 9절 "페인트 통 박스 상호작용 거리")
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Supply", meta = (ClampMin = "0.0"))
	float InteractRadius = 150.f;

	// 통을 꺼낼 때 박스가 흔들리는 시간(초). (기획서 6-1 처리 1항)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Supply", meta = (ClampMin = "0.0"))
	float ShakeDuration = 0.4f;
};
