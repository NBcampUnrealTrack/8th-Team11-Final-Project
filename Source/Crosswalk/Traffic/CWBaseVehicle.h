// Copyright Crosswalk Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CWTrafficVehicleSpawnParams.h"
#include "Data/CWVehicleMeshVariant.h"
#include "Interfaces/Interface_ParryReceiver.h"
#include "CWBaseVehicle.generated.h"

class ACWBaseVehicle;
class ACWTrafficLane;
class UBoxComponent;
class UCWVehicleStopSyncComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * 차량이 정지선 앞에 서거나 다시 출발한 순간. 서버·클라이언트 양쪽에서 각자 뜬다.
 *
 * 정지 구간은 DistanceAlongLane 이 변하지 않아서 복제가 흐르지 않는 구간이다.
 * 보정이 필요한 쪽은 이 알림으로 "지금 이 차는 서 있고, 서야 할 자리는 여기다" 를 받는다.
 * 매 틱 값을 비교할 필요가 없도록 전이하는 순간에만 나간다.
 *
 * 앞차에 막혀 선 것은 여기 들어오지 않는다. 그쪽은 앞차가 움직이면 따라 풀리므로
 * 복제가 계속 흐르고, 정지선처럼 값이 얼어붙지 않는다.
 *
 * @param Vehicle             상태가 바뀐 차량.
 * @param bStoppedAtStopLine  섰으면 true, 다시 출발했으면 false.
 * @param StopTargetDistance  서야 하는 차선 위 진행 거리(cm). 출발 알림에서는 -1 이다.
 *                            (통행이 허가돼서 풀린 것이라 그 시점엔 볼 정지선이 없다)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCWOnVehicleStopStateChangedSignature, ACWBaseVehicle*, Vehicle, bool, bStoppedAtStopLine, float, StopTargetDistance);

/**
 * 차선 스플라인을 따라 직선 주행하는 차량의 공통 베이스.
 * 승용차 / 오토바이 / 트럭 등 차종은 이 클래스를 상속한 BP로 만든다.
 *
 * 차량은 플레이어를 추적하지 않는다 - 거동이 예측 가능해야 회피/패링 학습이 성립한다.
 * 개체별 편차(속도 배율, 좌우 오프셋)는 스포너가 굴려서 넘긴다.
 * 앞차와는 최소 간격을 유지하므로 같은 차선에서 서로 통과하지 않는다.
 *
 * 네트워크 — 트랜스폼을 복제하지 않고 차선 위 진행 거리만 복제한다.
 *
 *   트랜스폼 복제(SetReplicateMovement)를 쓰면 클라이언트가 매 업데이트마다 순간이동한다.
 *   무브먼트 컴포넌트가 없는 순수 AActor 는 AActor::PostNetReceiveLocationAndRotation 이
 *   받은 위치를 그대로 SetActorLocationAndRotation 하는 것이 전부이고, 그 경로에 보간이 없다.
 *   속도를 같이 실어 보내도 AActor::PostNetReceiveVelocity 는 물리 예측이 켜져 있을 때만
 *   동작해서 그냥 버려진다. 업데이트 빈도를 올리는 것 말고는 손댈 구석이 없다.
 *
 *   그래서 위치가 아니라 "차선 위 어디까지 갔는가"(DistanceAlongLane)를 복제한다.
 *   차선 스플라인은 레벨에 배치된 액터라 클라이언트도 똑같이 갖고 있으므로,
 *   진행 거리만 있으면 위치와 회전을 각자 계산할 수 있다.
 *
 *   업데이트 사이는 클라이언트가 스스로 메운다. 주행 속도를 결정하는 값(Speed 는 차종 BP,
 *   SpeedMultiplier 는 복제, 앞차 간격은 서브시스템의 차량 목록)이 양쪽에 모두 있어서
 *   같은 Tick 코드를 그대로 돌릴 수 있다. 서버 값이 도착하면 그 값으로 맞추는데, 추정 속도가
 *   서버와 같으므로 보정량이 거의 0 이다. 차가 앞차에 막혀 서는 구간에서만 조금 어긋난다.
 *
 *   덤으로 대역폭도 줄어든다. 위치+회전 대신 float 하나이고, 업데이트 빈도도 낮출 수 있다.
 *
 *   패링당해 차선을 벗어난 구간은 진행 거리로 표현할 수 없어서, 튕겨나간 지점과 방향을
 *   따로 복제하고 클라이언트가 그 지점에서 직진을 시작한다. (bParried / ParriedLaunch*)
 */
UCLASS()
class CROSSWALK_API ACWBaseVehicle : public AActor, public IInterface_ParryReceiver
{
	GENERATED_BODY()

public:
	ACWBaseVehicle();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 주행은 서버·클라이언트가 같은 코드로 각자 돈다. 진행도 확정과 소멸만 서버 전용이다. */
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * 실제 이동 속도(cm/s). 서버·클라이언트 양쪽의 Tick 이 각자 채운다.
	 *
	 * 스플라인 추종은 SetActorLocationAndRotation 으로 위치를 덮어써서 물리도 무브먼트
	 * 컴포넌트도 속도를 채워주지 않는다. 그대로 두면 애니메이션·나이아가라·사운드처럼
	 * GetVelocity 를 읽는 쪽이 전부 0 을 받는다.
	 *
	 * 리플리케이션과는 무관하다. 트랜스폼을 복제하지 않으므로 GatherCurrentMovement 가
	 * 이 값을 읽어가지 않는다.
	 */
	virtual FVector GetVelocity() const override { return CurrentVelocity; }

