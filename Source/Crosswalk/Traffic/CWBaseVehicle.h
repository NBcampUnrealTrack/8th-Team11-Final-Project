// CWBaseVehicle.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CWTrafficVehicleSpawnParams.h"
#include "Data/CWVehicleMeshVariant.h"
#include "Data/Structs/CWParryAttemptContext.h"
#include "CWBaseVehicle.generated.h"

class ACWBaseVehicle;
class ACWTrafficLane;
class UDataTable;
class UBoxComponent;
class UCWVehicleAppearanceComponent;
class UCWVehicleAudioComponent;
class UCWVehicleHitDetectionComponent;
class UPoseableMeshComponent;
class USceneComponent;
class USkeletalMesh;
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
 * 한 프레임의 주행 속도 결정 결과. ACWBaseVehicle::ResolveStepSpeed 가 낸다.
 *
 * 세 값이 한 묶음인 이유: 속도가 0 인 것만으로는 "정지선 앞에 섰다" 와 "앞차에 막혔다" 를
 * 가를 수 없고, 정지선 목표 지점은 속도를 정하는 도중에 같이 나온다. 셋을 따로 돌려주면
 * 부르는 쪽이 매번 초기화하고 짝을 맞춰야 해서, 예전에는 out 포인터에 멤버 주소를 넘기면
 * 안 된다는 경고 주석까지 필요했다.
 *
 * 값으로 돌려주므로 부르는 쪽 멤버는 결과가 다 정해진 뒤에 한 번에 갱신된다.
 */
struct FCWVehicleStepResolution
{
	/** 이번 프레임에 낼 속도(cm/s). 앞차·정지선 중 더 느리게 만든 쪽의 값이다. */
	float Speed = 0.f;

	/**
	 * 이 속도를 정한 것이 정지선인지. 앞차에 막힌 것과 구분하는 데 쓴다.
	 * 정지선이 앞차보다 더 느리게 만들었을 때만 true 다.
	 */
	bool bStopLineLimited = false;

