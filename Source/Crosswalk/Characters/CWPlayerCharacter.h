#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputCoreTypes.h"
#include "Traffic/Interfaces/Interface_VehicleHitReceiver.h"
#include "CWPlayerCharacter.generated.h"

class UCameraComponent;
class UCWParryComponent;
class UCWToolHolderComponent;
class UCWVehicleHitReactionComponent;
class UCWWorkerComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
struct FInputActionValue;

/**
 * 쿼터뷰 협동 플레이어 캐릭터.
 *
 * 작업/도구 로직은 컴포넌트가 들고 있고, 이 클래스는 입력을 그쪽으로 넘기는 역할만 한다.
 *   - UCWWorkerComponent     : 작업 키를 누르고 있는 동안 존 게이지를 올린다
 *   - UCWToolHolderComponent : E 로 도구를 집고 놓는다
 *   - UCWParryComponent      : 우클릭으로 커서 방향의 차량을 패링한다
 *   - UCWVehicleHitReactionComponent : 차에 치였을 때 행동 잠금 상태를 관리한다
 *
 * 입력 구성
 *   Input Action 과 Mapping Context 를 C++ 이 런타임에 직접 만든다.
 *   에디터에서 애셋을 만들거나 지정할 필요가 없고, 값 타입(Axis2D/Boolean)을 틀릴 여지도 없다.
 *   나중에 키 배치를 애셋으로 관리하고 싶어지면 DefaultMappingContext 와 세 액션을
 *   BP 디테일 패널에서 채우면 그쪽이 우선 적용된다.
 *
 * 메쉬도 마네킹 플레이스홀더를 생성자에서 직접 물린다. BP 를 만들지 않아도 바로 플레이된다.
 */
UCLASS()
class CROSSWALK_API ACWPlayerCharacter : public ACharacter, public IInterface_VehicleHitReceiver
{
	GENERATED_BODY()

public:
	ACWPlayerCharacter();

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** 넉백으로 날아간 뒤 착지한 시점. 여기서부터 다운 시간이 흐른다. */
	virtual void Landed(const FHitResult& Hit) override;

	/**
	 * 차량에 치였을 때 트래픽 쪽에서 들어온다. 서버에서만 유효하다.
	 * 넉백 물리와 행동 잠금 시작은 여기서, 잠금 해제와 시간 관리는 피격 반응 컴포넌트가 한다.
	 */
	virtual void ReceiveVehicleHit_Implementation(const FCWVehicleHitContext& HitContext) override;

protected:
	/** WASD 8방향 이동. 카메라가 고정돼 있으므로 화면 기준 방향이 그대로 이동 방향이 된다. */
	void Move(const FInputActionValue& Value);

	/** E. 도구 집기 / 내려놓기. */
	void Interact();

	/** 작업 키를 누른 순간. */
	void StartWork();

	/** 작업 키를 뗀 순간. */
	void StopWork();

	/** 우클릭. 커서 방향으로 패링을 시도한다. 판정은 서버가 한다. */
	void Parry();

	/** 넉백을 모든 머신에서 같이 적용한다. 서버에서만 이동시키면 소유 클라 화면이 튄다. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ApplyHitKnockback(FVector_NetQuantize LaunchVelocity);

	/** 피격 잠금이 걸리고 풀릴 때. 작업 중단과 도구 드롭을 여기서 처리한다. */
	UFUNCTION()
	void HandleHitLockChanged(UCWVehicleHitReactionComponent* HitReactionComponent, bool bHitLocked);

private:
	virtual void BeginPlay() override;

	/** 피격으로 쓰러져 있는 동안인지. 이동·작업·패링이 전부 막힌다. */
	bool IsHitLocked() const;

	/**
	 * 커서 아래 월드 지점을 찾는다. 패링 방향의 기준점이 된다.
	 * 트레이스가 빗나가면 캐릭터 위치를 그대로 낸다. 그러면 패링 컴포넌트가 정면으로 대체한다.
	 */
	FVector ResolveCursorWorldLocation() const;

	/** 애셋이 지정되지 않았으면 IA/IMC 를 C++ 로 만들어 채운다. */
	void BuildBuiltInInput();

	/**
	 * 패링 입력이 비어 있으면 C++ 로 채운다.
	 *
	 * BP 가 입력 애셋을 물고 있으면 BuildBuiltInInput 이 통째로 건너뛰는데,
	 * 애셋 쪽에는 아직 패링 액션이 없어서 우클릭이 조용히 죽는다.
	 * BindAction 은 액션이 null 이어도 경고 없이 넘어가므로 원인을 찾기가 어렵다.
	 */
	void BuildParryFallbackInput();

	/**
	 * MoveAction 에 키 하나를 매핑한다.
	 * @param bSwizzleToForwardAxis 앞뒤(W/S) 키면 true. 값을 X축에서 Y축으로 옮긴다.
	 * @param bNegate               반대 방향(S/A) 키면 true.
	 */
	void AddMoveKeyMapping(UInputMappingContext* Context, const FKey& Key, bool bSwizzleToForwardAxis, bool bNegate);

	/**
	 * 매핑 컨텍스트를 등록한다.
	 *
	 * BeginPlay 에서 부르면 안 된다. 폰의 BeginPlay 는 컨트롤러가 Possess 하기 전에 돌기 때문에
	 * GetController() 가 nullptr 이고, 컨텍스트가 등록되지 않아 모든 입력이 죽는다.
	 * SetupPlayerInputComponent 는 Possess 이후에 호출되므로 여기서 부르는 게 안전하다.
	 */
	void ApplyMappingContext();

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> TopDownCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UCWWorkerComponent> WorkerComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tool")
	TObjectPtr<UCWToolHolderComponent> ToolHolderComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Parry")
	TObjectPtr<UCWParryComponent> ParryComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hit")
	TObjectPtr<UCWVehicleHitReactionComponent> HitReactionComponent;

protected:
	/**
	 * 넉백 속도를 차량 속도에서 뽑을 때 쓰는 배율.
	 * 기획서 6.2 는 "멀리 날아간다"고만 하고 값이 없어 테스트로 잡는다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hit", meta = (ClampMin = "0.0"))
	float KnockbackSpeedScale = 1.2f;

	/** 넉백에 섞는 위쪽 속도(cm/s). 0이면 바닥을 미끄러지듯 밀려나 우스꽝스러운 맛이 없다. */
	UPROPERTY(EditDefaultsOnly, Category = "Hit", meta = (ClampMin = "0.0"))
	float KnockbackUpSpeed = 500.f;

protected:
	// 아래 5개는 비워두면 C++ 이 만든 기본 입력이 쓰인다. 채우면 그쪽이 우선한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> WorkAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ParryAction;

private:
	// 패링만 담은 보조 매핑 컨텍스트. 지정된 컨텍스트는 애셋이라 런타임에 건드리지 않는다.
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> ParryFallbackContext;

	// 첫 이동 입력만 로그로 찍기 위한 플래그. 매 프레임 찍으면 로그를 못 쓰게 된다.
	bool bLoggedFirstMoveInput = false;
};
