// CWTemperatureComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Painting/Data/CWTemperatureTypes.h"
#include "CWTemperatureComponent.generated.h"

class UCWSupplyCarrierComponent;
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
 *   3. 휴식구역 의자에 앉아 있는 동안에는 하락 속도가 크게 붙는다
 *   4. 평상 체온 아래까지 내려가면 두 계수가 1 을 넘어 버프가 된다
 *
 * 체온 눈금 하나가 페널티와 버프를 같이 표현한다
 *   MinTemperature ---- NormalTemperature ---- MaxTemperature
 *        버프 최대           계수 1.0              페널티 최대
 *
 *   휴식만이 평상 체온 아래로 내려보낼 수 있고, 작업과 운반이 그것을 도로 태운다.
 *   버프를 별도 상태로 두지 않은 이유는 MinTemperature 주석 참고 — 복제가 늘지 않는다.
 *
 * 체온이 어디서 멈추는가 (기획서 3.3)
 *   1차로는 저절로 멈춘다. 체온이 오르면 작업이 느려지고, 작업이 느려지면 체온 상승도
 *   같이 느려져서 "상승량 = 하락량" 이 되는 지점에서 균형을 잡는다.
 *
 *   이 구조가 성립하려면 체온 상승이 "흐른 시간" 이 아니라 "실제로 진행된 작업량" 에
 *   비례해야 한다. 그래서 AddWorkHeat 은 작업속도 계수가 이미 곱해진 시간을 받는다.
 *   운반 상승(AddCarryHeat)도 같은 이유로 이동 거리에 비례한다.
 *
 *   2차로 MaxTemperature 가 자른다. 균형점만 믿으면 파라미터 조합 하나가 어긋났을 때
 *   (예: 상승률을 올리면서 CoolRate 를 안 올렸을 때) 균형점이 사라져 체온이 무한정 오른다.
 *   그러면 게이지는 꽉 찬 채로 멈춰 있어 플레이어는 자기가 얼마나 깊이 들어갔는지 모르고,
 *   회복 시간만 "오래 일했을수록 길어진다" 로 예측 불가능해진다.
 *   상한이 있으면 최악의 회복 시간이 고정되므로 밸런싱의 기준선이 생긴다.
 *
 *   ReferenceMaxTemperature 는 상한이 아니라 "게이지의 끝" 이다. UI 와 머티리얼과
 *   패널티가 이 값을 100% 로 보고 정규화한다. 상한은 MaxTemperature 가 따로 쥐고 있고,
 *   둘을 같은 값으로 두면 "게이지가 꽉 차는 순간이 곧 상한" 이 된다.
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
	 * 휴식구역 의자에 앉고 일어날 때 ACWRestZone 이 부른다. 서버에서만 유효하다.
	 *
	 * 실제로 앉은 1명만 true 를 받는다. 존 안에 서 있기만 해서는 존 밖과 다를 것이 없다.
	 * (착석 기획서 4.3 - 겹침으로 회복되던 예전 방식과 달라진 지점이다)
	 */
	void SetResting(bool bNewResting);

	UFUNCTION(BlueprintPure, Category = "Temperature")
	float GetTemperature() const { return CurrentTemperature; }

	/** 0 = 평상 체온, 1 = 게이지 끝. UI 채움과 머티리얼 스칼라에 그대로 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Temperature")
	float GetHeatRatio() const;

	/**
	 * 0 = 평상 체온, 1 = MinTemperature. 평상 체온 위에서는 항상 0 이다.
	 *
	 * GetHeatRatio 를 음수로 열지 않고 따로 두는 이유: 저쪽은 UI 게이지와 머티리얼 스칼라가
	 * 0~1 로 묶여 있는 계약이다. 음수가 흘러들면 게이지와 오버레이가 같이 깨진다.
	 */
	UFUNCTION(BlueprintPure, Category = "Temperature")
	float GetChillRatio() const;

	/** 이동속도에 곱할 값. 1 이면 평상 속도. */
	UFUNCTION(BlueprintPure, Category = "Temperature")
	float GetMoveSpeedFactor() const;

	/** 작업 진행에 곱할 값. 1 이면 평상 속도, 0.2 면 5배 느려진 것이다. */
	UFUNCTION(BlueprintPure, Category = "Temperature")
	float GetWorkSpeedFactor() const;

	UFUNCTION(BlueprintPure, Category = "Temperature")
	ECWHeatStage GetHeatStage() const { return HeatStage; }

	/**
	 * 상한에 닿아 있는가.
	 *
	 * 밸런싱 중에 보라고 만든 것이다. 여기에 걸려 있다는 것은 균형점이 사라졌다는 뜻이고,
	 * 그러면 지금 화면에 보이는 체온은 "설계된 값" 이 아니라 "잘린 값" 이라 튜닝 근거로 못 쓴다.
	 */
	UFUNCTION(BlueprintPure, Category = "Temperature")
	bool IsAtMaxTemperature() const { return CurrentTemperature >= MaxTemperature - KINDA_SMALL_NUMBER; }

	/** 지금 휴식구역 점유자인가. 회복 연출을 켜고 끄는 기준이다. */
	UFUNCTION(BlueprintPure, Category = "Temperature")
	bool IsResting() const { return bResting; }

	/**
	 * 체온과 무관한 외부 요인으로 이동속도에 하한을 건다. 서버에서만 유효하다.
	 * (싱크홀 기획서 7-9 - 바닥에 깔린 아스팔트를 밟았을 때)
	 *
	 * 이 컴포넌트가 MaxWalkSpeed 의 단독 소유자라 외부에서 직접 넣으면 다음 틱에 덮인다.
	 * 그래서 속도를 깎는 모든 요인은 이 입구를 통해 들어온다.
	 *
	 * 이미 슬로우가 걸려 있는 동안의 호출은 무시된다. 밟고 서 있는 내내 타이머가
	 * 갱신되면 언제 풀리는지 예측할 수 없어서, 한 번 걸리면 그 구간은 끝까지 간다.
	 * 지속시간이 끝난 뒤 다시 부르면 그때 새로 걸린다.
	 *
	 * @param SlowFactor 이동속도에 곱할 값. MinMoveSpeedFactor 를 넣으면 최저 이속이 된다.
	 * @param Duration   지속시간(초)
	 */
	void ApplyExternalSlow(float SlowFactor, float Duration);

	UFUNCTION(BlueprintPure, Category = "Temperature")
	bool IsExternallySlowed() const { return bExternallySlowed; }

	/** 최저 이속 계수. 외부 슬로우가 "최저 이속"을 걸 때 이 값을 넘겨받는다. */
	UFUNCTION(BlueprintPure, Category = "Temperature")
	float GetMinMoveSpeedFactor() const { return MinMoveSpeedFactor; }

