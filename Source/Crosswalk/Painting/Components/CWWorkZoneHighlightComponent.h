#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CWWorkZoneHighlightComponent.generated.h"

class UCWToolHolderComponent;

/**
 * 소지한 도구에 맞는 작업존을 강조해서 작업 순서를 화면에 드러낸다. (작업존 하이라이트 기획서)
 *
 * 없애려는 문제
 *   순서 규칙은 이미 동작한다. WorkOrder 로 앞 단계가 안 끝나면 bLocked 가 걸린다.
 *   그런데 그 규칙이 화면에 드러나지 않아서, 플레이어에게는 비슷하게 생긴 사각 구역이
 *   나란히 있는 모습만 보인다. 작업이 안 되는 이유가 "순서가 아직" 인지 "위치가 틀림" 인지
 *   구분되지 않는다.
 *
 * 그래서 두 가지를 같이 보여준다. (ECWZoneHighlightState)
 *   Active  : 지금 이 도구로 작업할 수 있는 존
 *   Pending : 이 도구를 받지만 아직 잠긴 존   ← 이쪽이 순서를 가르치는 몫이다
 *
 * 이 컴포넌트가 하는 일은 셋뿐이다.
 *   1. 내가 조종하는 캐릭터인지 본다
 *   2. 등록된 존에게 "지금 이 도구면 너는 어떤 상태냐" 고 묻는다
 *   3. 그 답을 존에게 도로 알려준다
 *
 * 판정은 존이 한다 (ACWBaseWorkZone::GetHighlightStateFor). 여기서 새로 만드는 기준은 없다.
 * 존마다 기준이 다르기 때문이다 — 칸 존은 정리 도구와 채우기 도구를 따로 봐야 한다.
 *
 * 리티클(UCWWorkReticleComponent)과 겹치지 않는다
 *   그쪽은 붓이 닿을 자리를 원으로 그려 "여기를 지금 누르면 되는가" 를 답한다. 발밑 얘기다.
 *   이쪽은 거리와 무관하게 존 단위로 "이 도구를 어디에 쓰고 지금 차례인가" 를 답한다.
 *   같은 화면에 같이 뜨므로 Active 색은 리티클의 진행 가능 색과 같은 계열로 맞춘다.
 *
 * 완전히 로컬이다. 리플리케이트하지 않고 RPC 도 없다.
 * 판정에 쓰는 값(진행도·완료·잠금·소지 도구)이 이미 전부 복제되고 있어서, 각 머신이 같은
 * 입력을 보고 같은 답을 낸다. 따로 전파할 것이 없다.
 *
 * 표시만 하고 아무것도 바꾸지 않는다. 작업 판정(CanWorkNow)에는 손대지 않는다.
 */
UCLASS(ClassGroup = (Painting), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWWorkZoneHighlightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWWorkZoneHighlightComponent();

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 등록된 존을 전부 다시 보고 표시 상태를 갱신한다. 타이머가 RefreshInterval 마다 부른다. */
	void RefreshHighlights();

	/**
	 * 모든 존의 표시를 끈다.
	 *
	 * 조기 반환 대신 이걸 부르는 자리가 있다. 존이 상태를 들고 있기 때문에 그냥 돌아가면
	 * 마지막으로 켜둔 표시가 그대로 남는다. 도구를 내려놓거나 캐릭터가 사라져도 안 꺼진다.
	 */
	void ClearAllHighlights();

	/** 내가 조종하는 캐릭터인가. 리슨 서버에서는 남의 폰에도 이 컴포넌트가 붙어 있다. */
	bool IsLocallyControlled() const;

protected:
	// 하이라이트를 켤 것인가. 끄면 모든 존의 표시가 내려간다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Highlight")
	bool bEnabled = true;

	// 다시 보는 간격(초).
	//
	// 도구가 바뀌는 순간만 듣지 않고 주기적으로 도는 이유는 BeginPlay 주석에 있다.
	// 0.2 면 사람 눈에는 즉시 반응으로 보인다. 존 하나당 판정이 전부 O(1) 이라
	// 이 빈도는 이미 매 프레임 도는 UCWWorkerComponent::FindTargetZone 의 몇 분의 일이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Highlight", meta = (ClampMin = "0.02"))
	float RefreshInterval = 0.2f;

	// 존이 스스로 연출을 그리기 전까지 쓰는 임시 표시.
	//
	// 3단계에서 존에 바닥 데칼이 붙으면 끈다. 그때까지 이걸 꺼두면 화면이 비어서
	// 판정이 맞는지 확인할 방법이 없다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Highlight|Debug")
	bool bDrawDebugBoxes = true;

	// 지금 작업할 수 있는 존의 색.
	// 리티클의 진행 가능 색(UCWWorkReticleComponent::WorkableColor)과 같은 계열이어야 한다.
	// 두 표시가 같은 화면에 뜨는데 초록의 의미가 서로 다르면 화면이 거짓말을 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Highlight|Debug")
	FColor ActiveColor = FColor(26, 230, 51);

	// 아직 차례가 아닌 존의 색.
	//
	// 빨강을 쓰지 않는다. 리티클이 빨강을 "눌러도 진행되지 않는다" 에 이미 쓰고 있어서,
	// 여기에 또 쓰면 두 빨강의 의미가 갈린다. Pending 은 금지가 아니라 "아직" 이므로
	// 색상을 바꾸는 대신 채도를 뺀다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Highlight|Debug")
	FColor PendingColor = FColor(110, 112, 118);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Highlight|Debug", meta = (ClampMin = "0.0"))
	float DebugLineThickness = 5.f;

private:
	// 같은 캐릭터의 도구 소지 컴포넌트. BeginPlay 에서 한 번 찾아 둔다.
	// 없으면 이 컴포넌트는 놀고 있는다.
	UPROPERTY(Transient)
	TObjectPtr<UCWToolHolderComponent> ToolHolderComponent;

	FTimerHandle RefreshTimerHandle;
};
