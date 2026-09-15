// CWVehicleAudioComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CWVehicleAudioComponent.generated.h"

class ACWBaseVehicle;
class UAudioComponent;
class USoundAttenuation;
class USoundBase;

/**
 * 차량 한 대의 소리(엔진 루프 + 경적)와 도플러를 담당한다. (연출 전용)
 *
 * **복제하지 않는다. 서버·클라이언트가 각자 자기 화면에서 울린다.**
 *
 * 소리는 판정이 아니라 연출이라 서버가 골라 Multicast 할 이유가 없다. 오히려 해롭다 -
 * 경적이 시끄러운지 아닌지는 "내 리스너 근처에서 몇 대가 울렸나"로 정해지는데, 그 판단은
 * 각자의 화면에서만 내릴 수 있다. 서버가 한 대를 골라 전원에게 보내면 누군가에게는
 * 멀어서 안 들리고 누군가에게는 겹친다. 대역폭도 0 으로 끝난다.
 *
 * 경적이 아무 때나 울리지 않도록 세 겹으로 막는다. 한 겹씩 담당이 다르다.
 *   1. 들리는 거리 안인가(HornAudibleRange) - 안 들릴 소리가 예산을 까먹지 않게 한다.
 *   2. 이 차가 최근에 울렸나(HornCooldownSeconds)  - 같은 차가 연달아 울리는 것을 막는다.
 *   3. 월드 전체가 최근에 울렸나(UCWTrafficSubsystem::TryConsumeHornBudget)
 *      - 서로 다른 차 여러 대가 같은 순간에 우는 것을 막는다.
 *
 * 동시 발성 상한은 여기서 세지 않는다. 그쪽은 Sound Concurrency 애셋(Max Count / Stop
 * Quietest)이 엔진 차원에서 이미 하는 일이고, 코드로 또 세면 두 벌이 서로 다른 답을 낸다.
 *
 * 도플러는 SoundCue 의 Doppler 노드를 쓰지 않고 여기서 직접 피치를 계산한다. 이유 셋:
 *   - 그 노드는 SoundCue 전용이다. MetaSound 로 만든 소리에는 아예 붙지 않는다.
 *   - 그 노드에는 상한이 없다. 패링으로 튕겨 나간 차처럼 속도가 튀면 피치도 같이 튄다.
 *   - 세기를 차량 BP 마다 다르게 주려면 애셋을 차종 수만큼 복제해야 한다.
 * Doppler 노드를 꽂은 애셋을 여기에 지정하면 피치가 두 번 곱해진다. 둘 중 하나만 쓴다.
 */
