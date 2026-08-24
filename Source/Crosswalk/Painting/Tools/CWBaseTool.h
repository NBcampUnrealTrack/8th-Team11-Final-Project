#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/Enums/CWToolTypes.h"
#include "Interfaces/Interface_Interactable.h"
#include "CWBaseTool.generated.h"

class ACWBaseTool;
class APawn;
class UStaticMeshComponent;

/**
 * 도구의 페인트 잔량이 바뀔 때. 서버와 클라이언트 양쪽에서, 모든 도구에 대해 발행된다.
 *
 * HUD 와 도구 위 게이지가 붙는 자리다. PaintRatio 는 0~1 로 정규화된 값이라
 * 게이지 채움에 그대로 넣으면 된다.
 *
 * 남의 도구 잔량까지 전부 발행하는 이유는 체온과 같다. (기획서 5-5)
 * "쟤 곧 떨어지겠다" 가 보여야 누구에게 먼저 보급할지 판단이 생긴다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCWOnToolPaintChangedSignature, ACWBaseTool*, Tool, float, PaintAmount, float, PaintRatio);

/**
 * 집어서 들고 다니는 작업 도구의 공통 부모.
 *
 * 도구 자체는 "누가 들고 있는가"와 "어느 작업에 쓰이는가(ToolType)"만 안다.
 * 어떤 존을 칠할 수 있는지는 존의 AcceptedTools 가 판단하고,
 * 1인 1개 제한과 집기/놓기 조작은 UCWToolHolderComponent 가 맡는다.
 *
 * 라인 테두리 도구는 이 클래스를 상속한 BP_Tool_Line 을 만들고
 * ToolType 만 Liner 로 바꾸면 끝난다. C++ 자식 클래스가 필요하지 않다.
 *
 * TryPickUp / Drop 을 BlueprintCallable 로 열지 않은 이유:
 * 홀더를 거치지 않고 직접 부르면 홀더의 HeldTool 과 상태가 어긋난다. 입구를 하나로 둔다.
 *
 * 네트워크
 *   Holder 만 리플리케이트하고, 손에 붙는 것 자체는 언리얼의 어태치먼트 리플리케이션이 처리한다.
 *   콜리전과 하이라이트는 리플리케이트되지 않으므로 OnRep_Holder 에서 다시 맞춘다.
 *   TryPickUp / Drop 은 서버에서만 실행된다. 클라이언트는 홀더 컴포넌트의 RPC 를 거친다.
 *
 * IInterface_Interactable 을 구현하는 이유:
 *   신호등이나 상점 카운터처럼 도구가 아닌 상호작용 대상이 생겼을 때
 *   같은 인터페이스로 묶으려면 도구도 그 계약을 따라야 한다.
 *   도구를 집는 경로는 두 개가 되지만(홀더의 E 입력, 범용 상호작용) 둘 다
 *   UCWToolHolderComponent::PickUpTool 로 모이므로 상태가 갈라지지 않는다.
 */
UCLASS()
class CROSSWALK_API ACWBaseTool : public AActor, public IInterface_Interactable
{
	GENERATED_BODY()

public:
	ACWBaseTool();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void BeginPlay() override;

	/** 범용 상호작용 진입점. 이 폰의 도구 소지 컴포넌트에 집기를 맡긴다. */
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;

	/** 이미 누가 들고 있으면 상호작용 대상에서 빠진다. */
	virtual bool CanInteract_Implementation(APawn* InstigatorPawn) const override;

	/**
	 * 이 도구를 집는다. 이미 다른 플레이어가 들고 있으면 실패한다.
	 * 서버에서만 유효하다. 두 명이 같은 도구를 집는 경합은 여기서 걸러진다.
	 * @return 집기에 성공하면 true
	 */
	bool TryPickUp(APawn* NewHolder);

	/** 지정한 위치에 내려놓는다. 서버에서만 유효하다. */
	void Drop(const FVector& DropLocation);

