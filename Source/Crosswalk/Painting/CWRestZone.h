// CWRestZone.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CWRestZone.generated.h"

class ACWRestZone;
class ACWPlayerCharacter;
class UBoxComponent;
class UCWTemperatureComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnRestZoneOccupantChangedSignature, ACWRestZone*, RestZone, ACWPlayerCharacter*, Occupant);

/**
 * 작업차량 앞에 놓는 1인용 휴식구역. (체온 기획서 5절)
 *
 * 들어오면 상호작용 키 없이 바로 체온이 빠르게 내려간다. 실제 하락은
 * UCWTemperatureComponent 가 하고, 이 액터는 "지금 누구 차례인가" 만 정한다.
 *
 * 1인 제한을 두 겹으로 거는 이유
 *   공간 자체를 좁게 만드는 것(레벨의 콜리전 지오메트리)이 1차이고, 이 액터의 점유자
 *   판정이 2차다. 좁게만 만들면 두 캡슐이 반씩 걸쳤을 때 누가 쉬는 중인지가 애매해지고,
 *   판정만 두면 왜 못 쉬는지가 화면에 보이지 않는다. 둘 다 있어야 한다.
 *
 *   그래서 이 액터의 박스는 막는 용도가 아니라 감지 전용(Overlap)이다.
 *   물리적으로 좁히는 것은 레벨에 놓는 차량과 구조물이 한다.
 *
 * 점유 규칙
 *   점유자는 스스로 나가기 전까지 유지된다. 자리가 비면 안에 남아 있는 사람 중
 *   체온이 가장 높은 사람에게 넘어간다. 선착순이 아니라 체온순인 이유는,
 *   급한 사람이 먼저 쉬는 쪽이 조율의 결과로 자연스럽기 때문이다.
 *
 *   차에 치여 쓰러진 사람도 점유자가 될 수 있다. 다운 시간이 짧아서(1초 + 기상 모션)
 *   자리를 오래 붙잡지 못하고, 여기서 걸러내면 "쓰러졌다는 이유로 회복도 못 하는" 이중 처벌이 된다.
 *   패링으로 차를 틀어 점유자를 날려버리면 존 밖으로 굴러 나가면서 자리가 자연히 넘어간다.
 *
 * 네트워크
 *   점유자 판정은 서버만 한다. 결과인 Occupant 만 리플리케이트해서, 바닥 데칼처럼
 *   "지금 누가 쓰는 중인지" 를 남의 화면에서도 보여줄 수 있게 한다. 대기 순서를 조율하려면
 *   기다리는 사람도 그 정보를 봐야 한다.
 */
UCLASS()
class CROSSWALK_API ACWRestZone : public AActor
{
	GENERATED_BODY()

public:
	ACWRestZone();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void BeginPlay() override;

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Rest")
	ACWPlayerCharacter* GetOccupant() const { return Occupant; }

	/** 지금 이 존을 쓰는 사람이 있는가. 바닥 데칼의 비어있음/사용중 표시에 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Rest")
	bool IsOccupied() const { return Occupant != nullptr; }

	/** 존 안에 있지만 아직 차례가 아닌 사람 수. 대기 인원 표시에 쓴다. 서버에서만 정확하다. */
	UFUNCTION(BlueprintPure, Category = "Rest")
	int32 GetWaitingCount() const;

protected:
	/** 점유자가 바뀐 직후. BP 자식에서 데칼/파티클/사운드를 붙이는 자리. 모든 머신에서 호출된다. */
	UFUNCTION(BlueprintNativeEvent, Category = "Rest")
	void OnOccupantChanged(ACWPlayerCharacter* NewOccupant);
	virtual void OnOccupantChanged_Implementation(ACWPlayerCharacter* NewOccupant) {}

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	/** 점유자를 다시 고른다. 서버에서만 유효하다. 이미 유효한 점유자가 있으면 그대로 둔다. */
	void RefreshOccupant();

