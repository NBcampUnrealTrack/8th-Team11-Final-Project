#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/CWParrySyncData.h"
#include "GameFramework/Character.h"
#include "InputCoreTypes.h"
#include "Traffic/Interfaces/Interface_VehicleHitReceiver.h"
#include "CWPlayerCharacter.generated.h"

class UAnimMontage;
class UCameraComponent;
class UNiagaraSystem;
class UCWParryComponent;
class UCWSupplyCarrierComponent;
class UCWTemperatureComponent;
class UCWToolHolderComponent;
class UCWVehicleHitReactionComponent;
class UCWWearableComponent;
class UCWWorkerComponent;
class UInputAction;
enum class EInputActionValueType : uint8;
class UInputMappingContext;
class USoundAttenuation;
class USkeletalMeshComponent;
class USoundBase;
class USpringArmComponent;
struct FInputActionValue;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * 쿼터뷰 협동 플레이어 캐릭터.
 *
 * 작업/도구 로직은 컴포넌트가 들고 있고, 이 클래스는 입력을 그쪽으로 넘기는 역할만 한다.
 *   - UCWWorkerComponent     : 작업 키를 누르고 있는 동안 존 게이지를 올린다
 *   - UCWToolHolderComponent : E 로 도구를 집고 놓는다
 *   - UCWParryComponent      : 우클릭으로 커서 방향의 차량을 패링한다
 *   - UCWVehicleHitReactionComponent : 차에 치였을 때 행동 잠금 상태를 관리한다
 *   - UCWTemperatureComponent : 작업으로 오른 체온에 따라 이동속도와 작업속도를 깎는다
 *   - UCWSupplyCarrierComponent : F/G/R 로 페인트 통을 꺼내고 반납하고 도구에 붓는다
 *   - UCWWearableComponent    : H 로 헬멧을 갈아 끼우고 J 로 작업복을 입고 벗는다
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

	/**
	 * E. 상호작용.
	 *
	 * 대상이 두 종류다. 바닥의 도구는 도구 소지 컴포넌트가, 작업차량처럼 도구가 아닌 것은
	 * IInterface_Interactable 이 받는다. 둘 중 가까운 쪽을 고른다.
	 */
	void Interact();

	/** 작업 키를 누른 순간. */
	void StartWork();

	/** 작업 키를 뗀 순간. */
	void StopWork();

	/** 취소 키(Q). 미니게임을 빠져나와 다시 움직일 수 있게 한다. */
	void CancelWork();

	/** F. 보급 박스에서 페인트 통을 꺼낸다. (기획서 6-1) */
	void PickUpCan();

	/** G. 페인트 통을 박스에 반납한다. (기획서 6-2) */
	void DropCan();

	/** R. 가장 가까운 도구에 페인트를 붓는다. (기획서 6-3) */
	void ChargePaint();

	/** 우클릭. 커서 방향으로 패링을 시도한다. 판정은 서버가 한다. */
	void Parry();

	/**
	 * H. 헬멧을 다음 것으로 갈아 끼운다.
	 *
	 * 피격 잠금(IsHitLocked)을 확인하지 않는다. 순수 외형이라 쓰러져 있는 동안 바꿔도
	 * 게임플레이에 영향이 없고, 오히려 래그돌 중에 헬멧이 머리를 잘 따라가는지 확인하기 좋다.
	 */
	void SwapHelmet();

	/** J. 옷을 전부 입거나 전부 벗는다. 헬멧과 같은 이유로 피격 잠금을 확인하지 않는다. */
	void ToggleOutfit();

	/**
	 * 래그돌을 모든 머신에서 같이 켠다. 서버 기준 위치를 함께 넘겨 피격 시작점이 화면마다 갈라지는 것을 줄인다.
	 * 서버에서만 쓰러뜨리면 소유 클라 화면이 튀고, 각자 현재 위치에서 시작하면 4인 화면의 넉백 시작점이 달라진다.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StartRagdoll(FVector_NetQuantize LaunchVelocity, FVector_NetQuantize ServerActorLocation);

	/**
	 * 날아가는 도중 서버가 확인한 pelvis 위치를 주기적으로 뿌린다. 위치를 순간이동시키지 않고
	 * 그 방향으로 살짝 임펄스만 얹어서, 시작점은 같아도 물리 시뮬레이션이 시간이 지나며
	 * 벌어지는 것을 완화한다 (CheckRagdollRest 참고). 몇 번 빠져도 다음 것이 이어받으므로 Unreliable.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_SyncRagdollPosition(FVector_NetQuantize ServerPelvisLocation);

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

	//체온이 바뀔 때, 머티리얼의 세기 갱신.
	UFUNCTION()
	void HandleTemperatureChanged(UCWTemperatureComponent* InTemperatureComponent, float Temperature, float HeatRatio);
	/**
	 * 도구가 아닌 상호작용 대상을 서버에 요청한다.
	 *
	 * 상호작용 판정은 서버에서만 유효하다. (IInterface_Interactable 의 계약)
	 * 어느 대상인지 클라이언트가 알려주지만, 승인 여부는 서버가 CanInteract 로 다시 판단한다.
	 */
	UFUNCTION(Server, Reliable)
	void Server_InteractWith(AActor* Target);

