#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/Enums/CWToolTypes.h"
#include "Interfaces/Interface_Interactable.h"
#include "CWBaseTool.generated.h"

class APawn;
class UStaticMeshComponent;

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

private:
	UFUNCTION()
	void OnRep_Holder();

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

private:
	// 지금 이 도구를 들고 있는 플레이어. nullptr 이면 바닥에 놓인 상태다.
	UPROPERTY(ReplicatedUsing = OnRep_Holder)
	TObjectPtr<APawn> Holder;

	// 하이라이트는 화면 표시용이라 클라이언트마다 따로 계산한다. 리플리케이트하지 않는다.
	bool bFocused = false;
};
