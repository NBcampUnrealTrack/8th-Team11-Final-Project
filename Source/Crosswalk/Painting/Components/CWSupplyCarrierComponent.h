// Copyright Crosswalk Team. All Rights Reserved.
// CWSupplyCarrierComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Enums/CWInteractionPromptTypes.h"
#include "Data/Enums/CWSupplyTypes.h"
#include "CWSupplyCarrierComponent.generated.h"

class ACWBaseTool;
class ACWPaintDecalField;
class ACWPaintSupplyBox;
class UCWSupplyCarrierComponent;
class UCWToolHolderComponent;
class UCWWorkerComponent;

/**
 * 소지한 페인트 통 개수가 바뀔 때. 서버와 클라이언트 양쪽에서, 모든 캐릭터에 대해 발행된다.
 *
 * 가슴팍 통 메쉬와 운반 애니메이션이 붙는 자리다. (기획서 6-1 처리 3~4항)
 * 남의 캐릭터에서도 발행되므로, 다른 플레이어가 통을 몇 개 들고 있는지 그대로 보인다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnCarriedCanCountChangedSignature, UCWSupplyCarrierComponent*, CarrierComponent, int32, CarriedCount);

/**
 * 페인트 통이 터졌을 때. 서버가 확정한 뒤 모든 머신에서 같은 시점에 발행된다.
 *
 * 터진 자리에 페인트가 튄 데칼을 뿌리는 자리다. (기획서 6-4)
 * 오염 레이어의 칸은 건드리지 않는다. 정리 부담은 실제로 칠한 페인트만으로 정해진다.
 *
 *  BurstLocation 지면에 붙인 터진 지점. 피격 순간 위치이지 착지 지점이 아니다.
 *  BrokenCount   터진 통 개수. 데칼 크기를 여기에 비례시킨다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCWOnCansBurstSignature, UCWSupplyCarrierComponent*, CarrierComponent, FVector, BurstLocation, int32, BrokenCount);

/**
 * 보급 입력이 무시됐을 때. 입력을 넣은 본인 기계에서만 발행된다.
 *
 * "양손이 찼습니다" 같은 피드백이 붙는 자리다. (기획서 6-6)
 * 서버 확정을 기다리지 않고 로컬 판정 시점에 바로 나간다. 화면 표시용이라 그래도 되고,
 * 왕복을 기다리면 "왜 안 되지" 하고 몇 번 더 누른 뒤에야 뜬다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnSupplyActionRejectedSignature, UCWSupplyCarrierComponent*, CarrierComponent, ECWSupplyRejectReason, Reason);

/**
 * 통을 꺼내거나 반납하기로 정해진 순간. 입력을 넣은 본인 기계에서만, 서버 확정을 기다리지 않고 발행된다.
 *
 * 뽑기/반납 사운드가 붙는 자리다. OnCarriedCanCountChanged 는 리플리케이션 왕복을 거쳐야 해서
 * 반응이 늦고, 왕복 시간보다 빠르게 연속 입력이 들어오면 중간값이 통째로 묶여 사운드가 통째로 빠질 수 있다.
 * 이 델리게이트는 서버 확정 여부와 무관하게 클라이언트가 스스로 판단한 시점에 바로 나간다. (NotifyActionRejected 와 같은 이유)
 *
 *  bPickedUp true 면 뽑기, false 면 반납.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnCanCarriedLocallySignature, UCWSupplyCarrierComponent*, CarrierComponent, bool, bPickedUp);

/**
 * 도구 충전이 서버에서 확정됐을 때. 서버가 확정한 뒤 모든 머신에서 같은 시점에 발행된다.
 *
 * 충전 사운드가 붙는 자리다. 충전한 사람과 도구를 받는 사람이 다를 수 있어서(기획서 6-3),
 * OnCanCarriedLocally 처럼 본인 기계에만 즉시 내보내면 받는 쪽이 못 듣는다. 그래서 뽑기/반납과
 * 달리 서버 확정 후 Multicast 로 전원에게 뿌린다. (OnCansBurst 와 같은 이유)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCWOnToolChargedSignature, UCWSupplyCarrierComponent*, CarrierComponent);

/**
 * 플레이어 캐릭터에 붙이는 페인트 통 운반 컴포넌트. (기획서 6-1 ~ 6-3, 6-6)
 *
 * 하는 일
 *   1. 보급 박스에서 통을 꺼내고 반납한다 (박스 반경 안에서만)
 *   2. 근처 도구에 통을 부어 페인트를 채운다
 *   3. 도구와 통을 동시에 들 수 없게 막는다
 *
 * 3번이 이 시스템의 뼈대다. 이 제약 하나 때문에 "자기가 들고 있는 도구는 자기가 충전할 수 없다"
 * 가 별도 규칙 없이 성립한다. 손이 두 개뿐이라는 물리적 이유로 설명되므로 규칙이 하나 줄어든다.
 *
 * 통을 액터로 스폰하지 않고 개수만 세는 이유:
 * 통은 들고 있는 동안 아무 상호작용도 하지 않는다. 위치도 캐릭터에 붙어 따라다닐 뿐이다.
 * 액터로 만들면 3개마다 리플리케이션과 소유권이 생기는데 얻는 게 없다.
 * 가슴팍에 보이는 메쉬는 OnCarriedCanCountChanged 를 받아 BP 가 켜고 끈다.
 *
 * 네트워크
 *   개수 판정은 전부 서버에서 한다. 클라이언트는 "꺼내고 싶다 / 이 도구에 붓고 싶다" 고
 *   요청만 보내고, 서버가 거리를 다시 재서 승인한다. 도구 집기(UCWToolHolderComponent)와 같은 모델이다.
 *
 *   대상(박스 / 도구)을 클라이언트가 지정해서 보내는 이유는 기획서 6-3 에 적힌 그대로다.
 *   서버가 자기 기준으로 최근접을 다시 고르면, 화면에서 고른 대상과 실제 대상이 달라진다.
 */