private:
	virtual void BeginPlay() override;

	/**
	 * 근처에서 상호작용할 수 있는 대상을 찾는다. 도구는 제외한다.
	 *
	 * 도구를 빼는 이유: 도구는 도구 소지 컴포넌트가 매 프레임 포커스를 계산하고 하이라이트까지
	 * 관리하고 있다. 여기서 같이 잡으면 같은 도구를 두 경로가 서로 다른 기준으로 고르게 된다.
	 *
	 * @param OutDistanceSquared 찾은 대상까지의 거리(제곱). 도구와 어느 쪽이 가까운지 비교하는 데 쓴다.
	 */
	AActor* FindNearestInteractable(float& OutDistanceSquared) const;

	/** 상호작용을 실행한다. 서버면 바로, 클라이언트면 요청을 보낸다. */
	void PerformInteractWith(AActor* Target);

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
	 *
	 * 위치 동기화(ServerActorLocation)를 여기서 처리하는 이유: 무브먼트를 끄기 전에 위치부터
	 * 옮기면, 맞은 사람 본인 화면(오토너머스 프록시)의 로컬 무브먼트 예측이 그 위치를 다시
	 * 밀어낼 수 있다. 그래서 반드시 무브먼트를 끈 다음에 위치를 맞춰야 한다.
	 */
	void StartRagdoll(const FVector& LaunchVelocity, const FVector& ServerActorLocation);

	/**
	 * 래그돌을 끄고 캡슐을 주어진 자리와 방향으로 옮긴 뒤 다시 세운다.
	 *
	 * 물리를 끄기 직전에 쓰러진 포즈를 스냅샷으로 떠 둔다. AnimBP 가 이 포즈에서
	 * 기상 애니 첫 프레임으로 블렌드해야 몸이 순간적으로 튀지 않는다.
	 */
	void StopRagdoll(const FVector& RecoverLocation, float RecoverYaw);

	/**
	 * 복귀 스냅 직후, 메시를 방금 전(순간이동 전) 자리에서 기본 포즈까지 짧게 이어붙인다.
	 * 래그돌 물리는 서버·클라이언트가 각자 따로 돌기 때문에, 다운 중 벌어진 위치 오차가 크면
	 * StopRagdoll 의 순간이동이 그대로 "텔레포트"처럼 보인다. 게임플레이 위치(캡슐)는
	 * 이미 RecoverLocation 으로 확정된 뒤라 조작 재개는 지연되지 않고, 보이는 몸통만 따라붙는다.
	 */
	void TickRecoverBlend();

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

	/**
	 * 기상이 끝난 뒤 메시와 카메라 붐이 제자리에 있는지 확인하고, 벗어나면 경고를 남긴다.
	 *
	 * 과거에 래그돌 복귀 시 상대 트랜스폼 오프셋이 피격마다 누적되던 버그가 있었다.
	 * 쌓이는 방향에 따라 메시가 사라진 것처럼도, 카메라가 몸에 박힌 것처럼도 보였다.
	 * 복귀 순서를 다시 손댈 때 되살아나기 쉬운 종류라 감시를 남겨 둔다.
	 */
	void VerifyRecoverAlignment() const;

	/** 일어설 자리를 고른다. 쓰러진 몸통 아래 바닥을 찾아 캡슐을 올려놓는다. */
	FVector ResolveRecoverLocation() const;

	/**
	 * 차에 치인 소리를 낸다. 각 머신이 알아서 내므로 권한과 무관하게 불린다.
	 *
	 * 두 소리를 다르게 놓는다. 부딪히는 소리는 충돌 지점에 고정하고, 비명은 몸에 붙여
	 * 날아가는 동안 같이 따라가게 한다. 둘 다 비워둬도 되고, 한쪽만 채워도 된다.
	 *
	 * 본인 피격도 감쇠에 태운다. 내가 맞은 지점은 리스너와 거리가 0 이라
	 * 어차피 풀볼륨으로 나오고, 남이 맞는 소리만 거리에 따라 멀어진다.
	 */
	void PlayVehicleHitSound();

	/**
	 * 차에 치인 이펙트를 낸다. 사운드와 마찬가지로 권한과 무관하게 각 머신에서 불린다.
	 *
	 * 스폰 위치와 방향은 복제된 피격 정보에서 가져온다. 그래서 이펙트용 멀티캐스트를
	 * 따로 만들 필요가 없다. 잠금 플래그가 복제되는 것만으로 모든 머신에서 한 번씩 난다.
	 */
	void PlayVehicleHitEffect();

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Temperature")
	TObjectPtr<UCWTemperatureComponent> TemperatureComponent;

	/**
	 * 체온 오라 머티리얼 오버라이드. 비워 두면 메시에 걸린 외곽선 머티리얼을 그대로 감싼다.
	 * 외곽선과 같은 오버레이 슬롯을 쓰므로, 지정한다면 외곽선까지 그리는 머티리얼이어야 한다.
	 * 체온 시스템 자체와는 무관하고 연출만 담당한다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category= "Temperature|FX")
	TObjectPtr<UMaterialInterface> HeatOverlayMaterial;

	//HeatRatio 넣는 곳.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeatOverlayMID;

	// 페인트 통 운반. (기획서 6-1 ~ 6-3, 6-6)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crosswalk|Component")
	TObjectPtr<UCWSupplyCarrierComponent> SupplyCarrierComponent;

	/**
	 * 헬멧과 옷을 함께 관리한다. 어떤 슬롯이 있고 무엇을 입을 수 있는지는
	 * 카탈로그 애셋(UCWWearableCatalog)이 정의하므로 이 클래스는 슬롯을 만들지 않는다.
	 *
	 * HeadgearComponent + OutfitComponent + 슬롯 컴포넌트 8개를 대체한다.
	 * BP 클래스 디폴트에서 이 컴포넌트의 Catalog 를 지정해야 동작한다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wearable")
	TObjectPtr<UCWWearableComponent> WearableComponent;

protected:
	/**
	 * 넉백 속도를 차량 속도에서 뽑을 때 쓰는 배율.
	 * 기획서 6.2 는 "멀리 날아간다"고만 하고 값이 없어 테스트로 잡는다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hit", meta = (ClampMin = "0.0"))
	float KnockbackSpeedScale = 1.2f;

	// 도구가 아닌 상호작용 대상을 찾는 반경(cm). 도구 집기 반경(홀더의 PickupRadius)과 별개다.
	// 작업차량은 도구보다 덩치가 커서 같은 반경으로 잡으면 트렁크 앞에 붙어야만 반응한다.
	UPROPERTY(EditDefaultsOnly, Category = "Interaction", meta = (ClampMin = "0.0"))
	float InteractRadius = 250.f;

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

	/**
	 * 날아가는 도중 위치 보정을 몇 초마다 뿌릴지. RagdollRestCheckInterval 배수로 맞춰지므로
	 * 이보다 짧게 잡아도 실제로는 그 주기로만 나간다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hit", meta = (ClampMin = "0.0"))
	float RagdollCorrectionInterval = 0.1f;

	/** 이 거리(cm)보다 오차가 작으면 보정을 건너뛴다. 매번 미세하게 당기면 떨리는 것처럼 보인다. */
	UPROPERTY(EditDefaultsOnly, Category = "Hit", meta = (ClampMin = "0.0"))
	float RagdollCorrectionMinDistance = 15.f;

	/**
	 * 오차를 임펄스로 환산하는 배율. 값이 클수록 세게 당겨온다.
	 * 실측 튜닝이 필요한 값이다 — 너무 크면 홱 당겨져 부자연스럽고, 너무 작으면 효과가 없다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hit", meta = (ClampMin = "0.0"))
	float RagdollCorrectionStrength = 1.2f;

	/**
	 * 복귀 순간이동 후, 메시를 기본 포즈까지 이어붙이는 최소 시간(초). 0이면 즉시 스냅한다.
	 * 오차가 작을 때(=대부분의 경우) 이 시간으로 끝난다. 실제 걸리는 시간은 오차 크기에 따라
	 * 아래 RecoverBlendSpeed 로 늘어난다 (StopRagdoll에서 계산하는 RecoverBlendActiveDuration 참고).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hit", meta = (ClampMin = "0.0"))
	float RecoverBlendDuration = 0.2f;

	/**
	 * 이어붙이기 시간을 늘릴 때 기준이 되는 속도(cm/초). 오차 거리를 이 값으로 나눠 걸리는 시간을
	 * 정한다 — 서버·클라 물리가 크게 벌어졌을수록(비행 중 보정으로도 못 좁힌 경우) 더 길게 이어붙여서
	 * 순간이동처럼 보이지 않게 한다. 값이 작을수록 큰 오차에서 더 오래, 더 느리게 미끄러진다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hit", meta = (ClampMin = "1.0"))
	float RecoverBlendSpeed = 150.f;

	/**
	 * 오차가 아무리 커도 이어붙이기가 이 시간을 넘기지는 않는다.
	 *
	 * RagdollPoseBlendTime(래그돌 포즈 스냅샷이 기상 애니로 넘어가는 시간)보다 길면 안 된다.
	 * 뼈대 애니메이션은 이미 정상적으로 재생 중인데 메시 전체 위치만 계속 밀려 들어가면,
	 * 이미 서 있는 몸이 슬금슬금 미끄러지는 것처럼 겹쳐 보인다("잔상").
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hit", meta = (ClampMin = "0.0"))
	float RecoverBlendMaxDuration = 0.35f;

	/** 위 이어붙이기를 갱신하는 주기(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Hit", meta = (ClampMin = "0.01"))
	float RecoverBlendTickInterval = 0.02f;

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
	 * 몬타주가 끝나기 이만큼 전에 조작을 돌려주고, 동시에 몬타주를 블렌드 아웃시킨다(초).
	 *
	 * 기상 애니 끝자락은 이미 다 선 자세를 유지하는 구간이라, 끝까지 기다리면 화면상 서 있는데
	 * 키가 안 먹는 답답한 순간이 생긴다. 조금 일찍 풀어 이동 애니로 자연스럽게 넘어가게 한다.
	 *
	 * 몬타주 애셋의 Blend Out 시간과 같게 맞추는 것이 기준이다. 그러면 조작이 풀리는 순간부터
	 * 블렌드가 시작되어 몬타주 끝에 정확히 이동 애니로 넘어간다. 이 값이 더 짧으면 기상 포즈로
	 * 굳은 채 미끄러지는 구간이 생기고, 더 길면 애니 뒷부분이 그만큼 잘린다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hit|GetUp", meta = (ClampMin = "0.0"))
	float GetUpUnlockLeadTime = 0.25f;

	/**
	 * 기상 몬타주 재생 속도. 1 보다 크면 빨리 일어난다. 앞뒤 애니의 길이와 템포가 달라
	 * 각각 따로 둔다. 재생할 몬타주를 고를 때 짝지은 값이 함께 선택된다.
	 *
	 * 잠금 시간은 이 값이 반영된 실제 재생 시간으로 잰다. 그래서 여기를 올려도 몬타주가
	 * 먼저 끝나고 잠금만 남아 가만히 서 있는 구간이 생기지 않는다.
	 *
	 * 속도는 여기서만 조절하는 것을 권한다. 몬타주 애셋이나 애니 시퀀스의 Rate Scale 로도
	 * 바꿀 수 있지만, 조절 지점이 두 곳으로 갈리면 최종 속도가 두 값의 곱이 되어 헷갈린다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hit|GetUp", meta = (ClampMin = "0.01"))
	float GetUpFromBackPlayRate = 1.f;

	UPROPERTY(EditDefaultsOnly, Category = "Hit|GetUp", meta = (ClampMin = "0.01"))
	float GetUpFromFrontPlayRate = 1.f;

	/**
	 * 래그돌 스냅샷 포즈를 밑에 깔아 두는 시간(초). IsUsingRagdollPose 가 이 시간만큼 true 다.
	 * 기상 몬타주의 Blend In 시간보다 조금 길게 잡는다. 짧으면 블렌드가 끝나기 전에
	 * 밑 포즈가 Idle 로 바뀌면서 몸이 한 번 들썩인다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hit|GetUp", meta = (ClampMin = "0.0"))
	float RagdollPoseBlendTime = 0.35f;

protected:
	/**
	 * 차에 치이는 순간 충돌 지점에 스폰되는 이펙트. 비워두면 이 연출만 빠진다.
	 *
	 * 부딪히는 소리와 같은 자리에 낸다. 몸이 날아가도 부딪힌 자리는 그 자리다.
	 * 이펙트 정면은 차가 밀고 나가는 방향으로 맞춰진다. 크기와 지속 시간은 코드에서
	 * 손대지 않으므로 Niagara 시스템 애셋 쪽에서 잡는다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Effect")
	TObjectPtr<UNiagaraSystem> VehicleHitEffect;

protected:
	// 아래 사운드 셋은 전부 비워도 된다. 비어 있으면 그 소리만 나지 않고 나머지는 정상 동작한다.

	/**
	 * 차에 치이는 순간 부딪히는 소리. 충돌 지점에서 나고 몸을 따라가지 않는다.
	 * 넉백으로 몸이 멀어지면 같이 멀어지므로 짧게 잡는 게 좋다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Sound")
	TObjectPtr<USoundBase> VehicleHitSound;

	/** 맞은 사람이 지르는 비명. 머리 본에 붙어 날아가는 몸을 따라간다. */
	UPROPERTY(EditDefaultsOnly, Category = "Sound")
	TObjectPtr<USoundBase> VehicleHitVoiceSound;

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
	// 아래 7개는 비워두면 C++ 이 만든 기본 입력이 쓰인다. 채우면 그쪽이 우선한다.
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

	// 헬멧을 갈아 끼우는 키(H). 프로토타입 확인용 입력이라 기획서에 없는 키를 쓴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SwapHelmetAction;

	// 보급 박스에서 페인트 통을 꺼내는 키(F). (기획서 6-1)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> PickUpCanAction;

	// 페인트 통을 박스에 반납하는 키(G). (기획서 6-2)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> DropCanAction;

	// 근처 도구에 페인트를 붓는 키(R). (기획서 6-3)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ChargePaintAction;

	// 옷을 입고 벗는 키(J). 헬멧과 마찬가지로 프로토타입 확인용이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ToggleOutfitAction;

