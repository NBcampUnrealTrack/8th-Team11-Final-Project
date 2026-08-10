#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/CWParrySyncData.h"
#include "GameFramework/Character.h"
#include "InputCoreTypes.h"
#include "Traffic/Interfaces/Interface_VehicleHitReceiver.h"
#include "CWPlayerCharacter.generated.h"

class UAnimMontage;
class UCameraComponent;
class UCWParryComponent;
class UCWToolHolderComponent;
class UCWVehicleHitReactionComponent;
class UCWWorkerComponent;
class UInputAction;
enum class EInputActionValueType : uint8;
class UInputMappingContext;
class USoundAttenuation;
class USoundBase;
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

	/**
	 * 기상 애니로 넘어가는 중이라 래그돌 스냅샷 포즈를 밑에 깔아야 하는 구간인지.
	 *
	 * AnimBP 가 이 값으로 슬롯 아래 포즈를 고른다. true 인 동안 이동 블렌드스페이스 대신
	 * Pose Snapshot 을 물려야, 몬타주가 블렌드인 하는 사이에 쓰러진 자세에서 이어진다.
	 * 서 있는 Idle 포즈에서 블렌드하면 일어서다 말고 다시 눕는 것처럼 보인다.
	 */
	UFUNCTION(BlueprintPure, Category = "Hit")
	bool IsUsingRagdollPose() const;

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

	/** 취소 키(Q). 미니게임을 빠져나와 다시 움직일 수 있게 한다. */
	void CancelWork();

	/** 우클릭. 커서 방향으로 패링을 시도한다. 판정은 서버가 한다. */
	void Parry();

	/**
	 * 래그돌을 모든 머신에서 같이 켠다. 서버 기준 위치를 함께 넘겨 피격 시작점이 화면마다 갈라지는 것을 줄인다.
	 * 서버에서만 쓰러뜨리면 소유 클라 화면이 튀고, 각자 현재 위치에서 시작하면 4인 화면의 넉백 시작점이 달라진다.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StartRagdoll(FVector_NetQuantize LaunchVelocity, FVector_NetQuantize ServerActorLocation);

	/**
	 * 래그돌을 풀고 기상 모션을 재생한다. 일어설 자리와 방향은 서버가 정해 넘긴다.
	 *
	 * 물리는 머신마다 조금씩 다르게 굴러서, 각자 자기 자리에서 일어나면 위치가 갈라진다.
	 * 앞뒤 판별도 마찬가지라 서버가 고른 결과를 그대로 쓴다. 머신마다 다른 애니가 나오면 안 된다.
	 *
	 * @param RecoverLocation 캡슐을 세울 자리
	 * @param RecoverYaw      쓰러진 몸이 향하던 방향. 이걸 맞춰야 기상 애니가 제자리에서 시작한다
	 * @param bFaceDown       엎드려 있으면 true. 재생할 기상 애니를 고르는 데 쓴다
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StopRagdoll(FVector_NetQuantize RecoverLocation, float RecoverYaw, bool bFaceDown);

	/** 피격 잠금이 걸리고 풀릴 때. 작업 중단과 도구 드롭을 여기서 처리한다. */
	UFUNCTION()
	void HandleHitLockChanged(UCWVehicleHitReactionComponent* HitReactionComponent, bool bHitLocked);

	/**
	 * 다운 시간이 끝나 일어날 차례가 됐을 때. 서버에서만 들어온다.
	 * 래그돌을 풀고 기상 모션을 뿌린 뒤, 모션이 끝나는 시점에 컴포넌트로 완료를 통보한다.
	 */
	UFUNCTION()
	void HandleGetUpRequested(UCWVehicleHitReactionComponent* HitReactionComponent);

	/**
	 * 패링이 성공했을 때. 서버가 확정한 결과를 멀티캐스트로 받은 것이라
	 * 모든 머신에서 같은 시점에 들어온다. 연출 외의 판정은 여기서 하지 않는다.
	 */
	UFUNCTION()
	void HandleParrySucceeded(const FCWParrySyncData& ParrySyncData);