	/**
	 * 서버 권한 패링 판정. 대상 선별은 트래픽 서브시스템이 끝낸 뒤 이 차량에게만 들어온다.
	 * 반경·각도는 선별 단계에서 걸러지므로, 여기서는 차량이 단일 관리하는 윈도우만 다시 본다.
	 */
	virtual bool ReceiveParryAttempt_Implementation(const FCWParryAttemptContext& ParryAttemptContext) override;

	/** 스포너가 스폰 직후 호출. 주행할 차선과 개체별 편차를 지정한다. */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Vehicle")
	void InitializeOnLane(ACWTrafficLane* InLane, const FCWTrafficVehicleSpawnParams& Params);

	/**
	 * 이 차량이 내려고 하는 속도(cm/s). 차종 기본 속도에 개체 배율을 적용한 값이다.
	 * 튕겨나가는 중이면 패링 배율까지 곱한다.
	 *
	 * 앞차에 막혀 느려지거나 서 있는 것은 반영하지 않는다. 지금 실제로 내고 있는 속도는
	 * GetCurrentSpeed 를 쓴다. 튕겨나간 뒤에는 앞차를 보지 않으므로 두 값이 같아진다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Vehicle")
	float GetEffectiveSpeed() const { return Speed * SpeedMultiplier * (bParried ? ParriedSpeedMultiplier : 1.f); }

	/**
	 * 지금 실제로 내고 있는 속도(cm/s). 앞차에 막혀 느려졌으면 그만큼 낮고, 서 있으면 0 이다.
	 *
	 * "이 차가 실제로 나를 칠 것인가" 를 묻는 자리는 이 값을 봐야 한다.
	 * 희망 속도로 판정하면 앞차에 막혀 멈춰 선 차량까지 임박한 위협으로 잡혀서,
	 * 서 있는 차를 패링할 수 있게 된다.
	 *
	 * 매 Tick 이 채우는 값이라 서버·클라이언트 양쪽에서 쓸 수 있고 추가 계산이 없다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Vehicle")
	float GetCurrentSpeed() const { return CurrentVelocity.Size(); }

	/** 콜리전 박스 기준 차량 전체 길이(cm). 차간 거리 계산의 기준값. */
	UFUNCTION(BlueprintPure, Category = "Traffic|Vehicle")
	float GetVehicleLength() const;

	/**
	 * 차종 기본 주행 속도(cm/s). 앞차나 개체 편차가 반영되기 전의 값이다.
	 *
	 * 실제로 얼마나 빨리 오는지는 GetCurrentSpeed 가 답하지만, 그것은 굴러가는 차에만
	 * 있는 값이다. 배치 단계에서 "이 차종이 한 지점을 지나가는 데 몇 초 걸리는가" 를
	 * CDO 로 미리 계산하려면 편차가 섞이지 않은 이 값이 필요하다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Vehicle")
	float GetBaseSpeed() const { return Speed; }

	/**
	 * 차체 표면에서 Point 까지의 거리(cm). 박스 안쪽이면 0.
	 * 근접 판정에 원점 거리를 쓰면 트럭이 승용차보다 불리해지므로 표면 기준으로 잰다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Vehicle")
	float GetDistanceToPoint(const FVector& Point) const;

	/** 피격·패링 연출 분기에 쓰이는 차종 ID. 수신 측이 차종별 분기에 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Traffic|Vehicle")
	FName GetVehicleType() const { return VehicleType; }

	UFUNCTION(BlueprintPure, Category = "Traffic|Vehicle")
	ACWTrafficLane* GetCurrentLane() const { return CurrentLane; }

	UFUNCTION(BlueprintPure, Category = "Traffic|Vehicle")
	float GetDistanceAlongLane() const { return DistanceAlongLane; }

	/**
	 * 차선 위 진행 거리를 Delta(cm) 만큼 민다. UCWVehicleStopSyncComponent 전용이다.
	 *
	 * 값만 열어 준다. 얼마나 밀지, 한 번에 밀지 나눠 밀지 같은 판단은 전부 그 컴포넌트가 한다.
	 * 여기서 판단까지 하면 정지 구간의 보정이 주행 로직과 같은 파일에 섞인다.
	 *
	 * 블루프린트에 열지 않는다. 이 값은 위치 그 자체라, 아무 데서나 밀 수 있으면
	 * 차가 어디서 왜 움직였는지를 추적할 수 없게 된다.
	 */
	void AdjustDistanceAlongLane(float Delta) { DistanceAlongLane += Delta; }