private:
	/** 외부 슬로우 지속시간이 끝났을 때. 여기서부터 다시 걸릴 수 있다. */
	void ClearExternalSlow();

	/**
	 * 서버에서 체온을 굴린다. 하락과 운반 상승이 여기서 돈다.
	 *
	 * 작업 상승만 밖에 있다. 그쪽은 "작업이 실제로 진행됐는가" 를 존이 판단해야 해서
	 * 틱이 아니라 UCWWorkerComponent 가 AddWorkHeat 로 밀어 넣는다.
	 */
	void SimulateTemperature(float DeltaTime);

	/**
	 * 페인트 통을 들고 이동한 만큼 체온을 올린다. SimulateTemperature 안에서만 부른다.
	 *
	 * 작업 상승과 겹칠 일은 없다. 통을 들면 도구를 들 수 없어 두 경로는 동시에 성립하지 않는다.
	 */
	void AddCarryHeat(float DeltaTime);

	/**
	 * 모든 체온 상승이 지나가는 단 하나의 입구. 상한은 여기서만 건다.
	 *
	 * 경로마다 각자 자르면 상한 조건이 흩어져서, 나중에 상승원이 하나 더 생겼을 때
	 * 그것만 안 잘린다. ApplyExternalSlow 가 이동속도의 단일 입구인 것과 같은 이유다.
	 */
	void AddHeat(float Amount);

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

	/** 버프 구간의 폭. 0 으로 나누는 것을 막는다. */
	float GetChillSpan() const;

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

	/**
	 * 체온이 넘어갈 수 없는 상한(도). 모든 상승 경로가 여기서 잘린다.
	 *
	 * 균형점이 있으면 여기에 닿지 않는다. 이건 균형점이 사라졌을 때를 받는 안전망이다.
	 * 상승률 하나를 올리면서 CoolRate 를 같이 안 올리면 균형점이 없어지는데, 그때
	 * 상한이 없으면 체온이 100도, 200도까지 올라가 회복 시간이 통제를 벗어난다.
	 *
	 * ReferenceMaxTemperature 와 같은 값으로 둔다. 다르게 두면 이렇게 된다.
	 *   더 높게: 게이지가 꽉 찬 뒤에도 값은 더 오른다. 화면상 구분이 안 가는 구간이 생긴다
	 *   더 낮게: 게이지가 끝까지 안 차고 패널티도 최대치에 못 간다
	 * 둘 다 의도해서 쓸 일은 거의 없으므로, 색 표(기획서 6.2)를 고칠 때 같이 옮긴다.
	 *
	 * 이 값이 정하는 것은 "최악의 회복 시간" 이다. 상한에서 평상 체온까지가
	 * 서서 (MaxTemperature - NormalTemperature) / CoolRate 초, 착석은 여기에
	 * RestCoolRateBonus 가 더해진 속도로 걸린다. 밸런싱은 이 두 숫자를 기준으로 잡는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "1.0"))
	float MaxTemperature = 70.f;

	/**
	 * 휴식으로 내려갈 수 있는 최저 체온(도). 평상 체온 아래는 전부 버프 구간이다.
	 *
	 * 왜 체온 하나로 버프까지 표현하는가
	 *   버프를 별도 상태로 두면 그 상태를 복제해야 한다. ApplyMoveSpeed 가 모든 머신에서
	 *   각자 돌기 때문에, 서버만 버프를 알면 클라이언트 화면에서는 안 빨라지고 이동 예측이 튄다.
	 *   체온은 이미 복제되고 있고 두 배율이 전부 거기서 파생되므로, 눈금을 아래로 늘리면
	 *   새 상태 없이 같은 일을 한다.
	 *
	 * 10 인 이유 — 착석 시간에서 거꾸로 뽑았다.
	 *   버프 충전 = (NormalTemperature - 이 값) / (CoolRate + RestCoolRateBonus)
	 *   테스트 세트 기준 2.8초. 70도에서 앉으면 회복 3.6초 + 충전 2.8초 = 총 6.4초 착석이다.
	 *   착석이 짧아 자리 점유 경쟁이 헐거워지던 문제를 버프 구간이 같이 메운다.
	 *
	 * 평상 체온 아래로는 휴식으로만 내려간다. 일어난 뒤에는 그 자리에서 유지되고,
	 * 작업이나 운반으로 체온이 오르면서 소모된다. 따로 해제 타이머를 두지 않는 이유다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature")
	float MinTemperature = 10.f;

	// 작업 1초당 오르는 체온(도). 여기에 작업속도 계수가 곱해진다.
	//
	// 이 값과 CoolRate 가 기획서 3.3 의 "실질적 최대 체온" 을 결정한다.
	// 상승량은 뜨거워질수록 줄어들다가 하락량과 만나는 지점에서 멈춘다.
	//
	//   균형 비율 = (1 - CoolRate / WorkHeatRate) / (1 - MinWorkSpeedFactor)
	//   최대 체온 = NormalTemperature + (ReferenceMaxTemperature - NormalTemperature) * 균형 비율
	//
	// 기본값(상승 2.79 / 하락 0.558 / 작업 하한 0.2)이면 균형점이 정확히 70도, 즉 게이지 끝이다.
	// 다만 끝에 가까워질수록 상승과 하락의 차이가 0 으로 수렴해서 실제로는 70도에 닿기 직전까지만 간다.
	//
	// CoolRate 와 5:1 로 묶여 있다. 이 비를 유지한 채 둘을 같이 k 배 하면 체온 시계 전체가
	// k 배 빨라지고 균형점과 휴식구역 배율은 그대로다. 원래 1.0 / 0.2 였던 것을 2.79 배 한 값이라
	// 작업 시작부터 과열까지가 80초에서 29초로 줄었다. (다이나믹 튜닝, 2026-09-23)
	//
	// 주의: 작업속도 계수에는 하한이 있어서 상승량도 WorkHeatRate * MinWorkSpeedFactor 아래로는
	// 내려가지 않는다. 이 값이 CoolRate 보다 크면 균형점이 아예 생기지 않고, 작업하는 내내
	// 체온이 올라 MaxTemperature 에 붙은 채로 머문다. (상한이 없던 시절에는 무한정 올랐다)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "0.0"))
	float WorkHeatRate = 2.79f;

	// 초당 내려가는 체온(도). 기획서 3.1 대로 항상 일정하다. 작업 중에도 계속 빠진다.
	//
	// 이 값이 "서서 버티기" 시간을 혼자 정한다. 상한에서 평상 체온까지 33.5 / 0.558 = 60초다.
	// 차에 치여 멀리 날아갔을 때의 복귀 고통이 여기에 직결된다 — 원래 0.2 일 때는 168초였고,
	// 200m 복귀가 94초 걸려서 불쾌하다는 판단으로 올렸다. (2026-09-23)
	//
	// 휴식구역 매력도 = (CoolRate + RestCoolRateBonus) / CoolRate 다. 이 값만 올리면
	// 분모가 커져 휴식구역이 덜 매력적이 되므로, 올릴 때는 RestCoolRateBonus 도 같은 배수로 올린다.
	//
	// 주의: 이 값이 WorkHeatRate * MinWorkSpeedFactor 보다 작으면 균형점이 사라져
	// 작업을 계속하는 동안 체온이 MaxTemperature 까지 올라가 거기 붙는다.
	// (기획서 3.3 의 "실질적 최대" 가 없어지고, 상한이 대신 받는다)
	//
	// 게임이 망가지지는 않지만 밸런싱이 어려워진다. 상한에 붙으면 상승률을 얼마로 바꾸든
	// 화면에 보이는 체온이 똑같아서, 값을 움직여도 결과가 안 변하는 것처럼 보인다.
	// 디버그 표시의 [상한] 이 그 상태를 알려준다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "0.0"))
	float CoolRate = 0.558f;

	/**
	 * 휴식구역 의자에 앉아 있는 동안 추가로 내려가는 체온(도/초). 위의 기본 하락에 더해진다.
	 *
	 * 값은 휴식구역 매력도에서 뽑는다. CoolRate 와의 비가 곧 "서서 버티기 대비 몇 배" 다.
	 *   매력도 = (CoolRate + RestCoolRateBonus) / CoolRate = (0.558 + 8.79) / 0.558 = 16.8배
	 *   착석 완전 회복 = 33.5 / 9.348 = 3.6초  (서서 버티기는 60초)
	 *
	 * 이 배수가 휴식구역의 존재 이유다. 서서 식는 시간이 휴식구역 왕복 시간과 비슷해지면
	 * 아무도 안 간다. 왕복 20초 기준으로 서서 60초는 되어야 갈 이유가 생긴다.
	 *
	 * 착석 3.6초는 예전 10초보다 짧지만, 체온 시계 전체가 2.79 배 빨라진 결과라 비중은 같다.
	 * 여기에 버프 충전 2.8초가 더 붙어 실제 착석은 6.4초다. (MinTemperature 주석 참고)
	 *
	 * 주의: CoolRate 를 바꾸면 이 값도 같은 배수로 움직여야 매력도가 유지된다.
	 * ReferenceMaxTemperature 나 NormalTemperature 를 바꿔도 회복 시간이 달라진다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "0.0"))
	float RestCoolRateBonus = 8.79f;

	/**
	 * 페인트 통을 들고 이동할 때, 통 1개당 초당 오르는 체온(도). 0 이면 이 규칙이 통째로 꺼진다.
	 *
	 * 왜 필요한가
	 *   체온이 오르는 경로가 작업 하나뿐이면 보급 담당은 평상 체온으로 게임을 마친다.
	 *   양손 제약 때문에 통을 든 사람은 도구를 들 수 없어 AddWorkHeat 에 한 번도 걸리지 않는다.
	 *   그러면 휴식구역을 두고 순서를 다투는 인원이 한 명 줄어, 4인 중 3인에게만 긴장이 걸린다.
	 *
	 * 왜 "이동할 때만" 인가
	 *   들고 서 있는 것은 노동이 아니다. 왕복하는 거리가 곧 보급의 노동량이라 거기에 비례시킨다.
	 *   흐른 시간이 아니라 실제 이동량에 거는 것은 작업 체온과 같은 이유다. (기획서 3.3)
	 *   더워지면 느려지고, 느려지면 덜 더워져서 상승과 하락이 만나는 지점에서 저절로 멈춘다.
	 *
	 * 1.4 인 이유 — 체감 속도를 우선한 값이다. 균형점은 일부러 포기했다. (2026-09-23 튜닝)
	 *   균형 비율 = (1 - CoolRate / (이 값 * 통 개수)) / (1 - MinMoveSpeedFactor)
	 *   통 1개: 67.5도에서 멈춘다
	 *   통 2개: 균형점 없음. MaxTemperature(70도)까지 올라가 거기 붙는다
	 *
	 *   0.75 까지는 균형점이 살아 있었고(1개 49.7도 / 2개 68.9도) 개수로 구간이 갈렸는데,
	 *   그 값에서는 통 1개 순증이 0.19도/초라 왕복 한 번에 체감이 안 난다는 판단으로 올렸다.
	 *   지금 값에서는 개수 구분이 거의 사라진다. 구분을 되살리려면 0.75 로 내린다.
	 *
	 *   균형점을 포기할 수 있게 된 것은 MaxTemperature 가 생긴 덕이다. 상한이 없던 시절에는
	 *   회복 시간이 통제를 벗어나서 이런 선택 자체가 불가능했다.
	 *
	 * 주의: 이 값 * 최대 소지 개수 * MinMoveSpeedFactor 가 CoolRate 보다 크면 균형점이 사라져
	 * 나르는 동안 체온이 MaxTemperature 까지 올라가 거기 붙는다.
	 * 기본값 기준 1.4 * 2 * 0.35 = 0.98 로 CoolRate(0.558) 을 넘는다 — 위에 적은 대로 의도한 것이다.
	 * 디버그 표시에 [상한] 이 뜨는 것이 정상 동작이지 버그가 아니다.
	 * UCWSupplyCarrierComponent::MaxCarriedCount 를 올리면 이 값도 같이 봐야 한다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "0.0"))
	float CarryHeatRatePerCan = 1.4f;

	// 이동으로 치는 최소 속도(cm/s). 밀림이나 미끄러짐 같은 미세한 움직임을 걸러낸다.
	//
	// 서 있기만 해도 체온이 찔끔씩 오르면 "움직일 때만" 이라는 규칙이 화면에서 거짓말이 된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "0.0"))
	float CarryHeatMinSpeed = 10.f;

	// 게이지 끝에서의 이동속도 계수. (기획서 4.1 "0 에 수렴")
	//
	// 기획서 문구대로 0 에 두면 이동 자체가 멈춰서 답답하다. 처음엔 0.2 로 뒀다가
	// 0.35 로 올렸다 — 기본 이동속도 500 기준 최저 175 다. (2026-09-23)
	//
	// 올린 이유: 차에 치이면 멀리 날아가는데 고열 상태의 이속으로 걸어 돌아오는 것이
	// 불쾌하다는 판단이다. 다만 실측해보면 복귀 시간의 대부분은 CoolRate 가 정한다 —
	// 돌아오는 동안에도 체온이 내려가기 때문이다. 200m 복귀가 0.2 일 때 64초, 0.35 에서 59초다.
	// 여기를 더 올려도 5초 단위로만 움직이므로, 복귀가 여전히 답답하면 CoolRate 를 봐야 한다.
	//
	// 운반 체온의 상한과 묶여 있다. 이 값을 올리면 "더워지면 느려져서 덜 오른다" 는 제동이
	// 약해져서 CarryHeatRatePerCan 의 허용 상한이 내려간다. 둘을 같이 봐야 한다.
	//
	// 값을 0 까지 내려도 게임이 잠기지는 않는다. 가만히 있어도 체온이 내려가기 때문이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinMoveSpeedFactor = 0.35f;

	// 게이지 끝에서의 작업속도 계수. (기획서 4.2 "최대 5배" → 1/5 = 0.2)
	//
	// 0 으로 두면 안 된다. 상승량이 0 이 되어 균형점이 사라지고, 그 온도에서 작업이 영영 안 끝난다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float MinWorkSpeedFactor = 0.2f;

	// MinTemperature 에 닿았을 때의 이동속도 계수. 1.0 으로 두면 이동 버프가 꺼진다.
	//
	// 버프가 화면에 안 보이므로(게이지는 0 에서 클램프된다) 너무 크게 잡으면
	// "왜 갑자기 빨라졌지" 가 된다. 체감은 나는데 설명은 안 되는 구간이라 1.2 에서 시작한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "1.0"))
	float MaxMoveSpeedFactor = 1.2f;

	/**
	 * MinTemperature 에 닿았을 때의 작업속도 계수. 1.0 으로 두면 작업 버프가 꺼진다.
	 *
	 * 이 버프는 스스로를 태운다. UCWWorkerComponent 가 계수를 곱한 시간을 그대로
	 * AddWorkHeat 에 넘기기 때문에, 1.2 배로 일하면 체온도 1.2 배로 오른다.
	 * 결과적으로 총 작업량당 체온 비용은 그대로고 타이밍만 앞당겨진다 —
	 * "휴식으로 번 것을 작업으로 쓴다". 밸런스가 저절로 닫히므로 따로 막지 않는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "1.0"))
	float MaxWorkSpeedFactor = 1.2f;

	// 과열 주의 구간에 들어서는 체온(도). 연출 신호용이고 패널티 계산과는 무관하다.
	//
	// 절대 온도라 상승률을 올려도 값 자체는 그대로 두면 되지만, 도달이 그만큼 빨라진다.
	// 현재 기본값 기준 작업 시작 7.7초면 여기 들어오고 28.5초에 과열이다.
	// 더 빠르게 튜닝하면 "주의" 가 사실상 기본 상태가 되므로 그때는 이 값도 같이 올린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Temperature|Stage")
	float CautionTemperature = 50.f;

	// 과열 구간에 들어서는 체온(도). 균형점보다 낮게 잡아야 경고가 의미를 갖는다.
	//
	// 작업 균형점은 게이지 끝(70도)이고 운반은 상한에 붙으므로, 양쪽 다 이 값을 넘어간다.
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

	// 외부 요인으로 인한 슬로우가 걸려 있는가. (아스팔트 등)
	//
	// ApplyMoveSpeed 는 클라이언트에서도 각자 돌기 때문에 이 값을 리플리케이트하지 않으면
	// 서버에서만 느려지고 클라이언트 화면에서는 평소 속도로 움직인다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Temperature")
	bool bExternallySlowed = false;

	// 외부 슬로우가 걸렸을 때 이동속도에 곱할 값. 슬로우가 없으면 1 이다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Temperature")
	float ExternalSlowFactor = 1.f;

private:
	// 외부 슬로우 해제 타이머. 서버에서만 돈다.
	FTimerHandle ExternalSlowTimerHandle;

	// 이동속도의 기준값. BeginPlay 에서 무브먼트의 초기값을 읽어 둔다.
	// 캐릭터 BP 에서 다른 값을 넣었어도 그 값이 기준이 된다.
	float BaseWalkSpeed = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> MovementComponent;

	// 통 개수를 물어볼 상대. BeginPlay 에서 한 번만 잡는다.
	// 캐릭터에 운반 컴포넌트가 없으면 nullptr 로 남고, 그러면 운반 체온만 조용히 꺼진다.
	UPROPERTY(Transient)
	TObjectPtr<UCWSupplyCarrierComponent> SupplyCarrierComponent;

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