	/**
	 * 서야 하는 차선 위 진행 거리(cm). 보고 있는 정지선이 없으면 -1.
	 * 남은 거리가 아니라 목표 지점이다. 이 차가 어디까지 왔든 같은 값이라 보정의 기준으로 쓸 수 있다.
	 */
	float StopTargetDistance = -1.f;
};

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
 *   서버와 같으므로 보정량이 거의 0 이다. 앞차가 없는 자유 주행은 [VehicleSync] 실측에서
 *   오차 ±10~20cm 로, 계산으로 잡았던 37cm(750cm/s x 50ms)보다 오히려 작았다.
 *
 *   어긋나는 것은 앞차를 따라가는 구간인데, 크기가 "조금" 이 아니다. 실측 150~250cm 까지
 *   벌어졌다가 서버 값이 도착할 때 당겨지기를 반복한다. 지연을 걸지 않은 상태에서도 같은
 *   크기로 나오므로 회선이 아니라 구조 문제다.
 *
 *   ResolveStepSpeed 가 보는 앞차 위치(VehicleAhead->GetDistanceAlongLane)도 20Hz 로만
 *   오는 값이다. 클라이언트는 조금 과거의 앞차를 기준으로 속도를 정하고 서버는 지금 앞차를
 *   기준으로 정한다. 앞차 추종 속도에서 프레임 시간 의존을 뺀 것(#74)은 계산식을 고친 것이지
 *   입력을 신선하게 만든 것이 아니라, 이 갈래는 그대로 남아 있다.
 *   감속 밴드가 범퍼 사이 300cm 에서 시작해 150cm 에서 정지라 폭이 150cm 인데, 오차가 그
 *   폭과 같은 규모다. 양쪽이 속도 곡선의 다른 지점에 서게 되므로 크기가 이만큼인 것으로
 *   보인다 - 여기까지는 미검증이다.
 *
 *   덤으로 대역폭도 줄어든다. 위치+회전 대신 float 하나이고, 업데이트 빈도도 낮출 수 있다.
 *
 *   패링당해 차선을 벗어난 구간은 진행 거리로 표현할 수 없어서, 튕겨나간 지점과 방향을
 *   따로 복제하고 클라이언트가 그 지점에서 직진을 시작한다. (bParried / ParriedLaunch*)
 *
 * 이 클래스에 남아 있는 것과 컴포넌트로 나간 것
 *
 *   여기            주행(차선 추종·앞차·정지선), 서버-클라 동기화, 패링 판정, 차종 데이터
 *   Appearance      차체·색 변형 뽑기와 복제, 메쉬 씌우기, 바퀴 회전
 *   HitDetection    플레이어 피격 판정(오버랩·재판정·랙 보정)
 *   Audio           엔진음·경적·도플러
 *
 *   차종 데이터(MeshVariants, WheelBones, VehicleType 등)는 컴포넌트가 아니라 여기 있다.
 *   차종 BP 가 입력하는 값이라 프로퍼티를 옮기면 그 입력이 전부 날아간다. 컴포넌트는
 *   게터로 읽기만 한다. 두 컴포넌트는 틱이 없고 이 클래스의 Tick 이 맨 앞에서 부른다 -
 *   "지난 프레임 값으로 본다" 는 전제를 액터 틱 순서 설정에 맡기지 않기 위해서다.
 */
UCLASS()
class CROSSWALK_API ACWBaseVehicle : public AActor
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
	 *
	 * UFUNCTION 이 아니다. 예전에는 Interface_ParryReceiver 의 BlueprintNativeEvent 였는데,
	 * 구현체가 이 클래스 하나이고 호출자가 서브시스템 하나라 인터페이스가 하는 일이 없었다.
	 * 게다가 BlueprintCallable 이어서 BP 에서 선별을 건너뛰고 직접 부를 수 있는 구멍이 있었다.
	 * 들어오는 길을 C++ 하나로 두어야 패링이 왜 됐는지를 한 경로에서만 따지면 된다.
	 */
	bool ReceiveParryAttempt(const FCWParryAttemptContext& ParryAttemptContext);

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

	/**
	 * 바퀴가 지금까지 돈 각도(도). 외형 컴포넌트의 값을 그대로 돌려주는 BP 호환용 창구다.
	 * 본에 꽂는 일은 C++ 이 하므로 BP 쪽 참고용으로만 남아 있다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Vehicle")
	float GetWheelSpinDegrees() const;

	/** 콜리전 박스 기준 차량 전체 길이(cm). 차간 거리 계산의 기준값. */
	UFUNCTION(BlueprintPure, Category = "Traffic|Vehicle")
	float GetVehicleLength() const;

	/**
	 * 액터 원점에서 콜리전 박스 앞면까지의 거리(cm). 스케일 적용 후 값이고 차 길이의 절반이다.
	 *
	 * 패링 예측·피격 랙 보정·차간 거리가 전부 이 값을 기준으로 잰다. 세 곳이 각자
	 * 박스에서 읽으면 어느 한 곳만 스케일 전 값을 쓰는 실수가 생긴다.
	 */
	float GetVehicleHalfLength() const;

	/**
	 * 콜리전 박스 크기를 바꾸고, 바뀐 높이에 맞춰 박스를 다시 노면 위로 올린다.
	 *
	 * 크기와 위치를 따로 부르면 안 된다. 박스 중심은 항상 높이의 절반 지점이라
	 * 한쪽만 바꾸면 그 차종만 노면을 뚫거나 공중에 뜬 채로 다닌다.
	 *
	 * Extent 는 스케일이 붙기 전 값이다. 액터 스케일은 익스텐트와 상대 위치에 똑같이
	 * 곱해지므로, 차량 BP 가 크기를 키워도 바닥은 계속 노면에 붙어 있다.
	 *
	 * 외형 컴포넌트가 변형의 크기를 넘겨 부른다. 판정 몸체를 만지는 유일한 자리를
	 * 차량 안에 두려고 함수는 여기 있고 값만 밖에서 온다.
	 */
	void ApplyCollisionExtent(const FVector& Extent);

	/**
	 * 외형·피격 컴포넌트가 읽는 차종 데이터와 부품. C++ 전용이다.
	 * 데이터의 소유자는 차량 BP 이고 컴포넌트는 읽기만 한다. (각 컴포넌트 헤더 주석 참고)
	 */
	const TArray<FCWVehicleMeshVariant>& GetMeshVariants() const { return MeshVariants; }
	float GetDefaultWheelRadius() const { return WheelRadius; }
	const TArray<FName>& GetDefaultWheelBones() const { return WheelBones; }
	float GetMinHitSpeed() const { return MinHitSpeed; }
	bool ShouldFlattenOnHit() const { return bFlattenOnHit; }
	UBoxComponent* GetCollisionBox() const { return CollisionBox; }
	UStaticMeshComponent* GetVehicleMesh() const { return VehicleMesh; }
	UPoseableMeshComponent* GetVehicleSkeletalMesh() const { return VehicleSkeletalMesh; }

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
	 * 패링당하는 순간 서버·클라이언트 모두에서 호출된다. 차량 BP 에서 구현해 **차에 붙는 연출**을 붙이는 자리다.
	 *
	 * 패링 성공 연출은 두 갈래로 나뉜다. 어디에 붙일지 헷갈리면 "누구 몸에 붙는가" 로 가른다.
	 *   차에 붙는 것   - 여기. 차체 스파크, 타이어 트레일, 차 사운드처럼 차량 액터를 따라가는 연출.
	 *   사람에 붙는 것 - UCWParryComponent::OnParrySucceeded. 카메라 흔들림, 히트스톱, HUD,
	 *                    쳐낸 사람 애니메이션처럼 플레이어를 따라가는 연출.
	 * 둘은 같은 사건에 각자 나가는 별개 RPC 다. 한쪽에 다른 쪽 연출을 붙이면 소유 액터가 달라서
	 * 매번 상대를 찾아야 하고, 그 코드가 두 파일에 흩어진다.
	 *
	 * 호출 시점은 차량이 이미 새 방향으로 회전한 뒤다. 그래서 GetActorForwardVector 가
	 * 곧 날아갈 방향이고, LaunchDirection 도 같은 값이다.
	 *
	 * @param LaunchDirection  차량이 날아가는 방향(정규화, 수평).
	 * @param LaunchSpeed      그 순간의 실제 주행 속도(cm/s). 연출 세기 환산에 쓴다.
	 * @param Parrier          쳐낸 플레이어. 차 쪽 연출을 그 사람에 따라 다르게 낼 때 쓴다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Traffic|Parry")
	void OnParriedFX(const FVector& LaunchDirection, float LaunchSpeed, AActor* Parrier);

protected:
	/** 콜리전 박스를 바닥 기준으로 정렬한다. 편집 화면과 런타임이 같은 모습이어야 하기 때문이다. */
	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 차선 끝에 도달했거나 패링 소멸 타이머가 끝났을 때. 지금은 Destroy 가 전부다. */
	virtual void OnReachedLaneEnd();

private:
	/**
	 * 앞차와 정지선을 보고 이번 프레임의 주행 속도를 정한다. 둘 다 없으면 제 속도를 그대로 낸다.
	 *
	 * 프레임 시간을 받지 않는다. 앞차와의 거리만 보고 정해야 프레임 간격이 흔들려도 속도가
	 * 같이 흔들리지 않고, 서버와 클라이언트가 같은 상황에서 같은 값을 낸다.
	 *
	 * 멤버를 건드리지 않는 const 함수다. 결과를 멤버에 반영하는 것은 부르는 쪽(Tick)이 한다.
	 * 각 항목의 뜻은 FCWVehicleStepResolution 참고.
	 */
	FCWVehicleStepResolution ResolveStepSpeed() const;

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

	/**
	 * 뽑힌 차체의 차종 이름으로 속도 범위표를 찾아 SpeedMultiplier 를 굴린다.
	 *
	 * 서버에서만 부른다. 결과는 복제되는 SpeedMultiplier 로 건너가므로 클라이언트는
	 * 굴리지 않는다 - 양쪽이 각자 굴리면 같은 차가 화면마다 다른 속도로 달린다.
	 * 이름이 비었거나 표에 없으면 차종 BP 의 Speed 를 그대로 쓴다.
	 */
	void RollSpeedForVariant(FName SpeedType, const UDataTable* SpeedTable);

	/**
	 * 서버의 진행 거리가 도착했을 때. **받은 값을 화면에 바로 쓰지 않는다.**
	 *
	 * 그냥 두면 리플리케이션이 DistanceAlongLane 을 통째로 덮어쓴다. 주행 중에는 그
	 * 순간이동이 움직임에 묻히지만, 정지선 앞에서는 배경이 정지해 있어서 같은 크기의
	 * 오차가 그대로 앞뒤 떨림으로 보인다(20Hz 니까 50ms 마다 한 번씩).
	 *
	 * 그래서 로컬 추정값을 도로 돌려놓고, 서버와의 차이는 DistanceCorrectionRemaining 에
	 * 빚으로 적어 둔다. 갚는 것은 Tick 이 프레임마다 조금씩 한다.
	 *
	 * 차이가 ClientSnapThreshold 를 넘으면 빚으로 들지 않고 받은 값을 그대로 쓴다.
	 * 스폰 직후 첫 수신이나 차선 끝 근처처럼 애초에 이어붙일 수 없는 거리는 끌고 가 봐야
	 * 차가 길게 미끄러질 뿐이다.
	 *
	 * **RepNotify 의 "이전 값" 파라미터를 쓰면 안 된다.** 엔진이 넘겨주는 그 값은
	 * 섀도 버퍼에서 나오는데(RepLayout.cpp, CallRepNotifies 의 NumParms == 1 분기),
	 * 섀도 버퍼에 들어 있는 것은 **마지막으로 받은 서버 값**이지 클라이언트가 자기 Tick 으로
	 * 계산해 온 값이 아니다. 그걸 로컬 값으로 착각해 대입하면 차가 매 수신마다 한 업데이트
	 * 구간(50ms x 주행 속도)만큼 뒤로 되감기고, 되감긴 만큼 감속식이 속도를 다시 올려서
	 * 정지선을 넘나드는 큰 왕복이 된다. 게다가 notify 직후 엔진이 오브젝트 값을 섀도로
	 * 복사해 가므로 되감은 값이 섀도에까지 남아 오차가 누적된다.
	 *
	 * 그래서 로컬 값은 LocalDistanceAlongLane 에 우리가 직접 들고 있는다.
	 *
	 * 서버에서는 불리지 않는다. 판정에 쓰는 값은 서버 자신의 DistanceAlongLane 그대로다.
	 */
	UFUNCTION()
	void OnRep_DistanceAlongLane();

	/** 적어 둔 빚을 이번 프레임 몫만큼 갚는다. 클라이언트 전용. (OnRep_DistanceAlongLane 참고) */
	void ApplyServerDistanceCorrection(float DeltaSeconds);

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

	/**
	 * 패링당했을 때 날아갈 방향을 정한다. 기준은 **커서 지점**이다. 항상 유효한 방향을 돌려준다.
	 *
	 * 폴백 사슬은 여기 한 곳에만 있다: 커서 지점(목적지) -> 플레이어 기준 방향 -> 온 길 되돌림.
	 * 1순위가 왜 두 번 뒤집혔는지는 .cpp 의 1·2순위 주석에 실측과 함께 적어 뒀다.
	 *
	 * 어느 것이 뽑히든 마지막에 ApplySelfLaunchDeadZone 을 한 번 거쳐 시전자 쪽이 걸러진다.
	 * TryParry 는 이 결과를 그대로 믿는다 - 폴백이 두 파일에 나뉘어 있으면 어느 쪽이 실제로
	 * 방향을 정했는지 로그만 보고는 알 수 없다.
	 *
	 * @param OutBaseDirection 데드존을 거치기 전, 폴백 사슬이 고른 방향. 진단 로그 전용이다.
	 *                         반환값에서는 역산할 수 없다 - 데드존이 민 결과와 애초에 다른
	 *                         순위가 뽑힌 결과가 똑같이 "기준과 다른 발사각" 으로 보인다.
	 */
	FVector ResolveLaunchDirection(const FCWParryAttemptContext& ParryAttemptContext, FVector& OutBaseDirection) const;

	/**
	 * 이 커서 좌표를 발사 목적지로 믿어도 되는지. (MinCursorLaunchDistance)
	 *
	 * 커서가 차체에 가까울수록 (커서 - 차량)이 짧아져 방향이 튀고, 그 구간의 커서 좌표는
	 * 애초에 트레이스가 차체를 뚫고 찍은 값이라 화면에서 본 자리도 아니다. 판정 자체를
	 * 무르지는 않는다 - 1순위만 건너뛰고 플레이어 기준 방향으로 넘어간다.
	 */
	bool IsCursorUsableAsLaunchTarget(const FVector& CursorWorldLocation) const;

	/**
	 * 시전자 쪽으로 향한 발사 방향을 데드존 경계로 밀어낸다. (SelfLaunchDeadZoneAngle)
	 *
	 * 패링에 성공했는데 그 차에 그대로 치이는 것을 막는다.
	 *
	 * 막는 것은 "막으려고 차 쪽을 가리켰을 뿐인데 차가 되돌아온다" 다. 발사 기준이 커서
	 * 지점이라 커서가 차량과 시전자 사이에 떨어지면 (커서 - 차량위치)가 곧
	 * (시전자 - 차량위치)가 되고, 그대로 두면 패링에 성공한 사람이 그 차에 치인다.
	 *
	 * 2026-09-18 ~ 09-21 사이에는 발사 기준이 플레이어 방향이라 이 경로가 아예 막혀 있었고,
	 * 그때 로그에서 이 함수는 한 번도 발사 방향을 밀지 않았다(4건 전부 0도). 커서 지점
	 * 기준으로 되돌리면서 다시 일하게 된 자리라, **09-18 실측은 이 값의 근거가 되지 못한다.**
	 *
	 * 목적지 기준에서 남는 또 한 경우는 시전자가 자기 뒤쪽을 가리켰을 때다. 가리킨 대로
	 * 간 것이라 막을 이유가 없다는 판단도 가능하고, 그러면 SelfLaunchDeadZoneAngle 을
	 * 0 으로 두면 된다. 다만 위의 되돌아오는 경우가 같이 열리므로 둘을 함께 봐야 한다.
	 *
	 * 방향을 버리지 않고 경계로 밀기만 한다. 좌우 중 원래 가리킨 쪽으로 밀어야
	 * "왼쪽을 가리켰는데 오른쪽으로 날아가는" 일이 생기지 않는다.
	 */
	FVector ApplySelfLaunchDeadZone(const FVector& LaunchDirection, const FCWParryAttemptContext& ParryAttemptContext) const;

	/**
	 * 패링 판정을 확정하고 차량을 LaunchDirection 으로 튕겨낸다. 성공하면 true.
	 *
	 * 들어오는 길은 ReceiveParryAttempt 하나뿐이다. 그 앞에 서브시스템의 선별(차종·반경·각도)이
	 * 있으므로 여기서는 윈도우만 다시 본다. 블루프린트에 열지 않는다 - 선별을 건너뛰고
	 * 직접 부르는 길이 하나 더 있으면, 패링이 왜 됐는지 안 됐는지를 두 경로에서 따져야 한다.
	 *
	 * @param LaunchDirection   날아갈 방향(수평, 정규화). ResolveLaunchDirection 이 낸 값이라 비어 있지 않다.
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
	 * 차에 붙는 패링 연출을 모든 머신에서 재생한다. 연출 전용이고 게임플레이 상태는 건드리지 않는다.
	 *
	 * 사람에 붙는 연출은 UCWParryComponent::Multicast_ParrySucceeded 가 따로 나간다.
	 * 같은 사건에 RPC 가 둘인 것은 의도다 - 역할 구분은 OnParriedFX 주석 참고.
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

protected:
	/**
	 * 액터 원점을 노면에 붙잡아 두는 빈 루트.
	 *
	 * 콜리전 박스를 루트로 두면 박스 중심이 곧 액터 원점이 된다. 그런데 액터 위치는
	 * 차선 스플라인 위의 점(노면)을 그대로 덮어쓰므로(UpdateTransformOnLane),
	 * 박스가 노면을 중심으로 걸려 아래 절반은 도로에 박히고 위 절반만 차를 덮었다.
	 * 실제 차 높이의 절반에만 판정이 있는 상태였다.
	 *
	 * 루트는 자기 자신에게 상대 위치를 줄 수 없어서, 트랜스폼만 가진 이 컴포넌트를
	 * 원점 자리에 세우고 박스를 자식으로 내려 위로 올렸다. 액터 원점은 계속 노면이라
	 * GetActorLocation 을 기준으로 재는 코드(피격·패링·앞차 간격)는 그대로 성립한다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Vehicle")
	TObjectPtr<USceneComponent> VehicleRoot;

	/**
	 * 플레이어 충돌 판정용. 플레이스홀더 단계에서는 이 박스가 곧 차량 크기다.
	 * 바닥이 노면에 닿도록 높이의 절반만큼 올라가 있다. (ApplyCollisionExtent)
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Vehicle")
	TObjectPtr<UBoxComponent> CollisionBox;

	/** 플레이스홀더 메쉬(직육면체 Box). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Vehicle")
	TObjectPtr<UStaticMeshComponent> VehicleMesh;

	/**
	 * 바퀴가 도는 차량용 메쉬. VehicleMesh 와 나란히 서고, 변형이 지정한 쪽만 채워진다.
	 *
	 * 정적 메쉬를 갈아 끼우는 대신 컴포넌트를 하나 더 둔 이유는, 기존 차종 BP 들이
	 * VehicleMesh 에 직접 메쉬를 물려 둔 상태이기 때문이다. 타입을 바꿔 버리면 그 BP 들이
	 * 한꺼번에 빈 차가 되고, 되돌리려면 BP 를 하나씩 다시 열어야 한다.
	 *
	 * 메쉬가 안 물린 컴포넌트는 아무것도 그리지 않으므로 쓰지 않는 쪽의 비용은 없다.
	 *
	 * 스켈레탈 메쉬 컴포넌트가 아니라 포저블인 이유
	 *   MegaCity 팩은 메쉬 94종이 각자 자기 스켈레톤을 들고 있다. 애님 블루프린트는
	 *   스켈레톤 하나에 묶이므로, 그 방식으로 가면 메쉬를 갈아 끼울 때마다 애님이 떨어져
	 *   바퀴가 멈춘다. 피하려면 94종을 한 스켈레톤으로 재임포트하거나(임포트 때 잡아 둔
	 *   전방축 보정이 날아간다) 94개 에셋에 호환 스켈레톤을 등록해야 한다.
	 *
	 *   포저블 메쉬는 애님 인스턴스도 스켈레톤 바인딩도 없이 본을 직접 돌린다. 본 이름만
	 *   맞으면 되므로 에셋을 하나도 건드리지 않고 94종이 전부 돈다. 이 차량들이 가진
	 *   애니메이션은 바퀴 회전뿐이라 애님 그래프로 얻을 것이 애초에 없었다.
	 *
	 * 이 슬롯은 BP 에서 비워 둔다. 스폰할 때 외형 컴포넌트(ApplyMeshVariant)가 채운다.
	 *   디테일 창으로 메쉬를 직접 물리면 BP 뷰포트에서 지오메트리가 뭉개져 보인다.
	 *   UPoseableMeshComponent 는 OnRegister 도 PostEditChangeProperty 도 오버라이드하지 않고
	 *   bTickInEditor 도 켜지 않아서, 에디터에서는 본 트랜스폼을 채우는 AllocateTransformData 가
	 *   한 번도 불리지 않는다. 런타임에는 첫 틱의 RefreshBoneTransforms 가 알아서 채우므로
	 *   게임 화면에서는 멀쩡하다. 에셋이 깨진 것이 아니다 - 메쉬를 눈으로 보려면
	 *   스켈레탈 메쉬 에디터에서 열어라. (2026-09-15, Police_01 을 이것 때문에 의심했다)
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Vehicle")
	TObjectPtr<UPoseableMeshComponent> VehicleSkeletalMesh;

	// 정지선 앞 서버-클라 오차를 좁히던 StopSyncComponent 는 없앴다. Tick 의 정지 확정 분기가
	// 양쪽에서 같은 목표 지점에 못 박으면서(StopSettleTolerance) 그 컴포넌트가 잴 오차가 항상 0 이 됐다.

	/**
	 * 엔진음·경적·도플러 담당. 소리는 연출이라 복제하지 않고 각자 화면에서 운다.
	 *
	 * 애셋은 여기에 지정하지 않는다. 차종마다 다른 소리를 쓰고, 어떤 차가 어떤 소리를
	 * 내는지는 C++ 기본값보다 차량 BP 에서 보는 편이 빠르다. (컴포넌트 주석 참고)
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Vehicle")
	TObjectPtr<UCWVehicleAudioComponent> AudioComponent;

	/**
	 * 차체·색 변형과 바퀴 회전 담당. 뽑은 변형(VariantPick)은 이 컴포넌트가 복제한다.
	 * 변형 목록 자체는 아래 MeshVariants 에 있고 컴포넌트는 읽기만 한다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Vehicle")
	TObjectPtr<UCWVehicleAppearanceComponent> AppearanceComponent;

	/**
	 * 플레이어 피격 판정 담당. 서버 전용이고 콜리전 박스의 오버랩을 받아 처리한다.
	 * 패링 직후 쳐낸 사람을 이번 겹침 동안 제외하는 것만 TryParry 가 이쪽에 부탁한다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Vehicle")
	TObjectPtr<UCWVehicleHitDetectionComponent> HitDetectionComponent;

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

	/**
	 * 아래 튜닝값은 전부 EditDefaultsOnly + BlueprintReadOnly 다. 런타임에 쓸 수 없다.
	 *
	 * 이 값들은 복제되지 않는다. 서버와 클라이언트가 각자 같은 값으로 감속·정지·패링을
	 * 계산한다는 전제로 돌아가므로, BP 가 한쪽에서만 바꾸면 정지 위치나 패링 윈도우가
	 * 갈리는데 그걸 잡는 로그가 없다. 차종 BP 의 기본값으로만 정한다.
	 */

	/** 차종 기본 주행 속도(cm/s). 플레이어 기본 이동속도 500의 1.5배. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0"))
	float Speed = 750.f;

	// 개체별 속도 편차는 여기 없다. 차종별 범위는 데이터 테이블(FCWVehicleSpeedRow)이 들고
	// 있고 스포너가 스폰할 때 뽑는다. 같은 값을 두 곳에서 정하면 어느 쪽이 이겼는지
	// 추적이 안 되므로, Speed 는 "범위를 안 정했을 때 쓰는 기본값" 하나로만 남긴다.

	/**
	 * 차종 기본 바퀴 반지름(cm). 변형이 WheelRadius 를 지정하지 않았을 때 쓰인다.
	 *
	 * 기본값 34 는 승용차 어림치이고 실측이 아니다. 차종마다 메쉬 에디터에서 바퀴
	 * 지름을 재서 절반을 넣는 것이 맞다. 틀려도 주행에는 영향이 없고 바퀴가 미끄러지거나
	 * 끌려 보일 뿐이다.
	 *
	 * 0 이면 바퀴를 돌리지 않는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0"))
	float WheelRadius = 34.f;

	/**
	 * 차종 기본 바퀴 본 이름. 변형이 WheelBones 를 지정하지 않았을 때 쓰인다.
	 *
	 * 양쪽 다 비어 있으면 바퀴가 돌지 않는다.
	 *
	 * MegaCity 팩은 26 차체 중 24개가 FL/FR/RL/RR 로 같아서, 이 값을 부모 BP 에 한 번만
	 * 넣으면 대부분이 덮인다. 나머지 둘(3축 세미, 오토바이)은 본 구성 자체가 달라
	 * 변형 쪽에서 따로 지정한다. 자세한 이름은 FCWVehicleMeshVariant::WheelBones 주석 참고.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Vehicle")
	TArray<FName> WheelBones;

	/**
	 * 피격 연출 분기에 쓰이는 차종 ID. FCWVehicleHitContext로 전달된다.
	 * 차종 BP마다 다른 값을 지정해야 피격 프로파일이 제대로 잡힌다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Vehicle")
	FName VehicleType = TEXT("Sedan");

	/** 대형 트럭처럼 납작해지는 특수 피격을 쓸지. 차종 BP에서 켠다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Vehicle")
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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0"))
	float MinHitSpeed = 100.f;

	// 피격 랙 보정의 되돌림 양과 상한은 여기 없다. 패링과 같은 값이어야 해서
	// UCWTrafficSubsystem::ResolveViewRewindSeconds 하나가 두 판정에 답한다.

	/** 앞차 뒤범퍼와 유지할 최소 여유 거리(cm). 차 길이에 더해서 쓴다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0"))
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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0"))
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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0"))
	float StopLineBrakeDistance = 900.f;

	/**
	 * 정지선 앞 마지막 구간에서 유지하는 최저 속도(cm/s). 이 값이 곧 "끝이 있는 정지" 다.
	 *
	 * ResolveApproachSpeed 의 감속은 남은 거리에 비례하는 지수형이라 **유한 시간에 0 이
	 * 되지 않는다.** 기본값 기준으로 남은 거리가 눈에 안 보일 만큼 줄어드는 데만 십수 초가
	 * 걸려서, 신호 한 사이클 내내 차가 미세하게 굴러가는 상태로 남는다. 값이 매 프레임
	 * 바뀌니 리플리케이션도 멈추지 않는다.
	 *
	 * 그래서 속도가 이 값 아래로 내려가면 더 줄이지 않고 그대로 굴려서 목표 지점까지
	 * 보낸다. 바닥을 까는 것이라 제동 곡선 자체는 그대로다 - 이 값이 걸리기 시작하는
	 * 지점은 StopLineBrakeDistance x (이 값 / 주행 속도), 기본값으로 정지 30cm 전이다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0"))
	float StopFinalCrawlSpeed = 25.f;

	/**
	 * 목표 지점까지 이 거리(cm) 안으로 들어오면 정지로 확정하고 목표 지점에 고정한다.
	 *
	 * 고정하는 값이 서버와 클라이언트에서 같다는 것이 핵심이다. 목표 지점은 양쪽이
	 * 정지선 위치와 차체 길이로 각자 계산하는 값이라 통신 없이도 일치한다. 그래서 두
	 * 기계가 같은 자리에 못 박히고, 값이 더 안 변하니 리플리케이션도 조용해진다.
	 *
	 * 이 값이 0 이면 확정이 영영 안 걸린다. StopFinalCrawlSpeed 주석 참고.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0"))
	float StopSettleTolerance = 5.f;

	/**
	 * 서버와의 차이를 좁히는 속도(cm/s). 클라이언트에서만 쓴다.
	 *
	 * 주행 속도보다 한참 느려야 한다. 이 값이 크면 좁히는 동작 자체가 순간이동처럼 보여서
	 * 덮어쓰기를 없앤 의미가 사라진다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|VehicleSync", meta = (ClampMin = "0.0"))
	float ClientCorrectionSpeed = 200.f;

	/** 서버와의 차이가 이 값(cm)을 넘으면 좁히지 않고 그대로 스냅한다. (OnRep_DistanceAlongLane 참고) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|VehicleSync", meta = (ClampMin = "0.0"))
	float ClientSnapThreshold = 300.f;

	/** 패링 판정 윈도우(초). 충돌 직전 이 시간 안에 입력해야 성공. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Parry", meta = (ClampMin = "0.0"))
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
	 * 새 맵을 만들 때 이 값부터 확인한다. 맵별 차선 간격 실측은 노션 검증 문서에 있다 -
	 * 좌표는 맵을 고치면 바뀌는 값이라 여기 적어 두면 거짓말이 된다.
	 *
	 * 확인 방법: 스테이지에서 같은 방향 차선 두 개 사이에 서서 양쪽 차량에 각각 패링을
	 * 시도한다. 옆 차선 차가 후보에서 빠져야 맞다. (좌표 계산만 했고 플레이 확인은 아직이다)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Parry", meta = (ClampMin = "0.0"))
	float ThreatLateralTolerance = 120.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Parry")
	bool bCanBeParried = true;

	/**
	 * 패링당해 튕겨나간 뒤 사라지기까지의 시간(초). 2초는 임시값이라 팀 합의로 확정한다.
	 * 무한정 날아가면 맵 밖까지 나가고, 뒤차가 볼 앞차 목록에서도 계속 남는다.
	 *
	 * 0 은 "즉시 소멸" 이 아니라 "소멸 없음" 이 되기 쉬운 값이라 하한을 둔다.
	 * 코드에서 0 이 들어와도 소멸 타이머는 반드시 걸린다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Parry", meta = (ClampMin = "0.1"))
	float ParriedDespawnDelay = 2.f;

	/**
	 * 튕겨나갈 때 주행 속도에 곱하는 배율. 1이면 원래 속도 그대로 날아간다.
	 * 튕김 세기는 정해진 값이 없어 플레이로 잡는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Parry", meta = (ClampMin = "0.0"))
	float ParriedSpeedMultiplier = 1.f;

	/**
	 * 시전자 쪽을 금지 구역으로 두는 부채꼴 전체 각(도). 0 이면 제한 없음.
	 *
	 * 차량에서 본 시전자 방향을 중심으로 이 각 안에 들어온 발사 방향은 경계로 밀려난다.
	 * ThreatLateralTolerance 처럼 좌우 각각이 아니라 부채꼴 전체 각이다. 각도 판정을
	 * 쓰는 다른 값(FCWParryAttemptContext::ParryAngle)과 같은 뜻으로 맞췄다.
	 *
	 * 60 은 임의값이다. 산출은 이렇다 - 차량은 방향이 꺾인 뒤 직진하므로 시전자와의 최소
	 * 거리가 (패링 시점 거리 x sin(반각))으로 정해진다. 윈도우 0.3초에 750cm/s 면 거리가
	 * 225cm 이고, 반각 30도면 225 x sin(30) = 112cm 가 남는다. 캐릭터 캡슐 반경이 40cm
	 * 안팎이니 차체 반폭 70cm 까지는 스치지 않는다는 계산이다.
	 *
	 * 그런데 차체 반폭은 차종마다 다르고(CollisionBox 를 메시에서 잡는다) 속도도 개체별
	 * 배율이 붙으므로, 이 계산은 기준선일 뿐 보장이 아니다. 고정 각이라 거리가 계산 전제와
	 * 다를 때 구멍이 남는다 - 각을 거리로 나누는 방식(sin(반각) = 막을 폭 / 거리)으로
	 * 바꾸면 그 구멍이 사라지지만, 값이 자동으로 정해져 튜닝 손잡이가 없어진다.
	 * 지금은 손으로 잡아 보고 구멍이 실제로 보이면 그때 옮긴다.
	 *
	 * 좁히면 자기 쪽으로 되돌아오는 사고가 남고, 넓히면 "내 앞을 지나가게 날리는" 조준이
	 * 통째로 막힌다. 후자가 커버 패링 직후의 흔한 조작이라 넓히는 쪽이 더 위험하다.
	 *
	 * 구간마다 성질이 다르다. 180 은 반각 90, 곧 시전자 쪽 반구 전체를 막아 차가 시전자에게
	 * 한 발짝도 다가가지 못하는 상태다. 그 위로는 조준 가능 범위가 시전자 뒤쪽으로 좁혀지고,
	 * 360 에서는 반각이 180 이라 정확히 반대 방향 하나만 데드존 밖에 남아 모든 발사가
	 * 시전자 정반대로 모인다. 커서가 결과에 영향을 주지 못하므로 그 값은 금지 구역이 아니라
	 * 방향 강제로 읽어야 한다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Parry", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float SelfLaunchDeadZoneAngle = 60.f;

	/**
	 * 커서를 발사 목적지로 쓰기 위한 최소 거리(cm). 차체 표면에서 수평으로 잰다.
	 * 이보다 가까우면 커서 지점 대신 플레이어 기준 방향으로 날린다. 0 이면 항상 커서 지점을 쓴다.
	 *
	 * 300 은 임의값이다. 근거로 쓸 수 있는 실측이 2026-09-18 네 건뿐이라 그 사이를 잡았다 -
	 * 커서-차량 86cm 한 건이 123도 어긋났고, 164 / 415 / 469cm 세 건은 정상이었다.
	 * 원점 기준으로 잰 값이라 표면 기준인 이 값과 직접 비교되지는 않는다(차체 반길이만큼 작다).
	 *
	 * 거리로 자르는 것은 증상을 피하는 것이지 원인을 고치는 것이 아니다. 원인은 커서 트레이스가
	 * 차체를 뚫는 것이고(ACWPlayerCharacter::ResolveCursorWorldLocation), 커서 전용 트레이스
	 * 채널을 파거나 CollisionBox 를 블로킹으로 바꾸면 사라진다. 후자는 피격·스포너 간격이
	 * 같은 박스를 보므로 파급이 크다. 고치면 이 값은 0 으로 두면 된다.
	 *
	 * 올리면 차를 조준했을 때 플레이어 방향으로 넘어가는 구간이 넓어지고, 내리면 커서를
	 * 차 가까이 찍어도 그대로 목적지가 되지만 방향이 튀는 구간에 들어간다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traffic|Parry", meta = (ClampMin = "0.0"))
	float MinCursorLaunchDistance = 300.f;

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
	 * 클라이언트는 업데이트 사이를 자기 Tick 으로 메우고, 값이 도착하면 그쪽으로 좁혀 간다.
	 * (덮어쓰지 않는 이유는 OnRep_DistanceAlongLane 주석 참고)
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_DistanceAlongLane, Category = "Traffic|Vehicle")
	float DistanceAlongLane = 0.f;

	/**
	 * 아직 갚지 않은 서버와의 차이(cm). 양수면 서버가 앞서 있다는 뜻이다.
	 * 클라이언트에서만 0 이 아니다. (OnRep_DistanceAlongLane 참고)
	 */
	float DistanceCorrectionRemaining = 0.f;

	/**
	 * 이 기계가 자기 Tick 으로 계산해 온 진행 거리(cm). 복제하지 않는다.
	 *
	 * DistanceAlongLane 은 서버 값이 도착하는 순간 엔진이 덮어쓰기 때문에, 덮이기 전 값이
	 * 필요한 OnRep_DistanceAlongLane 이 볼 곳이 따로 있어야 한다. RepNotify 의 이전 값
	 * 파라미터로는 대신할 수 없다 - 이유는 그 함수 주석에 적어 뒀다.
	 *
	 * Tick 이 DistanceAlongLane 을 확정한 직후마다 같이 갱신한다. 서버에서도 갱신되지만
	 * 서버에서는 OnRep 이 불리지 않아 아무도 읽지 않는다.
	 */
	float LocalDistanceAlongLane = 0.f;

	/** 스포너가 굴린 개체별 속도 배율. 클라이언트도 같은 속도로 추정해야 하므로 복제한다. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "Traffic|Vehicle")
	float SpeedMultiplier = 1.f;

	/** 스포너가 굴린 차선 중심선 대비 좌우 오프셋(cm). 위치 계산에 들어가므로 복제한다. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "Traffic|Vehicle")
	float LaneLateralOffset = 0.f;

	// 서버가 고른 차체와 색(VariantPick)은 외형 컴포넌트가 들고 복제한다.

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
	 * (UCWTrafficSubsystem::ResolveViewRewindSeconds) 깎을 것 자체가 없다.
	 */
	float ParryLaunchWorldTime = -1.f;

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
	 * 서야 하는 차선 위 진행 거리(cm). 보고 있는 정지선이 없으면 -1. 매 틱 Tick 이 ResolveStepSpeed 의 결과로 채운다.
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