	/**
	 * 지금 정지선 때문에 서 있는가. 서버·클라이언트 각자의 판단이라 두 값이 다를 수 있고,
	 * 다른 것 자체가 어긋남의 신호다.
	 *
	 * 앞차에 막혀 선 것은 false 다. 두 정지는 풀리는 방식이 달라서 섞으면 안 된다.
	 * 앞차는 앞이 움직이면 따라 풀리지만, 정지선은 신호가 바뀔 때까지 값이 얼어붙는다.
	 *
	 * 바뀌는 순간을 잡으려면 매 틱 비교하지 말고 OnStopStateChanged 를 쓴다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Vehicle")
	bool IsStoppedAtStopLine() const { return bStoppedAtStopLine; }

	/**
	 * 이 차가 서야 하는 차선 위 진행 거리(cm). 보고 있는 정지선이 없으면 -1.
	 *
	 * DistanceAlongLane 과 같은 좌표계라 그대로 대입하면 정확히 서야 할 자리에 놓인다.
	 * 정지 구간의 보정은 서버가 보낸 값을 기다리지 말고 이 값을 목표로 삼는 편이 낫다.
	 * 정지선 위치도 차체 길이도 양쪽이 같은 값을 갖고 있어서, 계산 결과가 애초에 같다.
	 *
	 * 감속을 시작하기 전에도 값이 나온다. 서 있는지까지 물으려면 IsStoppedAtStopLine 을 같이 본다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Vehicle")
	float GetStopTargetDistance() const { return StopTargetDistance; }

	/**
	 * Target이 이 차량의 진행 경로 위에 있을 때 충돌까지 남은 시간(초)을 낸다.
	 * 경로 밖이거나 이미 지나쳤으면 false.
	 *
	 * @param ViewRewindSeconds 차량을 몇 초 되돌린 상태로 볼 것인가. (랙 보정)
	 *                          0 이면 서버의 지금 상황을 그대로 본다.
	 *                          되돌린 시점에는 차가 이만큼 더 뒤에 있었으므로 남은 시간도 그만큼 길다.
	 *                          이 값을 그대로 쓰지는 않는다. 등속 직진 가정이 성립하는
	 *                          구간까지만 되돌린다. (ResolveEffectiveRewindSeconds)
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Parry")
	bool PredictTimeToImpact(const AActor* Target, float& OutTimeToImpact, float ViewRewindSeconds = 0.f) const;

	/**
	 * 패링 가능 차종인지. 트럭·긴급차량은 자식 BP에서 bCanBeParried를 끈다.
	 * 이미 튕겨나가는 중이어도 막지 않는다. 날아오는 차를 다른 플레이어가 다시 쳐낼 수 있어야 한다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Parry")
	bool CanBeParried() const { return bCanBeParried; }

	/** 패링 입력이 유효 판정되는 충돌 전 시간(초). 기획 값이므로 차량 쪽에서 단일 관리한다. */
	UFUNCTION(BlueprintPure, Category = "Traffic|Parry")
	float GetParryWindow() const { return ParryWindow; }

	/**
	 * 패링당하는 순간 서버·클라이언트 모두에서 호출된다. 차량 BP 에서 구현해 연출을 붙이는 자리다.
	 * 이펙트, 사운드, 트레일, 카메라 흔들림 전부 여기서 시작하면 되고 C++ 수정은 필요 없다.
	 *
	 * 호출 시점은 차량이 이미 새 방향으로 회전한 뒤다. 그래서 GetActorForwardVector 가
	 * 곧 날아갈 방향이고, LaunchDirection 도 같은 값이다.
	 *
	 * @param LaunchDirection  차량이 날아가는 방향(정규화, 수평).
	 * @param LaunchSpeed      그 순간의 실제 주행 속도(cm/s). 연출 세기 환산에 쓴다.
	 * @param Parrier          쳐낸 플레이어. 그 사람 화면에서만 카메라를 흔들고 싶으면
	 *                         BP 에서 이 액터가 로컬 조종 대상인지 확인하면 된다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Traffic|Parry")
	void OnParriedFX(const FVector& LaunchDirection, float LaunchSpeed, AActor* Parrier);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 차선 끝에 도달했거나 패링 소멸 타이머가 끝났을 때. 지금은 Destroy 가 전부다. */
	virtual void OnReachedLaneEnd();

private:
	/**
	 * 앞차를 추월해 통과하지 않도록 주행 속도를 제한한다. 앞차가 없으면 제 속도를 그대로 낸다.
	 *
	 * 프레임 시간을 받지 않는다. 앞차와의 거리만 보고 정해야 프레임 간격이 흔들려도 속도가
	 * 같이 흔들리지 않고, 서버와 클라이언트가 같은 상황에서 같은 값을 낸다.
	 *
	 * @param OutStopLineLimited     이번 속도를 정한 것이 정지선인지. 정지 상태 판정이 "앞차에 막힌 것"과
	 *                               구분하는 데 쓴다. 속도 자체는 이 값과 무관하므로 nullptr 이어도 된다.
	 * @param OutStopTargetDistance  서야 하는 차선 위 진행 거리(cm). 보고 있는 정지선이 없으면 -1.
	 *                               남은 거리가 아니라 목표 지점이다. 남은 거리는 진행 거리를 빼면 나오는데,
	 *                               목표 지점은 이 차가 어디까지 왔든 같은 값이라 보정의 기준으로 쓸 수 있다.
	 */
	float ResolveStepSpeed(bool* OutStopLineLimited = nullptr, float* OutStopTargetDistance = nullptr) const;

	/**
	 * 멈춰야 할 지점까지 Remaining 만큼 남았을 때 낼 속도. 앞차와 정지선이 각자 부른다.
	 *
	 * @param Remaining      서야 하는 자리까지 남은 거리(cm). 0 이하면 정지다.
	 * @param BrakeDistance  속도를 줄이는 데 쓸 구간(cm). 이만큼 남아 있으면 제 속도다.
	 */
	static float ResolveApproachSpeed(float Remaining, float BrakeDistance, float DesiredSpeed);

	/**
	 * 현재 진행 거리와 좌우 오프셋을 실제 트랜스폼에 반영한다.
	 * StepSpeed는 이번 프레임에 실제로 이동한 속도(cm/s). GetVelocity 가 돌려줄 값에 쓰인다.
	 */
	void UpdateTransformOnLane(float StepSpeed = 0.f);

	/** 고른 변형의 메쉬와 콜리전 크기를 반영한다. 인덱스가 범위 밖이면 아무것도 하지 않는다. */
	void ApplyMeshVariant();

	/** 서버가 고른 변형이 클라이언트에 도착했을 때. */
	UFUNCTION()
	void OnRep_MeshVariantIndex();