	/**
	 * 쓰러져 있는 사람을 위치로 다시 판정한다. 서버에서만 유효하다.
	 *
	 * 래그돌이 시작되면 캡슐 콜리전이 꺼져서 겹침이 끊긴다. 그대로 두면 존 안에 쓰러져 있는데도
	 * 자리를 잃는다. 그래서 쓰러진 사람은 겹침 이벤트 대신 이 판정으로 관리한다.
	 */
	void RefreshDownedCandidates();

	/** 이 지점이 감지 박스 안인가. */
	bool IsPointInsideArea(const FVector& Point) const;

	/**
	 * 겹침 판정에 쓸 기준 위치.
	 *
	 * 래그돌 중에는 캡슐이 치인 자리에 그대로 남아 있어서(카메라만 몸을 따라간다)
	 * 액터 위치를 쓰면 몸이 도로 건너편에 날아가 있어도 존 안으로 잡힌다. 몸 위치를 써야 한다.
	 */
	static FVector ResolveProbeLocation(const ACWPlayerCharacter* Player);

	/** 지금 차에 치여 쓰러져 있는가. */
	static bool IsPlayerDowned(const ACWPlayerCharacter* Player);

	/** 안에 남아 있는 사람 중 체온이 가장 높은 사람. 없으면 nullptr. */
	ACWPlayerCharacter* FindHottestCandidate() const;

	/** 파괴되거나 접속이 끊긴 후보를 목록에서 걷어낸다. */
	void PruneCandidates();

	void SetOccupant(ACWPlayerCharacter* NewOccupant);

	/** 점유 상태를 화면에 띄운다. 연출이 붙기 전까지 프로토타입 확인용. */
	void ShowDebugOccupant() const;

	/** 점유 상태를 체온 컴포넌트에 반영한다. 서버에서만 유효하다. */
	void ApplyRestingState(ACWPlayerCharacter* Player, bool bNewResting);

	UFUNCTION()
	void OnRep_Occupant();

	static UCWTemperatureComponent* FindTemperatureComponent(ACWPlayerCharacter* Player);

	/** 디버그 표시용 플레이어 이름. 머신마다 달라지는 액터 이름 대신 PlayerState 이름을 쓴다. */
	static FString ResolvePlayerLabel(const APawn* Player);

public:
	// 감지 영역. 루트 컴포넌트다. 막는 용도가 아니라 겹침만 본다.
	// 기본 크기는 캡슐 하나가 겨우 들어가는 정도로 잡아 뒀다. 레벨에서 조절한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rest")
	TObjectPtr<UBoxComponent> Area;

protected:
	// 후보 목록을 정리하는 주기(초).
	//
	// 나가는 것은 EndOverlap 이 알려주지만, 접속이 끊기거나 액터가 파괴되는 경우는
	// 알려주지 않는다. 그대로 두면 빈 자리가 영영 안 열린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rest", meta = (ClampMin = "0.05"))
	float CandidateCheckInterval = 0.25f;

	// 감지 박스를 플레이 중에도 보이게 한다. 바닥 데칼이 생기기 전까지 위치 확인용.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rest|Debug")
	bool bShowDebugArea = false;

	// 점유자와 대기 인원을 화면에 띄운다. 서버에서만 대기 인원이 정확하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rest|Debug")
	bool bShowDebugOccupant = false;

private:
	// 지금 회복 중인 1명. 서버가 정하고 전원에게 전파된다.
	UPROPERTY(ReplicatedUsing = OnRep_Occupant)
	TObjectPtr<ACWPlayerCharacter> Occupant;

	// 존 안에 들어와 있는 사람 전부. 점유자도 여기 포함된다. 서버에서만 채운다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACWPlayerCharacter>> Candidates;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Rest")
	FCWOnRestZoneOccupantChangedSignature OnOccupantChangedDelegate;
};
