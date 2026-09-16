// CWRestZone.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interfaces/Interface_Interactable.h"
#include "CWRestZone.generated.h"

class ACWRestZone;
class ACWPlayerCharacter;
class UBoxComponent;
class UCWTemperatureComponent;

/**
 * 프롬프트 표시 상태. (착석 기획서 10절)
 *
 * 판단은 각 머신이 자기 화면의 로컬 플레이어를 기준으로 한다. Occupant 가 이미
 * 리플리케이트되고 있어서 추가 통신이 필요 없다.
 */
UENUM(BlueprintType)
enum class ECWRestPromptState : uint8
{
	// 박스 밖이거나, 본인이 이미 앉아 있다.
	Hidden,

	// 자리가 비어 있다. "E - 앉기"
	Available,

	// 다른 사람이 앉아 있다. "사용 중"
	//
	// 아무것도 띄우지 않으면 왜 E 가 안 먹는지 화면에 설명되지 않는다.
	Occupied
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnRestZoneOccupantChangedSignature, ACWRestZone*, RestZone, ACWPlayerCharacter*, Occupant);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnRestPromptChangedSignature, ACWRestZone*, RestZone, ECWRestPromptState, PromptState);

/**
 * 작업차량 앞에 놓는 1인용 휴식구역. (체온 기획서 5절 + 착석 기획서)
 *
 * E 로 의자에 앉아야 체온이 내려간다. 존 안에 서 있기만 해서는 존 밖과 다를 것이 없다.
 * 실제 하락은 UCWTemperatureComponent 가 하고, 이 액터는 "지금 누가 앉아 있는가" 만 정한다.
 *
 * 점유 규칙 - 선착순
 *   먼저 앉은 사람이 스스로 일어날 때까지 자리를 유지하며 뺏기지 않는다.
 *
 *   이전에는 겹침으로 점유자를 정하고, 자리가 비면 안에 남은 사람 중 체온이 가장 높은
 *   사람에게 넘겼다. 착석에서는 자리를 넘기려면 다음 사람이 직접 E 를 눌러야 하므로
 *   자동 승계라는 개념 자체가 성립하지 않는다.
 *
 *   점유가 겹침이 아니라 "클레임" 이 되면서 챙길 예외도 앉은 사람 하나로 줄었다.
 *   후보 목록을 주기적으로 훑을 필요가 없어서 Tick 이 통째로 사라졌다.
 *
 * 안전지대가 아니다
 *   다른 플레이어가 패링으로 차량을 틀어 이쪽으로 날려보낼 수 있다. 앉아 있는 사람도
 *   차에 맞고, 맞는 즉시 자리를 반납한 뒤 래그돌로 넘어간다. 그 순서는 캐릭터가 지킨다.
 *
 *   쓰러진 몸의 위치를 다시 재서 자리를 유지할지 판단하던 예전 로직은 필요 없어졌다.
 *   겹침이 끊기는 것과 자리를 잃는 것이 더 이상 같은 일이 아니기 때문이다.
 *
 * Area 박스의 역할
 *   회복 판정이 아니라 "E 로 앉기" 프롬프트를 띄우는 범위다. 막지 않고 겹침만 본다.
 *   물리적으로 좁히는 것은 레벨에 놓는 차량과 구조물이 한다.
 *
 * 네트워크
 *   앉히고 일으키는 판정은 서버만 한다. 결과인 Occupant 만 리플리케이트해서, 바닥 데칼처럼
 *   "지금 누가 쓰는 중인지" 를 남의 화면에서도 보여줄 수 있게 한다. 기다리는 사람도
 *   그 정보를 봐야 순서를 조율한다.
 */
UCLASS()
class CROSSWALK_API ACWRestZone : public AActor, public IInterface_Interactable
{
	GENERATED_BODY()

public:
	ACWRestZone();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 디버그 표시 전용. 평소에는 BeginPlay 에서 꺼 둔다. */
	virtual void Tick(float DeltaSeconds) override;

	// E 를 눌렀을 때. 서버에서만 들어온다. 승인되면 앉힌다.
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;

	// 앉을 수 있는 상태인가. 서버가 다시 판정한다.
	virtual bool CanInteract_Implementation(APawn* InstigatorPawn) const override;

	// 거리는 의자 기준으로 잰다. 액터 원점을 쓰면 파라솔·선풍기까지 포함한 구역 중심이라,
	// 의자에서 멀리 떨어져 있어도 E 가 먹는다. (작업차량이 트렁크 위치를 쓰는 것과 같은 이유)
	virtual FVector GetInteractionLocation_Implementation() const override;

	UFUNCTION(BlueprintPure, Category = "Rest")
	ACWPlayerCharacter* GetOccupant() const { return Occupant; }

	/** 지금 이 존을 쓰는 사람이 있는가. 바닥 데칼의 비어있음/사용중 표시에 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Rest")
	bool IsOccupied() const { return Occupant != nullptr; }

	/** 내 화면에 지금 무엇을 띄워야 하는가. 위젯이 이 값을 읽는다. */
	UFUNCTION(BlueprintPure, Category = "Rest")
	ECWRestPromptState GetPromptState() const { return PromptState; }