	/**
	 * 튕겨나간 지점과 방향이 클라이언트에 도착했을 때. 차선 추종을 끊고 그 자세로 맞춘다.
	 *
	 * 회전은 복제되지 않으므로 여기서 ParriedLaunchDirection 을 보고 직접 세워야 한다.
	 * 그냥 두면 클라이언트에서는 차가 차선 방향 그대로 옆으로 미끄러지는 것처럼 보인다.
	 *
	 * bParried 가 아니라 ParryLaunchSerial 에 달아야 한다. 이유는 그 프로퍼티 주석에 적어 뒀다.
	 */
	UFUNCTION()
	void OnRep_ParryLaunch();

	/** 오버랩 정보에서 피격 지점을 뽑는다. 스윕이 아니면 최근접점으로 대체한다. */
	FVector ResolveHitLocation(const AActor* HitActor, bool bFromSweep, const FHitResult& SweepResult) const;

	/**
	 * 패링당했을 때 날아갈 방향을 정한다. 기준점은 플레이어가 아니라 이 차량이다.
	 *
	 * 커서 지점을 못 쓰면 플레이어 기준 방향으로, 그것도 비면 TryParry 가 온 길로 되돌려 보낸다.
	 */
	FVector ResolveLaunchDirection(const FCWParryAttemptContext& ParryAttemptContext) const;

	/**
	 * 패링 판정을 확정하고 차량을 LaunchDirection 으로 튕겨낸다. 성공하면 true.
	 *
	 * 들어오는 길은 ReceiveParryAttempt 하나뿐이다. 그 앞에 서브시스템의 선별(차종·반경·각도)이
	 * 있으므로 여기서는 윈도우만 다시 본다. 블루프린트에 열지 않는다 - 선별을 건너뛰고
	 * 직접 부르는 길이 하나 더 있으면, 패링이 왜 됐는지 안 됐는지를 두 경로에서 따져야 한다.
	 *
	 * @param ViewRewindSeconds 랙 보정량. 선별 단계와 같은 값을 넘겨야 한다.
	 *                          다른 값을 넘기면 선별은 통과했는데 여기서 떨어지는 일이 생긴다.
	 */
	bool TryParry(AActor* Parrier, const FVector& LaunchDirection, float ViewRewindSeconds);

	/**
	 * 실제로 되돌릴 시간(초). 넘어온 보정량을 등속 직진 가정이 성립하는 구간으로 깎는다.
	 *
	 * PredictTimeToImpact 의 랙 보정은 "이 차는 지금 진행 방향으로 등속 직진해왔다"를
	 * 가정한다. 패링당한 차량은 그 순간 방향이 꺾이므로, 그 가정은 발사 시점 이후로만 참이다.
	 *
	 * 선별 단계와 TryParry 가 같은 값으로 판정해야 하므로 두 호출자 대신 여기서 깎는다.
	 */
	float ResolveEffectiveRewindSeconds(float ViewRewindSeconds) const;

	/**
	 * 패링 연출을 모든 머신에서 재생한다. 연출 전용이고 게임플레이 상태는 건드리지 않는다.
	 *
	 * 차량이 실제로 튕겨나가는 것은 bParried / ParriedLaunch* 복제가 담당한다.
	 * 상태를 이 RPC 로 넘기지 않는 이유: 패링 순간에 관련성 밖이었다가 뒤늦게 들어온
	 * 클라이언트는 이 RPC 를 못 받는다. 그때도 복제 프로퍼티는 현재 값이 그대로 도착한다.
	 *
	 * 그 분리가 성립하므로 Unreliable 이다. 유실이 게임플레이를 갈라놓지 않는데 Reliable 로
	 * 두면 유실분이 재전송 큐에 쌓여 신뢰 채널을 먹는다. 놓친 클라이언트는 이번 연출 한 번을
	 * 못 보는 것이 전부이고, 차량이 어디로 날아가는지는 복제 프로퍼티가 그대로 맞춘다.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayParriedFX(FVector_NetQuantizeNormal LaunchDirection, float LaunchSpeed, AActor* Parrier);

	/**
	 * 이 대상을 지금 칠 수 있으면 치고 기록한다. 서버에서만 유효하다.
	 *
	 * 오버랩이 시작될 때와, 느렸다가 다시 빨라진 순간 두 곳에서 들어온다.
	 * 두 경로가 같은 판정을 써야 하므로 조건을 여기 한 곳에 모은다.
	 */
	void TryApplyHit(AActor* OtherActor, bool bFromSweep, const FHitResult& SweepResult);

	/**
	 * 이 피격이 대상의 화면에서도 실제로 닿았는지 본다. 통과하면 지금 쳐도 되는 피격이다.
	 *
	 * 서버는 "지금 차 위치"로 겹침을 잡지만, 대상의 화면 속 차는 그 사람 핑만큼 뒤에 있다.
	 * 차를 그만큼 되돌렸을 때 아직 대상에 닿지 않았다면, 화면에서는 피한 것이므로 흘려보낸다.
	 *
	 * 판정식: (차가 대상을 파고든 깊이) < (핑 동안 차가 이동한 거리) 이면 보류한다.
	 *   되돌린 차 앞면이 아직 대상 뒤에 있다는 뜻이라, 그 사람 화면에선 차가 안 왔다.
	 *
	 * 가만히 선 대상은 차가 계속 파고들어 깊이가 이동 거리를 넘는 순간 통과한다. 그래서
	 * 매 틱 재검사가 필요하다. (RetryPendingHits) 보류를 한 번만 하고 끝내면 그 사람은
	 * 핑이 높을수록 안 맞는 무적이 되어 버린다.
	 *
	 * 되돌릴 양이 0 인 대상(호스트 조종/랜)은 깊이가 음수 미만이 될 수 없어 항상 통과한다.
	 * 즉 보정 이전과 동일하게 동작한다.
	 */
	bool PassesHitLagCompensation(const AActor* Target) const;

