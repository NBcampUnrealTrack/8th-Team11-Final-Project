#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Enums/CWToolTypes.h"
#include "CWToolHolderComponent.generated.h"

class ACWBaseTool;
class UCWSupplyCarrierComponent;
class UCWWorkerComponent;

/**
 * 플레이어 캐릭터에 붙이는 도구 소지 컴포넌트. (기획서 3.1 "상호작용 / 도구 집기 (E)")
 *
 * 하는 일
 *   1. 매 프레임 근처에서 집을 수 있는 도구를 찾아 FocusedTool 로 물어둔다 (HUD 프롬프트용)
 *   2. E 를 누르면 들고 있으면 내려놓고, 없으면 FocusedTool 을 집는다 (1인 1개)
 *   3. 소지 도구가 바뀔 때마다 UCWWorkerComponent 에 알려준다
 *
 * 3번이 핵심이다. 이걸 통해 "라인 테두리 도구를 들고 있을 때만 테두리 존 게이지가 오르는"
 * 연결이 완성된다. 존 쪽 판정은 ACWBaseWorkZone::CanUseTool 이 한다.
 *
 * 네트워크
 *   집기/놓기는 전부 서버에서 판정한다. 클라이언트는 "이 도구를 집고 싶다" 고 요청만 보내고,
 *   서버가 거리를 다시 재서 승인한다. 그래서 두 명이 같은 도구를 집는 일이 생기지 않는다.
 *   FocusedTool 은 화면 표시용이라 리플리케이트하지 않고 각 클라이언트가 로컬로 계산한다.
 */
UCLASS(ClassGroup = (Painting), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWToolHolderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWToolHolderComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * E 키. Enhanced Input 의 Started 에 연결한다. 들고 있으면 내려놓고, 없으면 근처 도구를 집는다.
	 * 클라이언트에서 부르면 서버에 요청만 보낸다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tool")
	void Interact();

	/** 특정 도구를 집는다. 서버에서만 유효하다. 이미 다른 도구를 들고 있으면 실패한다. (1인 1개) */
	UFUNCTION(BlueprintCallable, Category = "Tool")
	bool PickUpTool(ACWBaseTool* Tool);

	/** 들고 있는 도구를 앞쪽 바닥에 내려놓는다. 서버에서만 유효하다. */
	UFUNCTION(BlueprintCallable, Category = "Tool")
	void DropHeldTool();

	/**
	 * 들고 있는 도구를 지정한 위치에 내려놓는다. 서버에서만 유효하다.
	 *
	 * 캐릭터가 이미 옮겨진 뒤에 떨어뜨려야 하는 경우에 쓴다. 싱크홀에 빠질 때가 그렇다.
	 * 구멍 중앙으로 끌려간 다음 앞쪽 바닥에 놓으면 도구가 구멍 안에 들어가 회수할 수 없다.
	 * 그래서 빠지기 직전 위치를 기억해 두었다가 여기로 넘긴다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tool")
	void DropHeldToolAt(const FVector& DropLocation);

	UFUNCTION(BlueprintPure, Category = "Tool")
	ACWBaseTool* GetHeldTool() const { return HeldTool; }

	UFUNCTION(BlueprintPure, Category = "Tool")
	ACWBaseTool* GetFocusedTool() const { return FocusedTool; }

	/** 들고 있는 도구의 종류. 맨손이면 None. */
	UFUNCTION(BlueprintPure, Category = "Tool")
	ECWToolType GetHeldToolType() const;

	/** 페인트 통을 들고 있어서 도구를 집을 수 없는 상태인가. (기획서 6-6) */
	UFUNCTION(BlueprintPure, Category = "Tool")
	bool IsBlockedByCarriedCans() const;

private:
	/**
	 * 클라이언트가 보낸 집기 요청. 서버에서만 실행된다.
	 * 어떤 도구를 집으려는지 클라이언트가 알려주지만, 승인 여부는 서버가 다시 판단한다.
	 */
	UFUNCTION(Server, Reliable)
	void Server_Interact(ACWBaseTool* RequestedTool);

	/** 서버에서 실제로 집기/놓기를 수행한다. */
	void PerformInteract(ACWBaseTool* RequestedTool);

	UFUNCTION()
	void OnRep_HeldTool();

	/** 집을 수 있는 도구 중 가장 가까운 것. 없으면 nullptr. */
	ACWBaseTool* FindNearestPickableTool() const;

	/** 도구를 내려놓을 지점. 캐릭터 앞쪽 지면으로 잡되, 주울 수 없는 곳이면 발밑으로 잡는다. */
	FVector CalculateDropLocation() const;

	void SetFocusedTool(ACWBaseTool* NewTool);

	/** 소지 도구를 작업 컴포넌트에 반영한다. */
	void NotifyWorkerComponent();

public:
	// 이 반경 안에 있는 도구만 집을 수 있다. (cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tool")
	float PickupRadius = 150.f;

protected:
	// 내려놓을 때 캐릭터 앞으로 밀어내는 거리(cm). 발밑에 두면 다시 집을 때 헷갈린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tool")
	float DropForwardDistance = 90.f;

	// 내려놓을 바닥이 캐릭터 발 높이와 이만큼(cm)까지만 달라도 된다.
	// 이보다 높으면 매시 윗면, 낮으면 난간 밖으로 보고 발밑에 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tool", meta = (ClampMin = "0.0"))
	float MaxDropHeightDelta = 50.f;

	// 앞으로 밀어낼 때 벽에서 띄우는 거리(cm). 도구가 벽에 파묻혀 보이지 않게 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tool", meta = (ClampMin = "0.0"))
	float DropClearanceRadius = 20.f;

	// 서버가 집기 요청을 검증할 때 PickupRadius 에 곱하는 여유값.
	// 클라이언트와 서버의 캐릭터 위치가 핑만큼 어긋나 있어서, 딱 맞게 재면 정상 요청도 거절된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tool", meta = (ClampMin = "1.0"))
	float ServerRangeTolerance = 1.6f;

private:
	UPROPERTY(ReplicatedUsing = OnRep_HeldTool)
	TObjectPtr<ACWBaseTool> HeldTool;

	// 지금 E 를 누르면 집힐 도구. 화면 표시용이라 리플리케이트하지 않고 각자 로컬로 계산한다.
	UPROPERTY(Transient)
	TObjectPtr<ACWBaseTool> FocusedTool;

	UPROPERTY(Transient)
	TObjectPtr<UCWWorkerComponent> WorkerComponent;

	// 통을 들고 있으면 도구를 집을 수 없다. (기획서 6-6) 없어도 동작한다.
	UPROPERTY(Transient)
	TObjectPtr<UCWSupplyCarrierComponent> SupplyCarrierComponent;
};