	/**
	 * 앉을 자리의 월드 트랜스폼. 캡슐 원점이 놓일 지점과 바라볼 방향이다.
	 *
	 * Seat 컴포넌트를 BP 에서 의자 위로 옮겨 두면 된다. 코드는 위치를 모른다.
	 */
	UFUNCTION(BlueprintPure, Category = "Rest")
	FTransform GetSeatTransform() const;

	/**
	 * 자리를 비운다. 서버에서만 유효하다.
	 *
	 * 캐릭터가 일어날 때(스스로든, 차에 맞아서든, 소멸하면서든) 여기로 들어온다.
	 * 앉히는 입구가 Interact 하나이듯, 자리를 비우는 입구도 여기 하나다.
	 */
	void ReleaseOccupant(ACWPlayerCharacter* Player);

protected:
	/** 점유자가 바뀐 직후. BP 자식에서 데칼/파티클/사운드를 붙이는 자리. 모든 머신에서 호출된다. */
	UFUNCTION(BlueprintNativeEvent, Category = "Rest")
	void OnOccupantChanged(ACWPlayerCharacter* NewOccupant);
	virtual void OnOccupantChanged_Implementation(ACWPlayerCharacter* NewOccupant) {}

	/** 내 화면의 프롬프트 표시가 바뀐 직후. BP 에서 위젯을 붙이는 자리다. */
	UFUNCTION(BlueprintNativeEvent, Category = "Rest")
	void OnPromptStateChanged(ECWRestPromptState NewPromptState);
	virtual void OnPromptStateChanged_Implementation(ECWRestPromptState NewPromptState) {}

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	void SetOccupant(ACWPlayerCharacter* NewOccupant);

	/** 점유 상태를 체온 컴포넌트에 반영한다. 서버에서만 유효하다. */
	void ApplyRestingState(ACWPlayerCharacter* Player, bool bNewResting);

	/** 프롬프트 상태를 다시 계산한다. 바뀐 경우에만 알린다. */
	void RefreshPromptState();

	/** 점유 상태를 화면에 띄운다. 연출이 붙기 전까지 프로토타입 확인용. */
	void ShowDebugOccupant() const;

	UFUNCTION()
	void OnRep_Occupant();

	static UCWTemperatureComponent* FindTemperatureComponent(ACWPlayerCharacter* Player);

	/** 디버그 표시용 플레이어 이름. 머신마다 달라지는 액터 이름 대신 PlayerState 이름을 쓴다. */
	static FString ResolvePlayerLabel(const APawn* Player);

public:
	// 프롬프트 표시 영역. 루트 컴포넌트다. 막는 용도가 아니라 겹침만 본다.
	// 크기는 레벨에서 조절하되 SeatInteractRadius 를 넘기지 않는다.
	// 넘기면 "앉기" 가 떠 있는데 E 는 안 먹는 구간이 생긴다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rest")
	TObjectPtr<UBoxComponent> Area;

	/**
	 * 앉는 자리. BP 에서 이 컴포넌트를 의자 위로 옮기고 방향을 맞춘다.
	 *
	 * 스태틱메시의 소켓이 아니라 별도 씬 컴포넌트를 쓰는 이유: 소켓은 메시 애셋을 열어
	 * 편집해야 하고, 의자 메시를 교체하면 다시 만들어야 한다. 이쪽은 BP 뷰포트에서
	 * 바로 끌어 옮길 수 있고 메시가 바뀌어도 그대로 남는다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rest")
	TObjectPtr<USceneComponent> Seat;

protected:
	// 의자에서 E 가 먹는 거리(cm). 캐릭터의 InteractRadius 와 별개로, 이 존이 스스로 재는 값이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rest", meta = (ClampMin = "0.0"))
	float SeatInteractRadius = 200.f;

	// 서버가 다시 판정할 때 주는 거리 여유 배율.
	// 클라이언트와 서버의 위치가 핑만큼 어긋나 있어서, 딱 맞게 재면 눌러도 안 앉는 일이 생긴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rest", meta = (ClampMin = "1.0"))
	float ServerRangeTolerance = 1.5f;

	// 프롬프트 영역을 플레이 중에도 보이게 한다. 위젯이 붙기 전까지 범위 확인용.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rest|Debug")
	bool bShowDebugArea = false;

	// 점유자를 화면에 띄운다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rest|Debug")
	bool bShowDebugOccupant = false;

private:
	// 지금 앉아 있는 1명. 서버가 정하고 전원에게 전파된다.
	UPROPERTY(ReplicatedUsing = OnRep_Occupant)
	TObjectPtr<ACWPlayerCharacter> Occupant;

	// 내 화면의 로컬 플레이어가 프롬프트 영역 안에 있는가. 머신마다 따로 굴러간다.
	bool bLocalPlayerInside = false;

	// 지금 내 화면에 띄우고 있는 것. 바뀐 순간에만 알리려고 들고 있는다.
	ECWRestPromptState PromptState = ECWRestPromptState::Hidden;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Rest")
	FCWOnRestZoneOccupantChangedSignature OnOccupantChangedDelegate;

	UPROPERTY(BlueprintAssignable, Category = "Rest")
	FCWOnRestPromptChangedSignature OnPromptChangedDelegate;
};