	/** 지금 E 를 누르면 집힐 대상인지. 외곽선 하이라이트에 쓴다. */
	void SetFocused(bool bNewFocused);

	/**
	 * 작업한 만큼 페인트를 쓴다. (기획서 5-3)
	 *
	 * 서버/클라이언트를 가리지 않고 부른 기계에서 바로 깎는다. 붓질과 같은 모델이다.
	 * 서버 왕복을 기다리면 그 사이 클라이언트가 더 칠하고, 서버가 그만큼을 거부하면
	 * 칸은 칠해졌는데 잔량만 남는 상태로 갈라진다. 값이 어긋나도 리플리케이션이 덮는다.
	 *
	 * @return 실제로 깎인 양. 잔량이 모자라면 남아 있던 만큼만 깎인다.
	 */
	float ConsumePaint(float Amount);

	/**
	 * 페인트 통으로 채운다. 서버에서만 유효하다. (기획서 6-3)
	 *
	 * 소모와 달리 서버 권위로 두는 이유: 충전은 통을 소비하는 1회성 행동이라
	 * 예측했다가 서버가 거절하면 통이 사라졌다 되살아나는 그림이 나온다.
	 *
	 * @return 실제로 채워진 양. 최대치를 넘는 만큼은 버려진다.
	 */
	float AddPaint(float Amount);

	/** 이 도구가 페인트를 쓰는가. 빗자루는 false 라 잔량 개념이 없다. (기획서 5-2) */
	UFUNCTION(BlueprintPure, Category = "Tool|Paint")
	bool UsesPaint() const { return bUsesPaint; }

	/** 지금 이 도구로 작업할 수 있는가. 페인트를 쓰지 않는 도구는 언제나 true 다. */
	UFUNCTION(BlueprintPure, Category = "Tool|Paint")
	bool HasPaint() const { return !bUsesPaint || PaintAmount > 0.f; }

	UFUNCTION(BlueprintPure, Category = "Tool|Paint")
	float GetPaintAmount() const { return PaintAmount; }

	/** 0 = 빈 통, 1 = 가득. 게이지 채움에 그대로 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Tool|Paint")
	float GetPaintRatio() const;

	UFUNCTION(BlueprintPure, Category = "Tool|Paint")
	float GetMaxPaintAmount() const { return MaxPaintAmount; }

	UFUNCTION(BlueprintPure, Category = "Tool")
	ECWToolType GetToolType() const { return ToolType; }

	UFUNCTION(BlueprintPure, Category = "Tool")
	APawn* GetHolder() const { return Holder; }

	UFUNCTION(BlueprintPure, Category = "Tool")
	bool IsHeld() const { return Holder != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Tool")
	FText GetDisplayName() const { return DisplayName; }

protected:
	/** 집힌 직후. BP 자식에서 사운드/파티클을 붙이는 자리. */
	UFUNCTION(BlueprintNativeEvent, Category = "Tool")
	void OnPickedUp();
	virtual void OnPickedUp_Implementation();

	/** 내려놓은 직후. */
	UFUNCTION(BlueprintNativeEvent, Category = "Tool")
	void OnDropped();
	virtual void OnDropped_Implementation();

	UFUNCTION(BlueprintNativeEvent, Category = "Tool")
	void OnFocusChanged(bool bNewFocused);
	virtual void OnFocusChanged_Implementation(bool bNewFocused);

	/** 잔량이 바뀐 직후. BP 자식에서 도구 위 게이지를 갱신하는 자리. */
	UFUNCTION(BlueprintNativeEvent, Category = "Tool|Paint")
	void OnPaintAmountChanged(float NewAmount, float NewRatio);
	virtual void OnPaintAmountChanged_Implementation(float NewAmount, float NewRatio);

private:
	UFUNCTION()
	void OnRep_Holder();

