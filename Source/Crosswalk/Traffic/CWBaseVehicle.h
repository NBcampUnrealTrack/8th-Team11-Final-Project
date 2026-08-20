// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CWTrafficVehicleSpawnParams.h"
#include "Interfaces/Interface_ParryReceiver.h"
#include "CWBaseVehicle.generated.h"

class ACWBaseVehicle;
class ACWTrafficLane;
class UBoxComponent;
class UStaticMeshComponent;

/**
 * 차량이 플레이어를 들이받았을 때 발행.
 * 넉백 물리 / 2초 다운 / 도구 드롭 같은 플레이어 상태 전이는 플레이어 파트 담당이므로
 * 트래픽 쪽은 "누가 어느 방향으로 맞았는지"만 알리고 관여하지 않는다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCWOnVehicleHitActorSignature, ACWBaseVehicle*, Vehicle, AActor*, HitActor, FVector, ImpactDirection);

/**
 * 패링에 성공당했을 때 발행. 판정이 서버에서만 돌기 때문에 이 델리게이트도 서버에서만 뜬다.
 * 게임플레이 반응(점수, 스택 같은 것)을 붙이는 자리다.
 * 연출을 여기에 붙이면 클라이언트에서는 아무것도 안 보이므로 OnParriedFX 를 쓴다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnVehicleParriedSignature, ACWBaseVehicle*, Vehicle, AActor*, Parrier);

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
	 * 차종 기본 속도에 개체 배율을 적용한 실제 주행 속도(cm/s).
	 * 튕겨나가는 중이면 패링 배율까지 곱한다. 충돌 예측도 이 값을 쓰므로 한곳에서 낸다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Vehicle")
	float GetEffectiveSpeed() const { return Speed * SpeedMultiplier * (bParried ? ParriedSpeedMultiplier : 1.f); }

	/** 콜리전 박스 기준 차량 전체 길이(cm). 차간 거리 계산의 기준값. */
	UFUNCTION(BlueprintPure, Category = "Traffic|Vehicle")
	float GetVehicleLength() const;

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
	 * Target이 이 차량의 진행 경로 위에 있을 때 충돌까지 남은 시간(초)을 낸다.
	 * 경로 밖이거나 이미 지나쳤으면 false.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Parry")
	bool PredictTimeToImpact(const AActor* Target, float& OutTimeToImpact) const;

	/**
	 * 패링 시도. 성공하면 차량이 LaunchDirection 으로 튕겨나가고 true.
	 * 입력 처리와 쿨타임(1초)은 플레이어 파트 담당이므로 여기서 관리하지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Parry")
	bool TryParry(AActor* Parrier, const FVector& LaunchDirection);

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

	/** 차선 끝에 도달했을 때. 풀링을 붙이면 이 지점을 반납 훅으로 쓴다. */
	virtual void OnReachedLaneEnd();

private:
	/**
	 * 앞차를 추월해 통과하지 않도록 이번 프레임 이동 속도를 제한한다.
	 * 앞차가 없으면 제 속도를 그대로 낸다.
	 */
	float ResolveStepSpeed(float DeltaSeconds) const;

	/**
	 * 현재 진행 거리와 좌우 오프셋을 실제 트랜스폼에 반영한다.
	 * StepSpeed는 이번 프레임에 실제로 이동한 속도(cm/s). GetVelocity 가 돌려줄 값에 쓰인다.
	 */
	void UpdateTransformOnLane(float StepSpeed = 0.f);

	/**
	 * 패링당한 사실이 클라이언트에 도착했을 때. 차선 추종을 끊고 튕겨나간 지점으로 자세를 맞춘다.
	 *
	 * 회전은 복제되지 않으므로 여기서 ParriedLaunchDirection 을 보고 직접 세워야 한다.
	 * 그냥 두면 클라이언트에서는 차가 차선 방향 그대로 옆으로 미끄러지는 것처럼 보인다.
	 */
	UFUNCTION()
	void OnRep_Parried();

	/** 오버랩 정보에서 피격 지점을 뽑는다. 스윕이 아니면 최근접점으로 대체한다. */
	FVector ResolveHitLocation(const AActor* HitActor, bool bFromSweep, const FHitResult& SweepResult) const;

	/**
	 * 패링당했을 때 날아갈 방향을 정한다. 기준점은 플레이어가 아니라 이 차량이다.
	 *
	 * 커서 지점을 못 쓰면 플레이어 기준 방향으로, 그것도 비면 TryParry 가 온 길로 되돌려 보낸다.
	 */
	FVector ResolveLaunchDirection(const FCWParryAttemptContext& ParryAttemptContext) const;

	/**
	 * 패링 연출을 모든 머신에서 재생한다. 연출 전용이고 게임플레이 상태는 건드리지 않는다.
	 *
	 * 차량이 실제로 튕겨나가는 것은 bParried / ParriedLaunch* 복제가 담당한다.
	 * 상태를 이 RPC 로 넘기지 않는 이유: 패링 순간에 관련성 밖이었다가 뒤늦게 들어온
	 * 클라이언트는 이 RPC 를 못 받는다. 그때도 복제 프로퍼티는 현재 값이 그대로 도착한다.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayParriedFX(FVector_NetQuantizeNormal LaunchDirection, float LaunchSpeed, AActor* Parrier);

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

	/** 앞차 뒤범퍼와 유지할 최소 여유 거리(cm). 차 길이에 더해서 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle", meta = (ClampMin = "0.0"))
	float FollowingGap = 150.f;

	/** 패링 판정 윈도우(초). 충돌 직전 이 시간 안에 입력해야 성공. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry", meta = (ClampMin = "0.0"))
	float ParryWindow = 0.3f;

	/**
	 * 위협 판정 시 허용하는 좌우 이탈 폭(cm).
	 * 진행 경로에서 이만큼 벗어난 대상은 패링 후보로 보지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry", meta = (ClampMin = "0.0"))
	float ThreatLateralTolerance = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic|Parry")
	bool bCanBeParried = true;

	/**
	 * 패링당해 튕겨나간 뒤 사라지기까지의 시간(초). 2초는 임시값이라 팀 합의로 확정한다.
	 * 무한정 날아가면 맵 밖까지 나가고, 뒤차가 볼 앞차 목록에서도 계속 남는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry", meta = (ClampMin = "0.0"))
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

	/** 패링당해 튕겨나가는 중인 상태. 차선을 벗어나 직진한다. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_Parried, Category = "Traffic|Parry")
	bool bParried = false;

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

	/** 같은 액터를 중복으로 치지 않도록 이미 판정한 대상을 기록. */
	TSet<TWeakObjectPtr<AActor>> HitActors;

	FTimerHandle ParriedDespawnTimerHandle;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Traffic|Vehicle")
	FCWOnVehicleHitActorSignature OnVehicleHitActor;

	UPROPERTY(BlueprintAssignable, Category = "Traffic|Parry")
	FCWOnVehicleParriedSignature OnVehicleParried;
};
