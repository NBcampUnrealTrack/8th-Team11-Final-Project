// Copyright Crosswalk Team. All Rights Reserved.
// CWInteractionPromptComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Structs/CWInteractionPrompt.h"
#include "CWInteractionPromptComponent.generated.h"

class ACWPlayerCharacter;
class UCWInteractionPromptComponent;

/**
 * 머리 위에 떠 있어야 할 프롬프트 목록이 바뀔 때. 조종 중인 본인 기계에서만 발행된다.
 *
 * 값이 실제로 달라진 프레임에만 나간다. 플레이어가 가만히 서 있으면 한 번도 안 나간다.
 * UI 가 매 프레임 조회하지 않게 하려는 것이다. (ViewModel 데이터 계약 2번 칸)
 *
 *  Prompts 지금 떠 있어야 할 전부. 0~2개이며, 빈 배열이면 아무것도 그리지 않는다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnInteractionPromptsChangedSignature, UCWInteractionPromptComponent*, PromptComponent, const TArray<FCWInteractionPrompt>&, Prompts);

/**
 * 지금 E/R 이 향할 대상을 화면에 알려주는 컴포넌트. (ViewModel 데이터 계약 - 상호작용 프롬프트)
 *
 * 왜 필요한가
 *   조작키를 줄이면서 E 와 R 이 상황마다 다른 일을 하게 됐다. 특히 보급 박스 앞에서는
 *   박스 반경과 도구 충전 사거리가 둘 다 150cm 라 충전과 반납 후보가 거의 항상 겹치고,
 *   몇 cm 차이로 어느 쪽이 잡힐지가 뒤집힌다. 누르기 전에 결과가 보이지 않으면
 *   플레이어가 이 규칙을 학습할 방법이 없다. 그래서 이 컴포넌트가 설계의 전제 조건이다.
 *
 * 무엇을 하지 않는가
 *   판정을 하지 않는다. ACWPlayerCharacter 의 Resolve*Prompt 를 부르기만 한다.
 *   실제 실행(Interact / ChargePaint)도 같은 함수를 쓰므로, 화면에 뜬 대상과
 *   눌렀을 때 잡히는 대상이 갈릴 수 없다. 여기서 한 번 더 재면 그 보장이 깨진다.
 *
 * 네트워크
 *   복제하지 않는다. 남이 무엇을 잡을 수 있는지는 내 화면에 그릴 일이 없고,
 *   판정에 필요한 정보가 전부 로컬에 있다. 조종 중이 아닌 캐릭터에서는 틱 자체를 끈다.
 */
UCLASS(ClassGroup = (Interaction), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWInteractionPromptComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWInteractionPromptComponent();

	virtual void BeginPlay() override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * 지금 떠 있어야 할 프롬프트 전부. 0~2개다.
	 *
	 * 델리게이트를 놓친 뒤 따라잡을 때나 위젯이 처음 붙을 때 쓴다.
	 * 평소에는 OnInteractionPromptsChanged 만 듣고 있으면 된다.
	 */
	UFUNCTION(BlueprintPure, Category = "Interaction")
	const TArray<FCWInteractionPrompt>& GetActivePrompts() const { return ActivePrompts; }

private:
	/**
	 * 대상을 다시 고르고, 직전과 다를 때만 델리게이트를 발행한다.
	 * @return 목록이 실제로 바뀌었으면 true
	 */
	bool RefreshPrompts();

	/** 조종 중인 본인인가. 아니면 틱을 돌릴 이유가 없다. */
	bool IsLocallyControlled() const;

	/**
	 * [진단] 판정이 이 기계에서 도는지와 후보 탐색 비용을 로그로 남긴다.
	 *
	 * 두 가지를 확인하려는 것이다.
	 *   1. 멀티에서 각 머신이 자기 폰의 판정을 돌리는가 (클라이언트 로그에 시작 줄이 있는가)
	 *   2. 250cm 구체 오버랩이 얼마나 무거운가 (구간 평균/최대와 걸린 개수)
	 *
	 * UI 가 아직 안 붙어 있어서 화면으로는 1번을 확인할 방법이 없다. 그 자리를 메우는 로그다.
	 */
	void LogDiagnostics(bool bPromptsChanged);

public:
	/**
	 * 대상을 다시 고르는 주기(초). 0 이면 매 프레임.
	 *
	 * 매 프레임 돌려도 되는 비용이지만, 판정이 반경 250cm 구체 오버랩을 돌리므로
	 * 액터가 빽빽한 구역에서는 공짜가 아니다. 0.05 면 화면 반응은 즉각으로 느껴지면서
	 * 쿼리는 1/3 로 준다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = "0.0"))
	float RefreshInterval = 0.05f;

	/**
	 * [진단] 성능 요약을 찍는 간격(초).
	 *
	 * 갱신 한 번에 한 줄씩 찍으면 초당 20줄이라 로그 파일이 못 쓰게 된다.
	 * 구간으로 묶어 한 줄만 남기면, 여러 명의 로그를 나란히 놓고 읽을 수 있다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|Debug", meta = (ClampMin = "1.0"))
	float DiagnosticSummaryInterval = 5.f;

private:
	// 마지막으로 알린 목록. 이것과 다를 때만 델리게이트가 나간다.
	UPROPERTY(Transient)
	TArray<FCWInteractionPrompt> ActivePrompts;

	UPROPERTY(Transient)
	TObjectPtr<ACWPlayerCharacter> OwnerCharacter;

	// [진단] 구간 집계. 요약 한 줄을 찍고 나면 전부 0 으로 되돌린다.
	bool bDiagnosticStartLogged = false;
	double DiagnosticWindowStartSeconds = 0.0;
	int32 DiagnosticRefreshCount = 0;
	int32 DiagnosticChangeCount = 0;
	double DiagnosticQueryTotalSeconds = 0.0;
	double DiagnosticQueryMaxSeconds = 0.0;
	int32 DiagnosticOverlapTotal = 0;
	int32 DiagnosticOverlapMax = 0;
	int32 DiagnosticStaticOverlapTotal = 0;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FCWOnInteractionPromptsChangedSignature OnInteractionPromptsChanged;
};
