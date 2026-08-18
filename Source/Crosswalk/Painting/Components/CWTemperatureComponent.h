// CWTemperatureComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Painting/Data/CWTemperatureTypes.h"
#include "CWTemperatureComponent.generated.h"

class UCWTemperatureComponent;
class UCharacterMovementComponent;

/**
 * 체온 값이 바뀔 때마다. 서버와 클라이언트 양쪽에서, 모든 캐릭터에 대해 발행된다.
 *
 * UI 와 머티리얼이 붙는 자리다. HeatRatio 는 0~1 로 정규화된 값이라 게이지 채움과
 * 머티리얼 스칼라 파라미터에 그대로 넣으면 된다. Temperature 는 화면에 숫자로 띄울 때 쓴다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCWOnTemperatureChangedSignature, UCWTemperatureComponent*, TemperatureComponent, float, Temperature, float, HeatRatio);

/**
 * 체온 구간이 바뀐 순간에만 발행된다. 1회성 연출(플래시, 사운드, 경고음)이 붙는 자리다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnHeatStageChangedSignature, UCWTemperatureComponent*, TemperatureComponent, ECWHeatStage, NewStage);

/**
 * 휴식구역 점유 상태가 바뀔 때. 회복 중임을 알리는 연출이 붙는 자리다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnRestingChangedSignature, UCWTemperatureComponent*, TemperatureComponent, bool, bResting);

/**
 * 플레이어 캐릭터에 붙이는 체온 컴포넌트. (체온 관리 시스템 기획서)
 *
 * 하는 일
 *   1. 작업하면 체온이 오르고, 가만히 있으면 일정 속도로 내려간다
 *   2. 체온에서 이동속도 계수와 작업속도 계수를 뽑아준다
 *   3. 휴식구역 안에서는 하락 속도가 크게 붙는다
 *
 * 최대 체온을 값으로 막지 않는 이유 (기획서 3.3)
 *   체온이 오르면 작업이 느려지고, 작업이 느려지면 체온 상승도 같이 느려진다.
 *   그래서 "상승량 = 하락량" 이 되는 지점에서 저절로 멈춘다. 상한을 코드로 박을 필요가 없다.
 *
 *   이 구조가 성립하려면 체온 상승이 "흐른 시간" 이 아니라 "실제로 진행된 작업량" 에
 *   비례해야 한다. 그래서 AddWorkHeat 은 작업속도 계수가 이미 곱해진 시간을 받는다.
 *   호출부는 UCWWorkerComponent::TickComponent 한 곳뿐이다.
 *
 *   ReferenceMaxTemperature 는 상한이 아니라 "게이지의 끝" 이다. UI 와 머티리얼과
 *   패널티가 이 값을 100% 로 보고 정규화한다. 기본값 기준 실제 균형점은 68도 근처라
 *   여기에 닿는 일은 거의 없다.
 *
 * 네트워크
 *   체온을 실제로 굴리는 것은 서버뿐이고, 값만 전원에게 리플리케이트된다.
 *
 *   이동속도는 각 머신이 자기가 받은 체온으로 직접 계산해서 넣는다. MaxWalkSpeed 는
 *   리플리케이트되지 않아서, 서버에서만 세팅하면 클라이언트의 이동 예측이 서버 값과
 *   어긋나 계속 보정으로 튄다. 체온은 이미 모두에게 있으므로 각자 계산하면 값이 같다.
 *
 *   델리게이트도 모든 머신에서 모든 캐릭터에 대해 발행된다. 남의 체온이 얼마나 높은지
 *   보이는 것은 휴식구역 순서를 조율하는 데 직접 쓰이는 정보라 일부러 이렇게 둔다.
 */