UCLASS(ClassGroup = (Crosswalk), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWVehicleAudioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWVehicleAudioComponent();

	/**
	 * 경적을 울린다. 세 겹의 게이트를 모두 통과했을 때만 실제로 난다.
	 *
	 * 울릴 "이유"는 여기서 정하지 않는다. **이 컴포넌트가 스스로 부르는 곳은 없다.**
	 * 계기는 밖에서 붙인다 - 지금은 BP 에서 이 함수를 부르는 길뿐이다.
	 *
	 * 아래 세 게이트는 경적을 배경 연출로 쓸 때를 전제로 한 값이다. 접근 경고로 쓰려면
	 * 그대로 두면 안 된다 - 4 번에 1 번만 나는 경고는 없느니만 못하다. 계기를 붙일 때
	 * 게이트도 같이 다시 정할 것.
	 *
	 * @return 실제로 소리가 났으면 true. 게이트에 걸렸으면 false.
	 */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Audio")
	bool RequestHorn();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** 엔진 루프를 붙여 재생한다. 애셋이 없으면 아무 일도 하지 않는다. */
	void StartEngineLoop();

	/** 속도를 0~1 로 정규화해 엔진음 애셋에 넘긴다. 애셋이 파라미터를 안 쓰면 무시된다. */
	void UpdateEngineParameters();

	/**
	 * 리스너와의 상대 속도로 피치 배수를 구해 재생 중인 소리에 적용한다.
	 *
	 * 공식은 엔진의 USoundNodeDoppler::GetDopplerPitchMultiplier 와 같다.
	 * 음속 33000cm/s 에 기본 주행 속도 750cm/s 면 피치 변화가 2.3% 뿐이라
	 * (반음이 5.9%) 세기를 1 로 두면 사실상 안 들린다. DopplerIntensity 가 그 몫이다.
	 */
	void UpdateDoppler(float DeltaTime);

	/** 로컬 리스너(카메라)의 위치와 속도. 리스너가 없으면 false. */
	bool ResolveListener(FVector& OutLocation, FVector& OutVelocity) const;

	/** 소리를 낼 수 있는 상황인가. 데디케이티드 서버와 리스너 없는 월드를 거른다. */
	bool CanPlaySound() const;

private:
	/** 주행 중 계속 도는 루프. 비워 두면 엔진음 없이 경적과 도플러만 동작한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Audio", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USoundBase> EngineLoopSound;

	/** 경적. 비워 두면 RequestHorn 이 항상 false 를 돌려준다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Audio", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USoundBase> HornSound;

	/**
	 * 두 소리에 함께 쓰는 감쇠. 비워 두면 애셋 자신의 설정을 따른다.
	 *
	 * 차가 3D 로 들리려면 감쇠가 반드시 있어야 한다. 없으면 맵 반대편 차도 같은 크기로
	 * 들려서, 경적을 아무리 아껴 울려도 시끄럽다는 인상이 그대로 남는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Audio", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USoundAttenuation> SharedAttenuation;

	/** 엔진음 애셋에 넘길 속도 파라미터 이름. 애셋에 같은 이름이 없으면 그냥 무시된다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Audio", meta = (AllowPrivateAccess = "true"))
	FName EngineSpeedParameterName = TEXT("Speed01");

	/** 위 파라미터가 1.0 이 되는 속도(cm/s). 차종 기본 750 의 두 배를 상한으로 잡았다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Audio", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float EngineSpeedReference = 1500.f;

	/** 이 거리(cm) 밖에서는 경적을 시도조차 하지 않는다. 감쇠 반경과 비슷하게 맞춘다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Audio", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float HornAudibleRange = 4000.f;

	/** 같은 차가 다시 울리기까지의 최소 간격(초). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Audio", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float HornCooldownSeconds = 12.f;

	/** 월드 전체에서 경적 사이의 최소 간격(초). 여러 대가 같은 순간에 우는 것을 막는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Audio", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float GlobalHornIntervalSeconds = 3.f;

	/** 도플러를 적용할지. 끄면 피치를 건드리지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Doppler", meta = (AllowPrivateAccess = "true"))
	bool bEnableDoppler = true;

	/**
	 * 도플러 세기. 1 이 물리적으로 정확한 값이고, 그 값은 이 게임 속도에서 안 들린다.
	 * 기본 4 는 기본 주행 속도에서 통과 전후로 1.5 반음쯤 벌어지는 값이다. (계산)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Doppler", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float DopplerIntensity = 4.f;

	/** 피치 하한. 패링으로 튕겨 나간 차처럼 속도가 튀어도 소리가 무너지지 않게 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Doppler", meta = (AllowPrivateAccess = "true", ClampMin = "0.1"))
	float DopplerPitchMin = 0.7f;

	/** 피치 상한. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Doppler", meta = (AllowPrivateAccess = "true", ClampMin = "0.1"))
	float DopplerPitchMax = 1.5f;

	/**
	 * 피치가 목표값을 따라가는 속도.
	 *
	 * 차가 리스너 옆을 스칠 때 상대 속도의 부호가 한 프레임 만에 뒤집힌다. 그대로 적용하면
	 * 피치가 끊기며 딱 소리가 난다. 보간이 그 계단을 없앤다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Doppler", meta = (AllowPrivateAccess = "true", ClampMin = "0.1"))
	float DopplerSmoothingSpeed = 8.f;

	/** 소유 차량. BeginPlay 에서 한 번 캐시한다. */
	UPROPERTY(Transient)
	TObjectPtr<ACWBaseVehicle> OwnerVehicle;

	/** 재생 중인 엔진 루프. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> EngineAudio;

	/** 재생 중인 경적. 도플러를 엔진음과 같이 먹이려고 붙잡아 둔다. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> HornAudio;

	/** 마지막으로 경적을 울린 월드 시각(초). 한 번도 안 울렸으면 음수로 남는다. */
	float LastHornTime = -UE_BIG_NUMBER;

	/** 현재 적용 중인 피치 배수. 보간의 시작점이다. */
	float CurrentDopplerPitch = 1.f;
};