UCLASS(ClassGroup = (Painting), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWSupplyCarrierComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWSupplyCarrierComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void BeginPlay() override;

	/**
	 * 통 들기 키. 박스 반경 안에서만 통한다. (기획서 6-1)
	 * 클라이언트에서 부르면 서버에 요청만 보낸다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Supply")
	void PickUpCan();

	/**
	 * 통 내려놓기 키. 박스 반경 안에서만 통한다. (기획서 6-2)
	 *
	 * 아무 데서나 내려놓을 수 없는 것이 솔로와 협동을 가르는 지점이다.
	 * 통이 손에 남아 있으면 도구를 집을 수 없어서, 혼자 보급할 때는 1개만 들고 오는 것이 유리해진다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Supply")
	void DropCan();

	/**
	 * 페인트 충전 키. 사거리 안에서 가장 가까운 도구를 채운다. (기획서 6-3)
	 *
	 * 대상은 남이 들고 있는 도구든 바닥에 놓인 도구든 가리지 않는다. 둘 다 그냥 "도구" 다.
	 * 자기가 든 도구는 대상에 오를 수 없다. 통을 들고 있으면 도구를 들고 있을 수 없기 때문이다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Supply")
	void ChargeNearestTool();

	/**
	 * R 키. 도구 충전과 통 반납 중 가까운 쪽을 실행한다.
	 *
	 * 전용 키(G)를 없애고 R 하나로 합쳤다. "제일 가까운 대상이 잡힌다" 는 규칙을 E 와 똑같이
	 * 쓰기 위해서다.
	 *
	 * 도구를 먼저 보는 우선순위를 두지 않는 이유: 잔량이 가득 찬 도구도 충전 대상이라
	 * (아래 CanChargeTool 참고) 도구를 먼저 보면 박스 옆에 도구 하나만 굴러다녀도
	 * 반납이 영영 막힌다. 거리로 가르면 규칙이 하나로 통일되고 화면 프롬프트가 결과를 미리 보여준다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Supply")
	void ChargeOrReturn();

	/**
	 * R 이 지금 향할 대상을 고른다.
	 *
	 * 실행(ChargeOrReturn)과 화면 프롬프트가 같은 답을 쓰도록 판정을 이 함수 하나에만 둔다.
	 * 두 곳에서 따로 재면 화면에 뜬 대상과 실제로 잡히는 대상이 갈린다.
	 *
	 * @param OutTarget 고른 대상. None 을 반환할 때는 건드리지 않는다.
	 * @return 고른 동작. 대상이 없으면 None.
	 */
	UFUNCTION(BlueprintPure, Category = "Supply")
	ECWInteractionPromptAction ResolveChargeOrReturnTarget(AActor*& OutTarget) const;

	/**
	 * E 로 지금 통을 꺼낼 수 있는 박스. 없으면 nullptr.
	 *
	 * 거리·양손·소지 한도를 전부 보므로, 여기서 나온 박스는 눌렀을 때 반드시 성공한다.
	 * 프롬프트를 "범위 안" 이 아니라 "실제로 잡힐 대상" 에만 띄우기 위한 함수다.
	 */
	ACWPaintSupplyBox* FindPickUpBox() const;

	/**
	 * 사거리 안에 보급 박스가 있는가.
	 *
	 * 잡을 대상이 하나도 없을 때 거절 이유를 보급 쪽이 발행할지 도구 쪽이 발행할지 고르는 데 쓴다.
	 * 박스 앞에서 막힌 것과 아무 데서나 막힌 것은 이유가 다르다.
	 */
	UFUNCTION(BlueprintPure, Category = "Supply")
	bool IsNearSupplyBox() const;

	/**
	 * 들고 있던 통을 전부 터뜨리고 터진 연출을 뿌린다. 차량에 치였을 때 부른다. (기획서 6-4)
	 * 서버에서만 유효하다.
	 *
	 * 오염 레이어의 칸은 채우지 않는다. 그림은 데칼이 맡고, 정리 부담은 실제로 칠한 페인트만으로 정해진다.
	 *
	 * @param SpillLocation 피격 순간의 위치. 착지 지점이 아니다.
	 *                      굴러간 끝에 생기면 위치가 매번 달라지고,
	 *                      차도 한복판을 정리하러 들어가는 위험이 사라진다.
	 */
	void BreakCansAndSpill(const FVector& SpillLocation);

	/**
	 * 들고 있던 통을 전부 없앤다. 오염은 만들지 않는다.
	 * 서버에서만 유효하다.
	 *
	 * @return 없앤 통 개수
	 */
	int32 BreakAllCans();

	/**
	 * 보급 입력이 무시됐다고 알린다. 도구 집기를 막은 UCWToolHolderComponent 도 이걸 쓴다.
	 *
	 * 피드백 창구를 하나로 모으기 위해서다. 컴포넌트마다 따로 델리게이트를 두면
	 * UI 가 양쪽에 붙어야 하고, 같은 "양손이 찼다" 를 두 곳에서 그리게 된다.
	 */
	void NotifyActionRejected(ECWSupplyRejectReason Reason);

	UFUNCTION(BlueprintPure, Category = "Supply")
	int32 GetCarriedCount() const { return CarriedCount; }

	UFUNCTION(BlueprintPure, Category = "Supply")
	bool IsCarrying() const { return CarriedCount > 0; }

	UFUNCTION(BlueprintPure, Category = "Supply")
	int32 GetMaxCarriedCount() const { return MaxCarriedCount; }

private:
	/** 터진 통 연출을 전원에게 뿌린다. 연출이라 놓쳐도 게임플레이에 영향이 없다. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayCanBurst(FVector_NetQuantize BurstLocation, int32 BrokenCount);

	/** 충전 사운드를 전원에게 뿌린다. 이 5개 사운드 중 제일 자주 들려야 해서 Reliable 로 둔다. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayChargeSound();

	/**
	 * 이 사고를 맡을 자국 필드. 없으면 nullptr.
	 *
	 * 사고 지점을 품는 필드를 고른다. 스테이지에 필드를 여러 개 나눠 배치할 수 있어서다.
	 * 먼저 찾은 것을 그냥 쓰면, 다른 필드 위에서 난 사고가 그 필드의 Area 밖이라는 이유로
	 * 통째로 버려진다. (터지긴 하는데 바닥에 아무것도 안 남는다)
	 *
	 * 어느 필드에도 안 들어가는 지점이면 처음 찾은 필드를 넘긴다. 거기서 다시 걸러져
	 * 아무 자국도 남지 않는다. 인도나 맵 밖에서 터진 사고가 여기에 해당한다.
	 */
	ACWPaintDecalField* FindDecalField(const FVector& BurstLocation) const;

	UFUNCTION(Server, Reliable)
	void Server_PickUpCan(ACWPaintSupplyBox* RequestedBox);

	UFUNCTION(Server, Reliable)
	void Server_DropCan();

	UFUNCTION(Server, Reliable)
	void Server_ChargeTool(ACWBaseTool* RequestedTool);

	UFUNCTION()
	void OnRep_CarriedCount();

	/** 서버에서 실제로 통을 꺼낸다. */
	void PerformPickUp(ACWPaintSupplyBox* RequestedBox);

	/** 서버에서 실제로 통을 반납한다. */
	void PerformDrop();

	/** 서버에서 실제로 충전한다. */
	void PerformCharge(ACWBaseTool* RequestedTool);

	/** 사거리 안에서 가장 가까운 보급 박스. 없으면 nullptr. */
	ACWPaintSupplyBox* FindNearestBox(float RadiusScale = 1.f) const;

	/** 충전할 수 있는 도구 중 가장 가까운 것. 없으면 nullptr. */
	ACWBaseTool* FindNearestChargeableTool(float RadiusScale = 1.f) const;

	/** 이 도구를 충전할 수 있는가. 빗자루와 내가 든 도구는 제외된다. */
	bool CanChargeTool(const ACWBaseTool* Tool) const;

	/** 다운 중이거나 미니게임 중이면 보급 입력을 받지 않는다. (기획서 6-7) */
	bool IsSupplyInputBlocked() const;

	void SetCarriedCount(int32 NewCount);

	/** 값이 실제로 바뀌었을 때만 델리게이트를 발행한다. 모든 머신이 각자 부른다. */
	void HandleCarriedCountChanged();

	bool IsLocallyControlled() const;

public:
	// 한 번에 들 수 있는 통 개수. (기획서 9절 "최대 소지 개수")
	//
	// 2 인 이유: 통 1개가 도구 잔량 절반이라 2개가 도구 하나를 꽉 채우는 양이다.
	// 소지량이 "도구 한 자루분" 으로 딱 떨어져서, 누구에게 줄지 고르면 그걸로 한 번 왕복이 끝난다.
	// 3 이면 한 번 다녀와 여러 명을 채울 수 있어 보급 왕복 자체가 헐거워졌다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Supply", meta = (ClampMin = "1"))
	int32 MaxCarriedCount = 2;

	// 도구를 충전할 수 있는 거리(cm). (기획서 9절 "도구 충전 거리")
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Supply", meta = (ClampMin = "0.0"))
	float ChargeRadius = 150.f;

	// 통 1개가 도구에 채워주는 페인트 양. (기획서 9절 "충전량")
	//
	// 50 인 이유: 도구 최대 잔량이 100 이라 통 2개가 작업 존 1개분이 된다.
	// 100 으로 올리면 통 3개로 팀 전체가 만땅이 되어 "누구에게 먼저 줄까" 판단이 사라진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Supply", meta = (ClampMin = "0.0"))
	float ChargeAmountPerCan = 50.f;

protected:
	// 서버가 요청을 검증할 때 사거리에 곱하는 여유값.
	// 클라이언트와 서버의 위치가 핑만큼 어긋나 있어서, 딱 맞게 재면 정상 요청도 거절된다.
	// (UCWToolHolderComponent::ServerRangeTolerance 와 같은 이유, 같은 값)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Supply", meta = (ClampMin = "1.0"))
	float ServerRangeTolerance = 1.6f;

	// 소지 개수를 화면 좌상단에 띄운다. 보급 HUD 가 생기면 끄면 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Supply|Debug")
	bool bShowDebugCarriedCount = false;

private:
	// 지금 들고 있는 통 개수. 판정은 서버가 하고 값만 전원에게 전파된다.
	// 남의 캐릭터 가슴팍에도 통이 보여야 하므로 모두에게 보낸다.
	UPROPERTY(ReplicatedUsing = OnRep_CarriedCount)
	int32 CarriedCount = 0;

	// 마지막으로 알린 개수. 이것과 다를 때만 델리게이트가 나간다.
	int32 LastBroadcastCount = -1;

	// 같은 프레임에 박스를 두 번 찾지 않으려고 결과를 들고 있는다.
	//
	// 프롬프트 갱신 한 번에 FindPickUpBox(E 후보)와 ResolveChargeOrReturnTarget(R 후보)이
	// 둘 다 FindNearestBox 를 부른다. 반경 600cm 오버랩이라 두 번 돌릴 이유가 없고,
	// 하필 통을 들고 박스 앞에 서 있을 때 — 이 기능이 제일 많이 쓰이는 자리에서 — 겹친다.
	//
	// 프레임 안에서는 캐릭터가 움직이지 않으므로 답이 달라지지 않는다.
	mutable uint64 CachedNearestBoxFrame = 0;

	// 약참조로 드는 이유: 캐시가 살아 있는 동안 박스가 파괴되면 강참조는 허상이 된다.
	mutable TWeakObjectPtr<ACWPaintSupplyBox> CachedNearestBox;

	UPROPERTY(Transient)
	TObjectPtr<UCWToolHolderComponent> ToolHolderComponent;

	UPROPERTY(Transient)
	TObjectPtr<UCWWorkerComponent> WorkerComponent;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Supply")
	FCWOnCarriedCanCountChangedSignature OnCarriedCanCountChanged;

	UPROPERTY(BlueprintAssignable, Category = "Supply")
	FCWOnSupplyActionRejectedSignature OnSupplyActionRejected;

	UPROPERTY(BlueprintAssignable, Category = "Supply")
	FCWOnCanCarriedLocallySignature OnCanCarriedLocally;

	UPROPERTY(BlueprintAssignable, Category = "Supply")
	FCWOnCansBurstSignature OnCansBurst;

	UPROPERTY(BlueprintAssignable, Category = "Supply")
	FCWOnToolChargedSignature OnToolCharged;
};