	/**
	 * 이 대상의 화면이 서버보다 얼마나 뒤처져 있는가(초). 차를 되돌릴 양이다.
	 *
	 * 패링의 UCWParryComponent::ResolveViewRewindSeconds 와 같은 근거(그 사람 핑 전체)를
	 * 쓴다. 판정 주체가 쳐내는 사람이 아니라 맞는 사람이라 여기서 대상 폰을 보고 다시 구한다.
	 * 호스트가 직접 조종하는 대상은 0 이다. 자기가 보는 것이 곧 서버 상황이다.
	 */
	float ResolveTargetViewRewindSeconds(const AActor* Target) const;

	/**
	 * 겹쳐 있지만 아직 못 친 대상을 서버 틱마다 다시 판정한다. (서버 전용)
	 *
	 * 왜 필요한가
	 *   오버랩 이벤트는 겹치기 시작할 때 한 번만 온다. 그래서 그 순간에 조건이 안 맞아
	 *   흘려보낸 피격은, 나중에 조건이 갖춰져도 새 이벤트가 없어서 영영 판정되지 않는다.
	 *   흘려보내는 경로가 둘이라 둘 다 여기서 다시 본다.
	 *
	 *   1. 속도가 MinHitSpeed 아래 - 멈춰 있는 차에 걸어 들어간 사람이 그 차가 다시
	 *      출발해도 맞지 않는다. "차 안에 서 있으면 안전" 이 되어 버린다.
	 *   2. 랙 보정으로 보류 - 그 사람 화면에서 아직 차가 안 닿았다. 몸을 빼면 그대로
	 *      피한 것이고, 가만히 있으면 화면에서 차가 닿는 순간 여기서 성립해야 한다.
	 *
	 * 매 틱 도는 비용은 겹쳐 있는 대상 수만큼이다. 대부분의 차량은 이 집합이 비어 있어
	 * 곧바로 빠져나간다.
	 */
	void RetryPendingHits();