private:
	// 애셋 IMC 가 빠뜨린 키만 담는 보완 컨텍스트. 지정된 컨텍스트 위에 얹는다.
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> SupplementContext;

	// SoundAttenuation 이 비었을 때 C++ 이 만들어 쓰는 기본 감쇠. 한 번만 만든다.
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> RuntimeSoundAttenuation;

	// 래그돌을 풀 때 메시를 되돌릴 기준값. BP 에서 다르게 놨을 수 있어 BeginPlay 에서 읽는다.
	FTransform DefaultMeshRelativeTransform;

	// 카메라 붐을 되돌릴 기준값. 같은 이유로 BeginPlay 에서 읽는다.
	FTransform DefaultCameraBoomRelativeTransform;

	FName DefaultMeshCollisionProfile;

	bool bRagdollActive = false;

	// 스냅샷 포즈를 밑에 깔고 있는 구간. 각 머신이 자기 화면 기준으로 관리한다
	bool bUsingRagdollPose = false;

	FTimerHandle RagdollRestTimerHandle;

	// CheckRagdollRest 틱(RagdollRestCheckInterval)을 누적해서 RagdollCorrectionInterval마다
	// 위치 보정을 내보낸다. 서버에서만 의미가 있다.
	float RagdollCorrectionElapsed = 0.f;

	FTimerHandle GetUpTimerHandle;

	FTimerHandle RagdollPoseBlendTimerHandle;

	// 복귀 이어붙이기 진행 상태. 캡슐 기준 상대 트랜스폼으로 들고 있는다 (TickRecoverBlend 참고).
	FTransform RecoverBlendFromRelativeTransform;

	float RecoverBlendElapsed = 0.f;

	// 이번 이어붙이기에 실제로 쓸 시간. 오차 크기에 따라 StopRagdoll에서 매번 다시 계산해 넣는다.
	float RecoverBlendActiveDuration = 0.f;

	FTimerHandle RecoverBlendTimerHandle;
};
