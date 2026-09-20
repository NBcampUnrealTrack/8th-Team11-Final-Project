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
	 * 경적을 울린다. 세 겹의 게이트(거리·차량 쿨다운·월드 예산)를 모두 통과했을 때만 난다.
	 *
	 * **배경 연출용 입구다.** 게이트가 "가끔 울린다" 를 전제로 하므로 경고에는 쓰지 않는다 -
	 * 4 번에 1 번만 나는 경고는 없느니만 못하다. 접근 경고는 UpdateApproachWarning 이
	 * 게이트를 건너뛰고 따로 울린다.
	 *
	 * 이 함수를 스스로 부르는 곳은 없다. 계기는 BP 에서 붙인다.
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
	/** 지속음(엔진 루프·사이렌)을 붙여 재생한다. 애셋이 없는 쪽은 건너뛴다. */
	void StartLoopSounds();

	/**
	 * 소리 하나를 차량 루트에 붙여 재생하고 그 컴포넌트를 돌려준다. 애셋이 없으면 nullptr.
	 *
	 * 반복 재생 여부는 애셋이 정한다. 여기서는 붙이기만 한다.
	 */
	UAudioComponent* SpawnAttachedLoop(USoundBase* Sound) const;

	/** 속도를 0~1 로 정규화해 엔진음 애셋에 넘긴다. 애셋이 파라미터를 안 쓰면 무시된다. */
	void UpdateEngineParameters();

	/**
	 * 리스너와의 상대 속도로 지금 이 순간의 피치 배수를 구한다. 보간하지 않는다.
	 *
	 * 공식은 엔진의 USoundNodeDoppler::GetDopplerPitchMultiplier 와 같다.
	 * 음속 33000cm/s 에 기본 주행 속도 750cm/s 면 피치 변화가 2.3% 뿐이라
	 * (반음이 5.9%) 세기를 1 로 두면 사실상 안 들린다. DopplerIntensity 가 그 몫이다.
	 *
	 * 세기를 인자로 받는 이유는 지속음마다 알맞은 값이 다르기 때문이다. 엔진 루프는
	 * 들리게 만들려고 부풀린 값을 쓰지만, 사이렌은 기본파가 이미 왕복하는 소리라
	 * 같은 값을 걸면 그 왕복 패턴까지 늘었다 줄었다 한다.
	 *
	 * @return 리스너를 못 찾았거나 분모가 0 이면 false. 이때 OutPitch 는 건드리지 않는다.
	 */
	bool ComputeDopplerPitch(float Intensity, float& OutPitch) const;

	/**
	 * 지속음 하나에 도플러 피치를 보간해 적용한다.
	 *
	 * 피치 상태를 인자로 받는 이유는 지속음마다 세기가 달라 보간 상태도 따로여야 하기
	 * 때문이다. 하나를 공유하면 세기가 큰 쪽이 작은 쪽을 끌고 다닌다.
	 */
	void ApplyLoopDoppler(UAudioComponent* LoopAudio, float Intensity, float& InOutPitch, float DeltaTime);

	/**
	 * 피치 배수를 보간해 **지속음에만** 적용한다. 지금은 엔진 루프와 사이렌 둘이다.
	 *
	 * 경적에는 걸지 않는다. 경적은 1.6 초짜리 원샷이라 재생 도중 피치가 움직이면
	 * 도플러가 아니라 테이프 늘어지는 소리로 들린다. 실측(2026-09-16)으로 재생 내내
	 * -1.4 반음이 미끄러졌고, 원본 애셋의 하모닉은 완전히 수평이었다.
	 * 경적의 피치는 SpawnHorn 이 시작 시점에 한 번 정하고 끝까지 고정한다.
	 *
	 * 반면 엔진 루프는 지속음이라 차가 지나가는 동안 피치가 변하는 것이 맞다.
	 * 보간(DopplerSmoothingSpeed)은 그 변화를 매끄럽게 하려고 있는 것이다.
	 */
	void UpdateDoppler(float DeltaTime);

	/**
	 * 이 차가 로컬 플레이어를 칠 참이면 경고 경적을 울린다.
	 *
	 * **판정 대상은 카메라가 아니라 플레이어 폰이다.** 카메라는 캐릭터보다 688cm 뒤에
	 * 있어서, 카메라로 재면 실제보다 0.8 초쯤 늦게 걸린다.
	 *
	 * **로컬 플레이어만 본다.** 팀원에게 가는 차까지 울리면 4 인 방에서 경적이 네 배가
	 * 되는데, 정작 그 소리가 내 것인지 남의 것인지는 구분할 방법이 없다. 방향은
	 * 스페이셜라이즈가 주지만 "저 차가 나를 노리나" 는 소리로 답할 수 없다.
	 * 각 클라이언트가 자기 화면에서 자기 플레이어로만 판정하므로 복제할 것도 없다.
	 *
	 * **차 한 대는 한 번만 운다.** 플레이어가 경로를 들락거릴 때 경적이 따라 붙었다
	 * 떨어졌다 하는 것을 막는다. 대신 피했다가 다시 들어가면 두 번째 경고는 없다.
	 *
	 * RequestHorn 의 게이트를 타지 않는다. 쿨다운과 월드 예산은 "가끔 울린다" 를 만드는
	 * 값이라, 차 세 대가 동시에 나에게 오는 가장 위험한 순간에 한 대만 울리게 된다.
	 * 대신 위협 판정(ThreatLateralTolerance 120cm)이 좁아서 그것이 자연스러운 상한이 된다.
	 */
	void UpdateApproachWarning();

	/** 게이트 없이 경적을 붙여 재생한다. RequestHorn 과 경고가 함께 쓴다. */
	void SpawnHorn();

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
	 * 사이렌. 비워 두면 아무 일도 일어나지 않는다 - 구급차만 채우면 된다.
	 *
	 * 엔진 루프와 따로 둔 이유는 둘이 동시에 존재할 수 있기 때문이다. 엔진음이 들어오면
	 * 구급차는 엔진과 사이렌을 같이 내야 한다. 한 칸을 돌려쓰면 그때 하나를 포기해야 한다.
	 *
	 * 여기 꽂는 애셋은 **반복 재생이 켜져 있어야 한다.** 원샷을 꽂으면 한 번 울고 만다.
	 * 그리고 경적과 같은 동시성 애셋을 물리면 안 된다 - 루프는 끝나지 않아서 경적 자리를
	 * 영구히 점유한다. 사이렌 전용 동시성을 따로 만들어 Cue 에 물린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Audio", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USoundBase> SirenSound;

	/**
	 * 세 소리(엔진 루프·경적·사이렌)에 함께 쓰는 감쇠. 비워 두면 애셋 자신의 설정을 따른다.
	 *
	 * 차가 3D 로 들리려면 감쇠가 반드시 있어야 한다. 없으면 맵 반대편 차도 같은 크기로
	 * 들려서, 경적을 아무리 아껴 울려도 시끄럽다는 인상이 그대로 남는다.
	 *
	 * **최대 어테뉴에이션(dB)은 -60 이하로 둘 것.** -45 였을 때 사이렌이 감쇠 경계를
	 * 넘는 순간 그 음량에서 그냥 잘려 뚝 소리가 났다(2026-09-20 실기). 경적은 원샷이라
	 * 재생 중에 경계를 넘을 일이 드물어 드러나지 않았을 뿐, 같은 구멍이었다.
	 *
	 * 사이렌까지 같은 곡선을 쓰는 것은 아직 검증 안 됐다. 사이렌은 경적보다 멀리서부터
	 * 들려야 자연스러운 소리라, 지금 곡선이 짧게 느껴지면 사이렌용 감쇠를 따로 두는 것이
	 * 다음 수순이다. 다만 그 곡선은 접근 경고가 들리는 거리(2.5초 앞 = 구급차 기준 36.9m)를
	 * 같이 정하므로, 경적과 나눠야 건드릴 수 있다.
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

	/** 로컬 플레이어에게 다가올 때 경적으로 미리 알릴지. 끄면 경고가 아예 안 난다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Warning", meta = (AllowPrivateAccess = "true"))
	bool bEnableApproachWarning = true;

	/**
	 * 충돌까지 이만큼(초) 남았을 때 경고를 울린다.
	 *
	 * 화면이 주는 예고가 1.81 초다(탑뷰 카메라 실측: 캐릭터 앞 1364cm ÷ 750cm/s).
	 * 같은 값으로 두면 소리가 화면보다 먼저 오는 것이 아니라 같이 와서 경고가 되지 못한다.
	 * 2.5 초는 화면에 들어오기 전에 "온다" 를 먼저 알리는 몫이다.
	 *
	 * 거리가 아니라 시간으로 잡는 이유: 차 속도를 바꿔도 예고 시간이 저절로 일정해진다.
	 * 거리로 잡으면 속도를 올릴 때마다 여기도 같이 고쳐야 한다.
	 *
	 * 늘릴 때는 감쇠도 같이 봐야 한다. 2.5 초 x 최고 속도 880cm/s = 22m 인데,
	 * 그 거리에서 안 들리면 경고가 아니라 없는 것과 같다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Warning", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float ApproachWarningLeadSeconds = 2.5f;

	/** 도플러를 적용할지. 끄면 피치를 건드리지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Doppler", meta = (AllowPrivateAccess = "true"))
	bool bEnableDoppler = true;

	/**
	 * 도플러 세기. 1 이 물리적으로 정확한 값이고, 그 값은 이 게임 속도에서 안 들린다.
	 * 기본 4 는 기본 주행 속도에서 통과 전후로 1.5 반음쯤 벌어지는 값이다. (계산)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Doppler", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float DopplerIntensity = 4.f;

	/**
	 * 사이렌에 쓰는 도플러 세기. 엔진 루프와 따로 둔다.
	 *
	 * 기본 1 은 물리적으로 정확한 값이다. 엔진 루프에서 4 를 쓰는 근거("1 로 두면 안 들린다")가
	 * 사이렌에는 적용되지 않는데, 사이렌은 기본파가 800~1500Hz 를 0.31 초 주기로 왕복하는
	 * 소리라 피치를 부풀리면 그 왕복 패턴 자체가 늘었다 줄었다 하기 때문이다. 도플러가
	 * 아니라 테이프가 헐거운 소리로 들린다. 2026-09-16 에 경적에서 같은 실패를 한 적이 있다.
	 *
	 * 올리고 싶으면 귀로 확인하며 조금씩 올린다. (애셋 실측, 인게임 청취는 미검증)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Doppler", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float SirenDopplerIntensity = 1.f;

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

	/** 재생 중인 사이렌. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> SirenAudio;

	// 경적 컴포넌트는 붙잡아 두지 않는다. 피치는 시작 시점에 한 번 정하고 끝까지 고정이라
	// (SpawnHorn) 재생 중에 다시 만질 일이 없고, 원샷이라 끝나면 알아서 사라진다.

	/** 마지막으로 경적을 울린 월드 시각(초). 한 번도 안 울렸으면 음수로 남는다. */
	float LastHornTime = -UE_BIG_NUMBER;

	/** 이 차가 로컬 플레이어에게 이미 경고했는지. 한 번 서면 다시 내려가지 않는다. */
	bool bWarnedLocalPlayer = false;

	/** 엔진 루프에 현재 적용 중인 피치 배수. 보간의 시작점이다. */
	float CurrentDopplerPitch = 1.f;

	/** 사이렌에 현재 적용 중인 피치 배수. 세기가 달라 엔진 루프와 따로 보간한다. */
	float CurrentSirenDopplerPitch = 1.f;
};