UCLASS(ClassGroup = (Crosswalk), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWTemperatureComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWTemperatureComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void BeginPlay() override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * 작업으로 체온을 올린다. 서버에서만 유효하다.
	 *
	 * @param EffectiveWorkSeconds 작업속도 계수가 이미 곱해진 시간(초).
	 *                             느려진 만큼 체온도 덜 오른다. 이게 기획서 3.3 의 균형을 만든다.
	 */
	void AddWorkHeat(float EffectiveWorkSeconds);

	/**
	 * 휴식구역에 들어가고 나올 때 ACWRestZone 이 부른다. 서버에서만 유효하다.
	 *
	 * 점유자로 인정받은 1명만 true 를 받는다. 존 안에 서 있어도 점유자가 아니면 false 다.
	 */
	void SetResting(bool bNewResting);

	UFUNCTION(BlueprintPure, Category = "Temperature")
	float GetTemperature() const { return CurrentTemperature; }

	/** 0 = 평상 체온, 1 = 게이지 끝. UI 채움과 머티리얼 스칼라에 그대로 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Temperature")
	float GetHeatRatio() const;

	/** 이동속도에 곱할 값. 1 이면 평상 속도. */
	UFUNCTION(BlueprintPure, Category = "Temperature")
	float GetMoveSpeedFactor() const;

	/** 작업 진행에 곱할 값. 1 이면 평상 속도, 0.2 면 5배 느려진 것이다. */
	UFUNCTION(BlueprintPure, Category = "Temperature")
	float GetWorkSpeedFactor() const;

	UFUNCTION(BlueprintPure, Category = "Temperature")
	ECWHeatStage GetHeatStage() const { return HeatStage; }

	/** 지금 휴식구역 점유자인가. 회복 연출을 켜고 끄는 기준이다. */
	UFUNCTION(BlueprintPure, Category = "Temperature")
	bool IsResting() const { return bResting; }

private:
	/** 서버에서 체온을 굴린다. 상승은 AddWorkHeat 가 따로 넣고, 여기서는 하락만 한다. */
	void SimulateTemperature(float DeltaTime);

	/** 지금 체온에 맞는 이동속도를 캐릭터에 넣는다. 모든 머신이 각자 부른다. */
	void ApplyMoveSpeed();

	/** 값이 실제로 바뀌었을 때만 델리게이트를 발행한다. 모든 머신이 각자 부른다. */
	void BroadcastIfChanged();

	/** 지금 체온이 어느 구간인지. 구간이 바뀐 순간을 잡는 데만 쓴다. */
	ECWHeatStage ResolveHeatStage() const;

	/** 체온과 계수를 화면에 띄운다. UI 가 붙기 전까지 프로토타입 확인용. */
	void ShowDebugTemperature() const;

	/** 정규화 구간의 폭. 0 으로 나누는 것을 막는다. */
	float GetTemperatureSpan() const;

public:
	// 평상 체온(도). 정규화의 0 지점이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature")
	float NormalTemperature = 36.5f;

	// 정규화의 1 지점(도). 값을 여기서 자르지 않는다. UI·머티리얼·패널티가 100% 로 보는 기준일 뿐이다.
	//
	// 70 인 이유: 기획서 6.2 의 색 표가 70도(빨강)에서 끝난다. 100 으로 잡으면 70~100 구간의
	// 색이 정의되지 않고, 게이지도 절반 넘게 노랑~주황에 머물러 위험 구간이 눈에 안 띈다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "1.0"))
	float ReferenceMaxTemperature = 70.f;

	// 작업 1초당 오르는 체온(도). 여기에 작업속도 계수가 곱해진다.
	//
	// 이 값과 CoolRate 가 기획서 3.3 의 "실질적 최대 체온" 을 결정한다.
	// 상승량은 뜨거워질수록 줄어들다가 하락량과 만나는 지점에서 멈춘다.
	//
	//   균형 비율 = (1 - CoolRate / WorkHeatRate) / (1 - MinWorkSpeedFactor)
	//   최대 체온 = NormalTemperature + (ReferenceMaxTemperature - NormalTemperature) * 균형 비율
	//
	// 기본값(상승 1.0 / 하락 0.2 / 작업 하한 0.2)이면 균형점이 정확히 70도, 즉 게이지 끝이다.
	// 다만 끝에 가까워질수록 상승과 하락의 차이가 0 으로 수렴해서 실제로는 70도에 닿기 직전까지만 간다.
	//
	// 주의: 작업속도 계수에는 하한이 있어서 상승량도 WorkHeatRate * MinWorkSpeedFactor 아래로는
	// 내려가지 않는다. 이 값이 CoolRate 보다 크면 균형점이 아예 생기지 않고 체온이 무한정 오른다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "0.0"))
	float WorkHeatRate = 1.f;

	// 초당 내려가는 체온(도). 기획서 3.1 대로 항상 일정하다. 작업 중에도 계속 빠진다.
	//
	// 주의: 이 값이 WorkHeatRate * MinWorkSpeedFactor 보다 작으면 균형점이 사라져
	// 작업을 계속하는 동안 체온이 70도를 넘어 계속 올라간다. (기획서 3.3 의 "실질적 최대" 가 없어진다)
	// 패널티는 70도에서 포화되므로 게임이 망가지지는 않지만, 식는 데 그만큼 오래 걸린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "0.0"))
	float CoolRate = 0.2f;

	// 휴식구역 점유 중에 추가로 내려가는 체온(도/초). 위의 기본 하락에 더해진다.
	// 기획서 8절 "눈에 띄게 빠르게" 를 만족하려면 기본 하락보다 확실히 커야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "0.0"))
	float RestCoolRateBonus = 9.f;

	// 게이지 끝에서의 이동속도 계수. (기획서 4.1 "0 에 수렴")
	//
	// 기획서 문구대로 0 에 두면 이동 자체가 멈춰서 답답하다. 테스트 결과 0.2 로 둔다.
	// 기본 이동속도 500 기준 최저 100 이라, 기어가는 수준이지만 휴식구역까지는 갈 수 있다.
	//
	// 값을 0 까지 내려도 게임이 잠기지는 않는다. 가만히 있어도 체온이 내려가기 때문이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinMoveSpeedFactor = 0.2f;

	// 게이지 끝에서의 작업속도 계수. (기획서 4.2 "최대 5배" → 1/5 = 0.2)
	//
	// 0 으로 두면 안 된다. 상승량이 0 이 되어 균형점이 사라지고, 그 온도에서 작업이 영영 안 끝난다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float MinWorkSpeedFactor = 0.2f;

	// 과열 주의 구간에 들어서는 체온(도). 연출 신호용이고 패널티 계산과는 무관하다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature|Stage")
	float CautionTemperature = 50.f;

	// 과열 구간에 들어서는 체온(도). 균형점(68 근처)보다 조금 낮게 잡아야 경고가 의미를 갖는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature|Stage")
	float OverheatTemperature = 65.f;

	// 체온과 계수를 화면 좌상단에 띄운다. 체온 UI 가 생기면 끄면 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Temperature|Debug")
	bool bShowDebugTemperature = false;

protected:
	// 서버가 굴리고 전원이 받는다. 이 값 하나에서 UI, 머티리얼, 이동속도, 작업속도가 전부 파생된다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Temperature")
	float CurrentTemperature = 36.5f;

	// 휴식구역 점유자인가. 회복 연출을 남의 화면에서도 켜야 해서 리플리케이트한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Temperature")
	bool bResting = false;

private:
	// 이동속도의 기준값. BeginPlay 에서 무브먼트의 초기값을 읽어 둔다.
	// 캐릭터 BP 에서 다른 값을 넣었어도 그 값이 기준이 된다.
	float BaseWalkSpeed = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> MovementComponent;

	// 마지막으로 발행한 값. 이것과 다를 때만 델리게이트가 나간다.
	float LastBroadcastTemperature = -1000.f;

	ECWHeatStage HeatStage = ECWHeatStage::Normal;

	bool bLastBroadcastResting = false;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Temperature")
	FCWOnTemperatureChangedSignature OnTemperatureChanged;

	UPROPERTY(BlueprintAssignable, Category = "Temperature")
	FCWOnHeatStageChangedSignature OnHeatStageChanged;

	UPROPERTY(BlueprintAssignable, Category = "Temperature")
	FCWOnRestingChangedSignature OnRestingChanged;
};