	UFUNCTION()
	void OnRep_PaintAmount();

	/** 값이 실제로 바뀌었을 때만 연출과 델리게이트를 돌린다. 모든 머신이 각자 부른다. */
	void HandlePaintAmountChanged();

	/** 잔량을 범위 안으로 넣고, 바뀌었으면 알린다. @return 실제 변화량 */
	float SetPaintAmount(float NewAmount);

	/** 리플리케이트되지 않는 것들(콜리전, 하이라이트, 연출)을 소지 상태에 맞춘다. */
	void HandleHeldStateChanged();

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tool")
	TObjectPtr<UStaticMeshComponent> ToolMesh;

protected:
	// 이 도구가 어떤 작업 단계에 쓰이는지. BP_Tool_Line 은 Liner 로 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool")
	ECWToolType ToolType = ECWToolType::None;

	// HUD 프롬프트에 띄울 이름. 예) "라인 테두리 도구"
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool")
	FText DisplayName;

	// 손에 붙일 소켓 이름. 캐릭터 메쉬에 이 소켓이 없으면 메쉬 원점에 붙는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool")
	FName AttachSocketName = TEXT("hand_r");

	// 소켓 기준 보정값. 도구 메쉬마다 쥐는 위치와 각도가 달라서 따로 잡아줘야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool")
	FVector AttachRelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool")
	FRotator AttachRelativeRotation = FRotator::ZeroRotator;

	// 내려놓을 때 지면에서 띄우는 높이(cm). 메쉬 원점이 바닥에 맞춰져 있으면 0 이어도 된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool")
	float DropHeightOffset = 5.f;

	// 이 도구가 페인트를 소모하는가. 빗자루(BP_Tool_Broom)만 false 로 둔다. (기획서 5-2)
	//
	// false 면 잔량을 아예 보지 않는다. 충전 대상에서도 빠지고, 잔량 0 으로 작업이 막히지도 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Paint")
	bool bUsesPaint = true;

	// 가득 채웠을 때의 잔량. (기획서 9절 밸런싱 표 "도구 최대 페인트 잔량")
	//
	// 100 인 이유: 작업 구역의 완주 소요 페인트 기본값과 같은 값이다. 그래서 안쪽만 정확히
	// 칠하면 가득 찬 도구 하나로 구역 하나가 딱 끝나고, 삐져나간 만큼만 모자라진다.
	// 한쪽만 바꾸면 그 관계가 깨지므로 ACWBaseWorkZone::PaintCostFull 과 같이 봐야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Paint", meta = (ClampMin = "1.0"))
	float MaxPaintAmount = 100.f;

private:
	// 지금 이 도구를 들고 있는 플레이어. nullptr 이면 바닥에 놓인 상태다.
	UPROPERTY(ReplicatedUsing = OnRep_Holder)
	TObjectPtr<APawn> Holder;

	/**
	 * 남은 페인트. 플레이어가 아니라 도구가 들고 있다. (기획서 5-1)
	 *
	 * 도구를 넘겨주면 잔량도 함께 넘어가고, 바닥에 놓여도 그대로 유지된다.
	 * 어태치먼트만 옮겨 다니고 이 값은 액터에 남으므로 따로 승계 처리를 하지 않아도 된다.
	 *
	 * 시작값은 BeginPlay 에서 MaxPaintAmount 로 채운다. (기획서 8절)
	 */
	UPROPERTY(ReplicatedUsing = OnRep_PaintAmount)
	float PaintAmount = 0.f;

	// 마지막으로 알린 잔량. 이것과 다를 때만 델리게이트가 나간다.
	float LastBroadcastPaintAmount = -1.f;

	// 하이라이트는 화면 표시용이라 클라이언트마다 따로 계산한다. 리플리케이트하지 않는다.
	bool bFocused = false;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Tool|Paint")
	FCWOnToolPaintChangedSignature OnPaintChanged;
};