	/**
	 * 정지 상태 전이를 확정한다. 값이 바뀐 순간에만 델리게이트와 진단 로그가 나간다.
	 *
	 * 서버·클라이언트가 각자 부른다. 두 로그의 진행거리를 빼면 그 차가 양쪽에서 몇 cm
	 * 어긋난 자리에 섰는지가 바로 나오고, 클라이언트 쪽 줄이 아예 없으면 그 화면에서는
	 * 차가 서지 않았다는 뜻이다.
	 *
	 * 로그를 꺼 두었어도 상태와 델리게이트는 그대로 돈다. 진단 스위치가 보정 동작을
	 * 바꾸면 "로그를 켜니까 증상이 사라진다" 가 되어서 실측 자체가 성립하지 않는다.
	 */
	void UpdateStopState(bool bNowStopped);

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/**
	 * 차에서 떨어진 대상을 피격 기록에서 지운다. 다시 닿으면 또 맞아야 하기 때문이다.
	 * 기록을 영구히 들고 있으면 한 번 스친 사람은 그 차에 두 번 다시 맞지 않는다.
	 */
	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

protected:
	/** 플레이어 충돌 판정용. 플레이스홀더 단계에서는 이 박스가 곧 차량 크기다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Vehicle")
	TObjectPtr<UBoxComponent> CollisionBox;

	/** 플레이스홀더 메쉬(직육면체 Box). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Vehicle")
	TObjectPtr<UStaticMeshComponent> VehicleMesh;

	/**
	 * 정지선 앞에 선 동안 서버-클라 위치 오차를 좁힌다. (보정 담당은 전부 이 컴포넌트 안)
	 *
	 * 서버·클라이언트 양쪽에서 돈다. 트랜스폼을 복제하지 않는 구조라 클라이언트 화면의
	 * 차량 위치는 클라이언트 자신이 계산한 DistanceAlongLane 이 만든다. 서버에서만 돌리면
	 * 정작 고쳐야 할 화면이 안 고쳐진다.
	 *
	 * 치팅 통로가 아니다. 충돌·패링 판정은 서버가 서버 자신의 DistanceAlongLane 으로 따로
	 * 계산하고, 차가 다시 움직이면 복제가 다시 흘러 클라이언트 값을 덮는다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Vehicle")
	TObjectPtr<UCWVehicleStopSyncComponent> StopSyncComponent;

	/**
	 * 외형만 다른 변형 목록. 스폰할 때 서버가 하나를 골라 씌운다.
	 *
	 * 같은 차종 안에서 눈에 보이는 차만 갈아 끼우는 용도다. 속도나 피격 프로파일이 다른 차는
	 * 여기 넣지 말고 별도 차종 BP로 만든다.
	 *
	 * 메쉬마다 콜리전 크기를 따로 들고 있다. 이유는 FCWVehicleMeshVariant 주석 참고.
	 *
	 * 비워 두면 아무것도 하지 않는다. BP에서 VehicleMesh에 직접 지정한 메쉬가 그대로 남는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Vehicle")
	TArray<FCWVehicleMeshVariant> MeshVariants;

	/** 차종 기본 주행 속도(cm/s). 플레이어 기본 이동속도 500의 1.5배. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0"))
	float Speed = 750.f;

	/**
	 * 피격 연출 분기에 쓰이는 차종 ID. FCWVehicleHitContext로 전달된다.
	 * 차종 BP마다 다른 값을 지정해야 피격 프로파일이 제대로 잡힌다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle")
	FName VehicleType = TEXT("Sedan");

	/** 대형 트럭처럼 납작해지는 특수 피격을 쓸지. 차종 BP에서 켠다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle")
	bool bFlattenOnHit = false;

	/**
	 * 피격이 성립하는 최소 주행 속도(cm/s). 이보다 느린 차는 몸이 닿아도 치지 않는다.
	 *
	 * 앞차에 막혀 서 있는 차에 걸어 들어갔을 때 다운되는 것을 막는다. 넉백 세기는 속도에
	 * 비례하므로 이미 0 에 가깝지만, 다운과 페인트 통 파손은 켜지고 꺼지는 값이라
	 * 속도가 0 이어도 그대로 일어난다. 세기만 줄여서는 해결되지 않는다.
	 *
	 * 100 인 이유: 플레이어 기본 이동속도가 500 이라 그 5분의 1 이다. 이보다 느리게
	 * 굴러오는 차를 "치였다" 로 보기는 어렵고, 정상 주행(750)과는 충분히 멀다.
	 *
	 * 0 을 넣으면 이 판정이 꺼지고 예전처럼 속도와 무관하게 친다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0"))
	float MinHitSpeed = 100.f;

	/**
	 * 피격 랙 보정으로 차량을 되돌릴 수 있는 상한(초). 0 이면 보정을 아예 끈다.
	 *
	 * 피격 판정은 서버의 "지금 차 위치"로 하지만, 그 사람 화면의 차는 왕복 시간(핑)만큼
	 * 뒤처져 있다. 보정을 하지 않으면 화면에서 분명히 피한 사람이 서버에서는 맞는다.
	 * 그래서 판정 직전에 차를 그 사람이 본 위치까지 되돌려, 화면에서 아직 닿지 않았던
	 * 피격은 흘려보낸다. (TryApplyHit 의 랙 보정 게이트)
	 *
	 * 패링의 MaxViewRewindSeconds 와 같은 0.2 로 맞춘다. 두 판정이 다른 상한을 쓰면
	 * "패링은 됐는데 그대로 맞는다" 처럼 같은 순간을 서로 다르게 보는 엇갈림이 생긴다.
	 * 되돌리는 근거(그 사람 핑)와 되돌리는 대상(같은 차량)이 같으므로 상한도 같아야 한다.
	 *
	 * 이 값을 넘게 늦은 사람은 그만큼은 서버의 지금 상황으로 판정된다. 무제한으로 되돌리면
	 * 남들 화면에서 한참 전에 지나간 차에 뒤늦게 맞는 것으로 보이기 때문이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxHitRewindSeconds = 0.2f;

	/** 앞차 뒤범퍼와 유지할 최소 여유 거리(cm). 차 길이에 더해서 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0"))
	float FollowingGap = 150.f;

	/**
	 * 정지선 앞에 남길 거리(cm). 앞 범퍼와 정지선 사이의 간격이다.
	 *
	 * FollowingGap 과 분리한 이유: 실제 도로에서도 앞차를 따라가는 거리와 정지선 앞에
	 * 서는 거리는 다르다. 앞차 간격(150cm)을 그대로 쓰면 차가 정지선에서 그만큼 멀리
	 * 서고, 차 길이 절반까지 더해져 정지선보다 370cm 뒤에 멈춘 것처럼 보인다.
	 *
	 * 감속이 시작되는 거리는 StopLineBrakeDistance 가 따로 정한다. 이 값을 줄여도
	 * 급제동이 되지 않고, 서는 자리만 정지선에 가까워진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0"))
	float StopLineGap = 60.f;

	/**
	 * 정지선 앞에서 속도를 줄이는 데 쓰는 구간(cm). 이만큼 남았을 때부터 감속이 시작된다.
	 *
	 * **FollowingGap 과 값이 크게 다른 것이 정상이다.** 앞차는 나와 비슷한 속도로
	 * 달리고 있어서 상대속도가 0 에 가깝고 150cm 로 충분히 흡수된다. 정지선은 상대속도가
	 * 주행 속도 그대로라 같은 값을 쓰면 750cm/s 를 150cm 안에서 죽여야 하고, 시간상수가
	 * 0.2초인 급제동이 된다 - 부드럽게 서는 것이 아니라 벽에 박힌 것처럼 보인다.
	 *
	 * 기본값 900 은 감속도 3m/s^2 기준이다. v^2 / 2a = 7.5^2 / 6 = 9.4m.
	 * 차선이 짧아 900cm 를 못 잡는 맵에서는 줄여야 하는데, 그만큼 제동이 급해진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0"))
	float StopLineBrakeDistance = 900.f;

	/** 패링 판정 윈도우(초). 충돌 직전 이 시간 안에 입력해야 성공. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry", meta = (ClampMin = "0.0"))
	float ParryWindow = 0.3f;

	/**
	 * 위협 판정 시 허용하는 좌우 이탈 폭(cm).
	 * 진행 경로에서 이만큼 벗어난 대상은 패링 후보로 보지 않는다.
	 *
	 * 폭이 아니라 반경이다. PredictTimeToImpact 가 LateralOffset 의 크기와 비교하므로
	 * 좌우로 각각 120cm, 통로로 치면 240cm 다.
	 *
	 * 120 인 이유의 기준은 핵심 재미의 "협동"이다. 이 값이 남의 앞을 막아주는 범위와
	 * 자기 앞만 막는 범위를 가른다. 넓히면 옆 차선의 차까지 후보로 들어와 아무나 쳐낼 수
	 * 있게 되어 서로 지켜줄 일이 없어지고, 좁히면 자기 앞의 차만 막게 되어 커버가
	 * 성립하지 않는다. 감각을 조절할 때도 반경 확장이 아니라 이 값으로 한다.
	 *
	 * 성립 조건: 같은 방향 차선 간격이 240(= 120 x 2) 이상이어야 한다. 그보다 좁으면 두
	 * 차선 한가운데 선 사람이 양쪽 차량을 동시에 후보로 잡아 "아무나 쳐낸다" 가 된다.
	 * 새 맵을 만들 때 이 값부터 확인한다.
	 *
	 * 실측 (2026-09-01, 에디터에서 ACWTrafficLane 스플라인 좌표를 재서 계산)
	 *   Stage_02  차선 X = -570 / -220 / 90 -> 간격 350, 310. 절반이 175, 155 라 성립한다
	 *   Stage_01  차선이 하나라 해당 없음
	 *   Prototype 같은 방향 간격이 219, 237 이라 절반이 109.5, 118.5 로 조건을 못 채운다.
	 *             이 맵은 도로 머터리얼로 만든 도로가 아니라 차선 폭 기준이 따로 없는
	 *             개발용이므로 배치는 그대로 둔다. 여기서 재는 패링 감각은 스테이지와
	 *             다르다는 것만 알고 쓴다
	 *
	 * 미검증: 위는 좌표와 계산으로 전제가 성립하는지만 본 것이다. 실제 플레이에서 옆 차선
	 * 차량이 후보에서 빠지는지는 확인하지 않았다. 스테이지에서 차선 두 개 사이에 서서
	 * 양쪽 차량에 각각 패링을 시도해보면 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry", meta = (ClampMin = "0.0"))
	float ThreatLateralTolerance = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Parry")
	bool bCanBeParried = true;

	/**
	 * 패링당해 튕겨나간 뒤 사라지기까지의 시간(초). 2초는 임시값이라 팀 합의로 확정한다.
	 * 무한정 날아가면 맵 밖까지 나가고, 뒤차가 볼 앞차 목록에서도 계속 남는다.
	 *
	 * 0 은 "즉시 소멸" 이 아니라 "소멸 없음" 이 되기 쉬운 값이라 하한을 둔다.
	 * 코드에서 0 이 들어와도 소멸 타이머는 반드시 걸린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry", meta = (ClampMin = "0.1"))
	float ParriedDespawnDelay = 2.f;

	/**
	 * 튕겨나갈 때 주행 속도에 곱하는 배율. 1이면 원래 속도 그대로 날아간다.
	 * 튕김 세기는 정해진 값이 없어 플레이로 잡는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry", meta = (ClampMin = "0.0"))
	float ParriedSpeedMultiplier = 1.f;

	/**
	 * 현재 주행 중인 차선.
	 *
	 * 레벨에 배치된 액터라 클라이언트에도 같은 이름으로 존재한다. 그래서 차선 자체를
	 * 복제하지 않아도 이 참조는 해석된다. 클라이언트가 위치를 계산하려면 반드시 필요하다.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "Traffic|Vehicle")
	TObjectPtr<ACWTrafficLane> CurrentLane;

	/**
	 * 차선 시작점에서 진행한 거리(cm). 이 값 하나가 차량의 위치와 회전을 전부 결정한다.
	 * 클라이언트는 업데이트 사이를 자기 Tick 으로 메우고, 값이 도착하면 그 값으로 맞춘다.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "Traffic|Vehicle")
	float DistanceAlongLane = 0.f;

	/** 스포너가 굴린 개체별 속도 배율. 클라이언트도 같은 속도로 추정해야 하므로 복제한다. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "Traffic|Vehicle")
	float SpeedMultiplier = 1.f;

	/** 스포너가 굴린 차선 중심선 대비 좌우 오프셋(cm). 위치 계산에 들어가므로 복제한다. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "Traffic|Vehicle")
	float LaneLateralOffset = 0.f;

	/**
	 * 서버가 고른 MeshVariants 인덱스. 음수면 아직 고르지 않았다는 뜻이다.
	 *
	 * 모든 화면에 같은 차가 보여야 하므로 클라이언트가 각자 굴리면 안 된다.
	 *
	 * 기본값을 0이 아니라 음수로 두는 이유
	 *   초기 복제에서는 CDO 기본값과 같은 값이 아예 전송되지 않고, 전송이 없으면 RepNotify도 없다.
	 *   기본값이 0이면 0번 변형이 뽑힌 차량만 클라이언트에서 메쉬가 바뀌지 않는다.
	 *   어떤 유효 인덱스와도 다른 값을 기본값으로 두면 전송과 알림이 항상 보장된다.
	 *
	 * 같은 액터에 InitializeOnLane 을 다시 부르는 구조가 생기면(지금은 없다) 그 전에 이 값을
	 * INDEX_NONE 으로 되돌려야 한다. 직전과 같은 인덱스를 다시 뽑으면 값이 안 바뀌어 전송이 생략된다.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_MeshVariantIndex, Category = "Traffic|Vehicle")
	int32 MeshVariantIndex = INDEX_NONE;

	/** 패링당해 튕겨나가는 중인 상태. 차선을 벗어나 직진한다. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "Traffic|Parry")
	bool bParried = false;

	/**
	 * 패링당할 때마다 1 올라가는 번호. 클라이언트가 새 발사를 반영하게 만드는 신호다.
	 *
	 * 왜 필요한가
	 *   같은 차량이 두 번 패링당하면 bParried 는 이미 true 라서 값이 바뀌지 않는다.
	 *   서버는 바뀐 프로퍼티만 보내므로 아예 전송되지 않고, 따라서 RepNotify 도 뜨지 않는다.
	 *   (REPNOTIFY_Always 를 붙여도 안 된다. 그건 "전송된 값이 로컬 값과 같을 때도 알림을
	 *    띄운다" 는 뜻이라, 전송 자체가 없는 이 경우와는 무관하다)
	 *
	 *   장소와 방향은 바뀌니까 전송되기는 하는데, 알림이 없으면 클라이언트는 새 값을
	 *   들고만 있고 아무것도 하지 않는다. 회전은 복제되지 않으므로 클라이언트의 차량은
	 *   이전 방향 그대로 계속 날아가고, 서버와 위치가 영구히 갈린다.
	 *   그러면 다른 플레이어 화면에서 차가 엉뚱한 곳으로 가거나 사라진 것처럼 보인다.
	 *
	 *   이 번호는 매번 반드시 달라지므로 전송과 알림이 항상 보장된다.
	 *   uint8 이라 256 번마다 돌지만, 직전 값과 다르다는 성질은 그대로다.
	 *
	 * InitializeOnLane 에서 되돌리지 않는다. 계속 올라가기만 해야 우연히 같은 값이 되지 않는다.
	 */
	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing = OnRep_ParryLaunch, Category = "Traffic|Parry")
	uint8 ParryLaunchSerial = 0;

	// FVector_NetQuantize 계열은 BlueprintType 이 아니라 BlueprintReadOnly 를 붙일 수 없다.
	// 복제용 내부 값이라 BP 에 열 이유도 없다.

	/** 패링당한 순간의 위치. 클라이언트가 같은 지점에서 직진을 시작하게 한다. */
	UPROPERTY(VisibleInstanceOnly, Replicated, Category = "Traffic|Parry")
	FVector_NetQuantize ParriedLaunchLocation = FVector::ZeroVector;

	/** 패링당해 날아가는 방향. 회전이 복제되지 않으므로 클라이언트가 이 값으로 자세를 잡는다. */
	UPROPERTY(VisibleInstanceOnly, Replicated, Category = "Traffic|Parry")
	FVector_NetQuantizeNormal ParriedLaunchDirection = FVector::ZeroVector;

