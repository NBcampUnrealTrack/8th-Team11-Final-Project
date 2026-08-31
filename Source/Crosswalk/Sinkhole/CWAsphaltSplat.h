// CWAsphaltSplat.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CWAsphaltSplat.generated.h"

class UDecalComponent;
class USphereComponent;

/**
 * 싱크홀을 빗나가 바닥에 깔린 아스팔트 자국. (싱크홀 기획서 7-9)
 *
 * 밟은 플레이어는 일정 시간 최저 이속이 된다. 그래서 오조준이 단순한 탄 낭비를 넘어
 * 실제 위험이 되고, "가까이 가서 정확하게 vs 멀리서 안전하게" 라는 거리 트레이드오프에
 * 비로소 무게가 실린다. 쏜 본인을 포함한 모든 플레이어에게 적용된다.
 * 동료 앞에 일부러 쏴서 방해하는 플레이는 의도된 것이다.
 *
 * 사라질 때 투명해지지 않고 도로에 "스며드는" 것으로 표현한다.
 * 아스팔트는 식으면 굳지 증발하지 않는다. 사라지는 이유가 설명되어야 한다.
 *
 * 잔류 시간(2.9초)을 슬로우 지속시간(3초)보다 짧게 둔다.
 * 자국이 사라진 뒤에도 슬로우가 조금 남으므로 두 효과가 별개라는 것이 드러난다.
 *
 * 슬로우 재적용
 *   오버랩 이벤트는 들어올 때 한 번만 온다. 그래서 밟고 서 있는 동안 슬로우가 끝나도
 *   다시 걸리지 않는다. 짧은 주기로 겹쳐 있는 대상에게 계속 요청을 보내 해결한다.
 *   슬로우가 이미 걸려 있으면 요청은 무시되므로(UCWTemperatureComponent::ApplyExternalSlow),
 *   결과적으로 "풀리는 순간 다시 걸린다" 가 된다.
 *
 *   자국 수가 적고 주기도 느려서 비용은 무시할 수준이다. 이 방식을 택한 이유는
 *   플레이어 쪽에서 슬로우 종료를 자국에게 알리는 역방향 의존을 만들지 않기 위해서다.
 *
 * 네트워크
 *   서버가 스폰해 리플리케이트한다. 슬로우 판정도 서버에서만 한다.
 *   자국 자체는 2.9초 뒤 스스로 사라지므로 동기화할 지속 상태가 없다.
 */
UCLASS()
class CROSSWALK_API ACWAsphaltSplat : public AActor
{
	GENERATED_BODY()

public:
	ACWAsphaltSplat();

	virtual void BeginPlay() override;

protected:
	/** 스며들기 시작할 때. BP 에서 머티리얼 페이드를 돌리는 자리. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Asphalt")
	void OnSoakStarted(float SoakDuration);

private:
	/** 겹쳐 있는 대상들에게 슬로우를 다시 요청한다. 서버에서만 돈다. */
	void RefreshOverlappingSlow();

public:
	// 바닥에 깔린 아스팔트 자국.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asphalt")
	TObjectPtr<UDecalComponent> SplatDecal;

	// 밟음 판정.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asphalt")
	TObjectPtr<USphereComponent> SplatTrigger;

protected:
	// 착탄부터 완전히 스며들 때까지의 시간(초). 슬로우보다 짧게 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt", meta = (ClampMin = "0.1"))
	float LifeSpanSeconds = 2.9f;

	// 밟았을 때 슬로우가 지속되는 시간(초).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt", meta = (ClampMin = "0.1"))
	float SlowDuration = 3.f;

	// 밟음 판정 반경(cm).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt", meta = (ClampMin = "10.0"))
	float SplatRadius = 90.f;

	// 겹쳐 있는 대상에게 슬로우를 다시 요청하는 주기(초).
	//
	// 슬로우가 풀린 뒤 다시 걸리기까지의 최대 지연이 이 값이다. 너무 크면 자국 위에
	// 서 있는데 잠깐 빨라지는 구간이 보인다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt", meta = (ClampMin = "0.02"))
	float SlowRefreshInterval = 0.1f;

private:
	FTimerHandle SlowRefreshTimerHandle;
};