private:
	virtual void BeginPlay() override;

	/** 피격으로 쓰러져 있는 동안인지. 이동·작업·패링이 전부 막힌다. */
	bool IsHitLocked() const;

	/**
	 * 커서 아래 월드 지점을 찾는다. 패링 방향의 기준점이 된다.
	 * 트레이스가 빗나가면 캐릭터 위치를 그대로 낸다. 그러면 패링 컴포넌트가 정면으로 대체한다.
	 */
	FVector ResolveCursorWorldLocation() const;

	/** 비어 있는 InputAction 을 C++ 로 채우고, 빠진 키 매핑을 보완 컨텍스트에 담는다. */
	void BuildBuiltInInput();

	/**
	 * 래그돌을 켜고 임펄스를 먹인다. 무브먼트와 캡슐 콜리전을 함께 내린다.
	 * 캡슐이 살아 있으면 래그돌이 자기 캡슐에 걸려 튀고, 무브먼트가 돌면 물리와 서로 밀어낸다.
	 */
	void StartRagdoll(const FVector& LaunchVelocity);

	/**
	 * 래그돌을 끄고 캡슐을 주어진 자리와 방향으로 옮긴 뒤 다시 세운다.
	 *
	 * 물리를 끄기 직전에 쓰러진 포즈를 스냅샷으로 떠 둔다. AnimBP 가 이 포즈에서
	 * 기상 애니 첫 프레임으로 블렌드해야 몸이 순간적으로 튀지 않는다.
	 */
	void StopRagdoll(const FVector& RecoverLocation, float RecoverYaw);

	/**
	 * 기상 몬타주를 재생하고 길이(초)를 돌려준다. 애셋이 없거나 재생에 실패하면 0 이다.
	 * 재생 자체는 각 머신이 한다. 몬타주 복제를 켜면 같은 연출이 두 번 흘러 어긋난다.
	 */
	float PlayGetUpMontage(bool bFaceDown);

	/** 기상 모션이 끝났다고 컴포넌트에 알린다. 서버 타이머로만 들어온다. */
	void FinishGetUp();

	/** 쓰러진 몸이 엎드려 있는지 본다. 기상 애니를 앞뒤 중 어느 쪽으로 재생할지 정한다. */
	bool IsRagdollFaceDown() const;

	/** 쓰러진 몸이 향하던 방향(Yaw). 캡슐을 이 방향으로 세워야 기상 애니가 제자리에서 시작한다. */
	float ResolveRecoverYaw(bool bFaceDown) const;

	/**
	 * 쓰러진 몸의 축을 구한다. 본의 로컬 회전을 쓰지 않고 본 사이의 위치 차이로 계산한다.
	 *
	 * 스켈레톤마다 본 로컬 축 규약이 제각각이라(마네킹과 믹사모가 다르다) 회전값을 믿으면
	 * 캐릭터를 갈아끼울 때 앞뒤가 뒤집힌다. 위치 차이는 규약과 무관하다.
	 *
	 * @param OutBodyUp      골반에서 머리로 향하는 방향
	 * @param OutBodyForward 가슴이 바라보는 방향
	 * @return 필요한 본을 모두 찾았으면 true
	 */
	bool ResolveRagdollBodyAxes(FVector& OutBodyUp, FVector& OutBodyForward) const;

	/**
	 * 래그돌이 멈췄는지 본다. 서버에서만 돈다.
	 *
	 * 래그돌 중에는 무브먼트가 꺼져 있어 ACharacter::Landed 가 오지 않는다.
	 * 그래서 착지 통보를 이쪽에서 대신 한다. 몸이 멎어야 일어나기 시작하는 게 자연스럽다.
	 */
	void CheckRagdollRest();

	/** 일어설 자리를 고른다. 쓰러진 몸통 아래 바닥을 찾아 캡슐을 올려놓는다. */
	FVector ResolveRecoverLocation() const;

	/**
	 * 차에 치인 소리를 낸다. 각 머신이 알아서 내므로 권한과 무관하게 불린다.
	 *
	 * 본인 피격도 감쇠에 태운다. 내가 맞은 지점은 리스너와 거리가 0 이라
	 * 어차피 풀볼륨으로 나오고, 남이 맞는 소리만 거리에 따라 멀어진다.
	 */
	void PlayVehicleHitSound();

	/**
	 * 쓸 감쇠 설정을 고른다. 애셋이 지정돼 있으면 그것을, 없으면 기본값으로 하나 만들어 쓴다.
	 * 만든 것은 캐시해 두고 재사용한다.
	 */
	USoundAttenuation* ResolveSoundAttenuation();

	/** 비어 있으면 만들고, 이미 지정돼 있으면 그대로 둔다. */
	void EnsureInputAction(TObjectPtr<UInputAction>& Action, const TCHAR* ActionName, EInputActionValueType ValueType);

	/**
	 * 지정된 IMC 애셋이 다루지 않는 액션만 기본 키로 매핑해 둔다.
	 *
	 * BP 가 IMC 를 물고 있어도 그 안에 빠진 입력이 있으면 조용히 죽는데,
	 * BindAction 은 액션이 null 이어도 경고 없이 넘어가서 원인을 찾기가 어렵다.
	 * 지정된 컨텍스트는 애셋이라 런타임에 매핑을 추가하면 안 되므로 별도 컨텍스트에 담는다.
	 */
	void BuildSupplementContext();

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

	/** 래그돌 위치의 기준이 되는 본. 마네킹 기준값이라 스켈레톤을 갈면 바꿔야 한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Hit")
	FName RagdollPivotBone = TEXT("pelvis");

	/** 몸통 방향을 재는 데 쓰는 머리 쪽 본. 골반과의 위치 차이가 몸통 축이 된다. */
	UPROPERTY(EditDefaultsOnly, Category = "Hit")
	FName RagdollHeadBone = TEXT("head");

	/** 몸의 좌우 축을 재는 데 쓰는 양 어깨 본. 둘의 위치 차이가 오른쪽 방향이 된다. */
	UPROPERTY(EditDefaultsOnly, Category = "Hit")
	FName RagdollLeftShoulderBone = TEXT("upperarm_l");

	UPROPERTY(EditDefaultsOnly, Category = "Hit")
	FName RagdollRightShoulderBone = TEXT("upperarm_r");

	/** 이 속도 아래로 떨어지면 다 굴렀다고 본다(cm/s). */
	UPROPERTY(EditDefaultsOnly, Category = "Hit", meta = (ClampMin = "0.0"))
	float RagdollRestSpeed = 60.f;

	/** 멈췄는지 확인하는 주기(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Hit", meta = (ClampMin = "0.01"))
	float RagdollRestCheckInterval = 0.1f;

	/**
	 * 확인을 시작하기까지 두는 시간(초).
	 * 임펄스가 물리에 반영되기 전에 재면 속도가 0 이라 튕겨나가자마자 멈춘 것으로 잡힌다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hit", meta = (ClampMin = "0.0"))
	float RagdollRestCheckDelay = 0.4f;

protected:
	// 아래 기상 설정은 전부 비워도 된다. 몬타주가 비어 있으면 연출만 빠지고 지금처럼 바로 일어선다.

	/** 등을 대고 누운 상태에서 일어나는 몬타주. */
	UPROPERTY(EditDefaultsOnly, Category = "Hit|GetUp")
	TObjectPtr<UAnimMontage> GetUpFromBackMontage;

	/** 엎드린 상태에서 일어나는 몬타주. 비워두면 누운 쪽 몬타주를 대신 쓴다. */
	UPROPERTY(EditDefaultsOnly, Category = "Hit|GetUp")
	TObjectPtr<UAnimMontage> GetUpFromFrontMontage;

	/**
	 * 래그돌 포즈를 담아 두는 스냅샷 이름. AnimBP 의 Pose Snapshot 노드에 같은 이름을 넣어야 한다.
	 * 이름이 어긋나면 스냅샷이 빈 포즈로 잡혀 기상 첫 프레임에 몸이 T 포즈로 튄다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hit|GetUp")
	FName RagdollPoseSnapshotName = TEXT("RagdollPose");

	/**
	 * 기상 애니가 몸통 방향과 어긋날 때 돌려주는 보정각(도).
	 *
	 * 애니 제작 기준이 애셋마다 달라서, 누운 캐릭터가 애니 안에서 +X 를 보고 있다는 보장이 없다.
	 * 일어나는 순간 몸이 홱 도는 것처럼 보이면 여기를 90 단위로 돌려가며 맞춘다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hit|GetUp", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float GetUpFromBackYawOffset = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Hit|GetUp", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float GetUpFromFrontYawOffset = 0.f;

	/**
	 * 몬타주가 끝나기 이만큼 전에 조작을 돌려준다(초).
	 *
	 * 기상 애니 끝자락은 이미 다 선 자세를 유지하는 구간이라, 끝까지 기다리면 화면상 서 있는데
	 * 키가 안 먹는 답답한 순간이 생긴다. 조금 일찍 풀어 이동 애니로 자연스럽게 넘어가게 한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hit|GetUp", meta = (ClampMin = "0.0"))
	float GetUpUnlockLeadTime = 0.15f;

	/**
	 * 래그돌 스냅샷 포즈를 밑에 깔아 두는 시간(초). IsUsingRagdollPose 가 이 시간만큼 true 다.
	 * 기상 몬타주의 Blend In 시간보다 조금 길게 잡는다. 짧으면 블렌드가 끝나기 전에
	 * 밑 포즈가 Idle 로 바뀌면서 몸이 한 번 들썩인다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hit|GetUp", meta = (ClampMin = "0.0"))
	float RagdollPoseBlendTime = 0.35f;

protected:
	// 아래 사운드 셋은 전부 비워도 된다. 비어 있으면 그 소리만 나지 않고 나머지는 정상 동작한다.

	/** 차에 치이는 순간 나는 소리. */
	UPROPERTY(EditDefaultsOnly, Category = "Sound")
	TObjectPtr<USoundBase> VehicleHitSound;

	/** 패링에 성공해 차를 튕겨낸 순간 나는 소리. */
	UPROPERTY(EditDefaultsOnly, Category = "Sound")
	TObjectPtr<USoundBase> ParrySuccessSound;

	/**
	 * 거리 감쇠 설정. 비워두면 아래 두 값으로 C++ 이 만든 기본 감쇠가 쓰인다.
	 * 사운드 쪽에서 세밀하게 잡고 싶어지면 애셋을 만들어 여기에 꽂으면 그쪽이 우선한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Sound")
	TObjectPtr<USoundAttenuation> SoundAttenuation;

	/** 기본 감쇠에서 볼륨이 깎이지 않는 반경(cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Sound", meta = (ClampMin = "0.0"))
	float AttenuationInnerRadius = 400.f;

	/**
	 * 기본 감쇠가 다 깎여 없어지는 거리(cm).
	 *
	 * 카메라가 1200cm 위에서 55도로 내려다보므로 화면에 담기는 폭이 대략 2500cm 다.
	 * 이보다 크게 잡으면 화면 밖 소리까지 들려 정보가 아니라 소음이 된다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Sound", meta = (ClampMin = "0.0"))
	float AttenuationFalloffDistance = 2500.f;

protected:
	// 아래 6개는 비워두면 C++ 이 만든 기본 입력이 쓰인다. 채우면 그쪽이 우선한다.
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

	// 미니게임 중 빠져나오는 키(Q). 비워두면 그 기능만 없고 나머지는 정상 동작한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> CancelWorkAction;

private:
	// 애셋 IMC 가 빠뜨린 키만 담는 보완 컨텍스트. 지정된 컨텍스트 위에 얹는다.
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> SupplementContext;

	// SoundAttenuation 이 비었을 때 C++ 이 만들어 쓰는 기본 감쇠. 한 번만 만든다.
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> RuntimeSoundAttenuation;

	// 래그돌을 풀 때 메시를 되돌릴 기준값. BP 에서 다르게 놨을 수 있어 BeginPlay 에서 읽는다.
	FTransform DefaultMeshRelativeTransform;

	FName DefaultMeshCollisionProfile;

	bool bRagdollActive = false;

	// 스냅샷 포즈를 밑에 깔고 있는 구간. 각 머신이 자기 화면 기준으로 관리한다
	bool bUsingRagdollPose = false;

	FTimerHandle RagdollRestTimerHandle;

	FTimerHandle GetUpTimerHandle;

	FTimerHandle RagdollPoseBlendTimerHandle;
};
