#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputCoreTypes.h"
#include "CWPlayerCharacter.generated.h"

class UCameraComponent;
class UCWToolHolderComponent;
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
class CROSSWALK_API ACWPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ACWPlayerCharacter();

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

protected:
	/** WASD 8방향 이동. 카메라가 고정돼 있으므로 화면 기준 방향이 그대로 이동 방향이 된다. */
	void Move(const FInputActionValue& Value);

	/** E. 도구 집기 / 내려놓기. */
	void Interact();

	/** 작업 키를 누른 순간. */
	void StartWork();

	/** 작업 키를 뗀 순간. */
	void StopWork();

private:
	/** 애셋이 지정되지 않았으면 IA/IMC 를 C++ 로 만들어 채운다. */
	void BuildBuiltInInput();

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

protected:
	// 아래 4개는 비워두면 C++ 이 만든 기본 입력이 쓰인다. 채우면 그쪽이 우선한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> WorkAction;

private:
	// 첫 이동 입력만 로그로 찍기 위한 플래그. 매 프레임 찍으면 로그를 못 쓰게 된다.
	bool bLoggedFirstMoveInput = false;
};
