// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/TrafficVehicleSpawnParams.h"
#include "Interfaces/Interface_ParryReceiver.h"
#include "BaseVehicle.generated.h"

class ABaseVehicle;
class ATrafficLane;
class UBoxComponent;
class UStaticMeshComponent;

/**
 * 차량이 플레이어를 들이받았을 때 발행.
 * 넉백 물리 / 2초 다운 / 도구 드롭 같은 플레이어 상태 전이는 플레이어 파트 담당이므로
 * 트래픽 쪽은 "누가 어느 방향으로 맞았는지"만 알리고 관여하지 않는다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnVehicleHitActorSignature, ABaseVehicle*, Vehicle, AActor*, HitActor, FVector, ImpactDirection);

/**
 * 패링에 성공당했을 때 발행. 판정이 서버에서만 돌기 때문에 이 델리게이트도 서버에서만 뜬다.
 * 게임플레이 반응(점수, 스택 같은 것)을 붙이는 자리다.
 * 연출을 여기에 붙이면 클라이언트에서는 아무것도 안 보이므로 OnParriedFX 를 쓴다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVehicleParriedSignature, ABaseVehicle*, Vehicle, AActor*, Parrier);

/**
 * 차선 스플라인을 따라 직선 주행하는 차량의 공통 베이스.
 * 승용차 / 오토바이 / 트럭 등 차종은 이 클래스를 상속한 BP로 만든다.
 *
 * 차량은 플레이어를 추적하지 않는다 - 거동이 예측 가능해야 회피/패링 학습이 성립한다.
 * 개체별 편차(속도 배율, 좌우 오프셋)는 스포너가 굴려서 넘긴다.
 * 앞차와는 최소 간격을 유지하므로 같은 차선에서 서로 통과하지 않는다.
 */
UCLASS()
class CROSSWALK_API ABaseVehicle : public AActor, public IInterface_ParryReceiver
{
	GENERATED_BODY()

public:
	ABaseVehicle();

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * 서버 권한 패링 판정. 대상 선별은 트래픽 서브시스템이 끝낸 뒤 이 차량에게만 들어온다.
	 * 반경·각도는 선별 단계에서 걸러지므로, 여기서는 차량이 단일 관리하는 윈도우만 다시 본다.
	 */
	virtual bool ReceiveParryAttempt_Implementation(const FCWParryAttemptContext& ParryAttemptContext) override;

	/** 스포너가 스폰 직후 호출. 주행할 차선과 개체별 편차를 지정한다. */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Vehicle")
	void InitializeOnLane(ATrafficLane* InLane, const FTrafficVehicleSpawnParams& Params);

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
	ATrafficLane* GetCurrentLane() const { return CurrentLane; }

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

	/** 현재 진행 거리와 좌우 오프셋을 실제 트랜스폼에 반영한다. */
	void UpdateTransformOnLane();

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
	 * 차량은 이동만 복제되므로, 클라이언트 입장에서는 차가 갑자기 방향을 트는 것으로만 보인다.
	 * "패링당했다"는 사건 자체는 이 RPC 로만 전달된다.
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

	/** 현재 주행 중인 차선. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Traffic|Vehicle")
	TObjectPtr<ATrafficLane> CurrentLane;

	/** 차선 시작점에서 진행한 거리(cm). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Traffic|Vehicle")
	float DistanceAlongLane = 0.f;

	/** 스포너가 굴린 개체별 속도 배율. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Traffic|Vehicle")
	float SpeedMultiplier = 1.f;

	/** 스포너가 굴린 차선 중심선 대비 좌우 오프셋(cm). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Traffic|Vehicle")
	float LaneLateralOffset = 0.f;

	/** 패링당해 튕겨나가는 중인 상태. 차선을 벗어나 직진한다. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Traffic|Parry")
	bool bParried = false;

private:
	/** 같은 액터를 중복으로 치지 않도록 이미 판정한 대상을 기록. */
	TSet<TWeakObjectPtr<AActor>> HitActors;

	FTimerHandle ParriedDespawnTimerHandle;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Traffic|Vehicle")
	FOnVehicleHitActorSignature OnVehicleHitActor;

	UPROPERTY(BlueprintAssignable, Category = "Traffic|Parry")
	FOnVehicleParriedSignature OnVehicleParried;
};