private:
	/** GetVelocity() 가 돌려주는 값. 서버·클라이언트가 각자 자기 Tick 에서 채운다. */
	FVector CurrentVelocity = FVector::ZeroVector;

	/**
	 * 패링당해 발사된 시각(월드 시간, 초). 음수면 아직 발사된 적이 없다는 뜻이다.
	 * 랙 보정을 발사 이전 시간까지 되돌리지 않도록 ResolveEffectiveRewindSeconds 가 본다.
	 *
	 * 복제하지 않는다. 월드 시간은 머신마다 기준이 달라서 값을 그대로 보내면 의미가 없고,
	 * 패링 판정은 서버에서만 돌아서 보낼 이유도 없다. 클라이언트는 보정량 0 으로 들어오므로
	 * (UCWParryComponent::ResolveViewRewindSeconds) 깎을 것 자체가 없다.
	 */
	float ParryLaunchWorldTime = -1.f;

	/** 같은 액터를 중복으로 치지 않도록 이미 판정한 대상을 기록. */
	TSet<TWeakObjectPtr<AActor>> HitActors;

	/**
	 * 지금 몸에 겹쳐 있는 피격 대상. 랙 보정으로 보류됐거나 아직 속도가 하한을 못 넘어
	 * 못 친 대상을 매 틱 다시 보기 위해 들고 있는다. (RetryPendingHits)
	 *
	 * 오버랩 이벤트는 겹치기 시작할 때 한 번뿐이라, 이 집합이 없으면 나중에 조건이
	 * 갖춰진 피격을 다시 판정할 길이 없다. 떨어지면 HandleEndOverlap 이 지운다.
	 */
	TSet<TWeakObjectPtr<AActor>> OverlappingReceivers;

	/**
	 * 지금 정지선 때문에 서 있는지. 전이하는 순간에만 알리기 위한 기억이다.
	 *
	 * 매 틱 알리면 로그가 흘러가서 두 사람이 같은 사건을 짚을 수가 없고, 받는 쪽도
	 * 이미 하고 있는 보정을 매 틱 다시 시작하게 된다.
	 * 앞차에 막혀 선 것은 여기 들어오지 않는다 - 정지선 판정과 섞이면 로그가 거짓말을 한다.
	 *
	 * 복제하지 않는다. 양쪽이 각자 판단해야 두 값의 차이가 어긋남의 증거가 된다.
	 * 서버 값을 받아서 쓰면 클라이언트가 실제로 섰는지를 물을 방법이 없어진다.
	 */
	bool bStoppedAtStopLine = false;

	/**
	 * 서야 하는 차선 위 진행 거리(cm). 보고 있는 정지선이 없으면 -1. 매 틱 ResolveStepSpeed 가 채운다.
	 *
	 * 복제하지 않는다. 정지선 위치도 차체 길이도 이미 양쪽에 다 있어서 계산 결과가 같고,
	 * 값이 갈리는 유일한 경우는 신호색이 아직 도착하지 않은 순간뿐이다.
	 * 그건 이 값을 보내서 될 일이 아니라 신호 쪽에서 풀 문제다.
	 */
	float StopTargetDistance = -1.f;

	FTimerHandle ParriedDespawnTimerHandle;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Traffic|Vehicle")
	FCWOnVehicleStopStateChangedSignature OnStopStateChanged;
};
