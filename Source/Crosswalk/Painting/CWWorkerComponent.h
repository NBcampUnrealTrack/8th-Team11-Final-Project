#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Enums/CWToolTypes.h"
#include "CWWorkerComponent.generated.h"

class ACWBaseWorkZone;
class UCWWorkerComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnTargetZoneChangedSignature, UCWWorkerComponent*, WorkerComponent, ACWBaseWorkZone*, TargetZone);

/**
 * 플레이어 캐릭터에 붙이는 작업 입력 컴포넌트.
 *
 * 하는 일은 단순하다.
 *   작업 키를 누르고 있는 동안, 매 프레임 커서가 가리키는 존을 찾아서 ApplyWork 를 호출한다.
 * 존이 Hold 방식인지 칸 방식인지는 이 컴포넌트가 신경 쓰지 않는다. 존이 알아서 처리한다.
 *
 * 들고 있는 도구는 UCWToolHolderComponent 가 SetHeldTool 로 넣어준다.
 * 홀더가 없어도 SetHeldTool 을 직접 불러 테스트할 수 있게 일부러 분리해 뒀다.
 *
 * 네트워크 — 이 컴포넌트가 작업 관련 RPC 의 유일한 창구다.
 *
 * 존에 직접 Server RPC 를 보낼 수 없기 때문이다. Server RPC 는 보내는 클라이언트가 소유한
 * 액터에서만 나가는데, 레벨에 배치된 존은 소유자가 없어서 RPC 가 조용히 버려진다.
 * 그래서 폰에 붙은 이 컴포넌트가 요청을 받아 서버에서 존으로 넘긴다.
 *
 * 또 하나. 서버는 클라이언트의 마우스 커서를 읽을 수 없다. 커서 트레이스는 로컬 전용이라
 * 작업 지점은 소유 클라이언트가 주기적으로 올려보내야 한다. (칸을 칠하는 존에서 필요)
 */
UCLASS(ClassGroup = (Painting), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWWorkerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWWorkerComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 작업 키를 눌렀을 때. Enhanced Input 의 Started 에 연결한다. */
	UFUNCTION(BlueprintCallable, Category = "Work")
	void StartWork();

	/** 작업 키를 뗐을 때. Completed / Canceled 에 연결한다. */
	UFUNCTION(BlueprintCallable, Category = "Work")
	void StopWork();

	/** 도구를 집거나 놓을 때 호출한다. 놓을 때는 ECWToolType::None. */
	UFUNCTION(BlueprintCallable, Category = "Work")
	void SetHeldTool(ECWToolType NewTool);

	/**
	 * 이동 입력을 현재 대상 존에 넘긴다. 미니게임이 있는 존이면 방향 전환에 쓰인다.
	 * 캐릭터의 이동 처리보다 먼저 호출하고, true 가 돌아오면 그 프레임 이동을 건너뛴다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Work")
	bool ConsumeDirectionInput(const FVector2D& MoveInput);

	/**
	 * 작업 취소 키. 미니게임을 빠져나와 다시 움직일 수 있게 한다.
	 *
	 * 서버 확정을 기다리면 그 사이 이동이 막혀 차를 못 피한다.
	 * 그래서 이동 잠금만 로컬에서 즉시 풀고, 존 상태 정리는 서버에 맡긴다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Work")
	void CancelWork();

	/** 차량에 치여 다운되면 true, 일어나면 false. 다운 중에는 작업이 진행되지 않는다. */
	UFUNCTION(BlueprintCallable, Category = "Work")
	void SetWorkBlocked(bool bNewBlocked);

	UFUNCTION(BlueprintPure, Category = "Work")
	ECWToolType GetHeldTool() const { return HeldTool; }

	UFUNCTION(BlueprintPure, Category = "Work")
	ACWBaseWorkZone* GetTargetZone() const { return TargetZone; }

	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsWorking() const { return bWorking; }

	/** 다운 등으로 작업이 막혀 있는가. 미니게임 존이 중단 판단에 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsWorkBlocked() const { return bWorkBlocked; }

	/**
	 * 이 컴포넌트가 이 기계에서 조종 중인 플레이어의 것인가.
	 * 로컬로 시뮬레이션하는 존이 "내가 돌려야 할 작업인지" 판단하는 데 쓴다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsLocalWorker() const;

	/**
	 * 로컬에서 계산한 진행도를 서버에 올린다. 테두리 미니게임처럼
	 * 진행 자체는 본인 기계에서 돌고 결과만 공유하면 되는 존이 쓴다.
	 */
	void ReportWorkProgress(ACWBaseWorkZone* Zone, float NewProgress);

	/**
	 * 로컬에서 확정한 붓질 하나를 서버로 올린다. 칸을 칠하는 존이 매 붓질마다 부른다.
	 *
	 * 서버가 ReceivedWorkLocation 으로 붓질을 재현하지 않고 이걸 쓰는 이유는
	 * ACWBaseWorkZone::ReceiveBrushStroke 주석에 적어 뒀다.
	 * 존이 "바뀐 칸이 있을 때"만 부르므로 붓이 멈춰 있으면 패킷도 나가지 않는다.
	 */
	void SubmitBrushStroke(ACWBaseWorkZone* Zone, const FVector& WorldLocation, float Radius, bool bFilled, bool bSpillOnly);

private:
	/** 작업 키를 눌렀는지 서버에 알린다. 진행도는 서버만 올릴 수 있다. */
	UFUNCTION(Server, Reliable)
	void Server_SetWorking(bool bNewWorking, FVector_NetQuantize WorkLocation);

	/**
	 * 작업 중 커서 지점을 서버에 올린다. 매 프레임 보내면 낭비라 WorkLocationSendInterval 로 묶는다.
	 * 한두 패킷 유실돼도 다음 것이 덮으므로 Unreliable 로 둔다.
	 */
	UFUNCTION(Server, Unreliable)
	void Server_UpdateWorkLocation(FVector_NetQuantize WorkLocation);

	/** 미니게임 방향 전환 요청. 어느 존인지 같이 보내야 서버가 대상을 특정할 수 있다. */
	UFUNCTION(Server, Reliable)
	void Server_SubmitDirectionInput(ACWBaseWorkZone* Zone, FVector2D MoveInput);

	/**
	 * 로컬에서 계산한 진행도 보고.
	 * 중간 값은 몇 개 유실돼도 다음 것이 덮으므로 Unreliable 로 둔다.
	 */
	UFUNCTION(Server, Unreliable)
	void Server_ReportWorkProgress(ACWBaseWorkZone* Zone, float NewProgress);

	/** 완주 보고. 이건 놓치면 작업이 영영 안 끝나므로 Reliable 이어야 한다. */
	UFUNCTION(Server, Reliable)
	void Server_ReportWorkCompleted(ACWBaseWorkZone* Zone);

	/**
	 * 붓질 보고. 놓치면 그 자리만 영영 안 칠해지거나 안 지워지므로 Reliable 이어야 한다.
	 * 칸 상태가 실제로 바뀔 때만 나가서, 붓을 대고 가만히 있으면 패킷이 나가지 않는다.
	 */
	UFUNCTION(Server, Reliable)
	void Server_SubmitBrushStroke(ACWBaseWorkZone* Zone, FVector_NetQuantize WorldLocation, float Radius, bool bFilled, bool bSpillOnly);

	/**
	 * 지금 작업할 수 있는 존과 작업 지점을 찾는다.
	 *
	 * 존은 캐릭터 위치에서 가장 가까운 것을 고르고, 작업 지점은 커서가 가리키는 곳으로 잡는다.
	 * (커서가 허공이면 캐릭터 위치를 쓴다)
	 *
	 * 커서를 읽을 수 있는 것은 소유 클라이언트뿐이므로, 서버에서는 마지막으로 올라온
	 * ReceivedWorkLocation 을 대신 쓴다.
	 */
	ACWBaseWorkZone* FindTargetZone(FVector& OutWorkLocation) const;

	void SetTargetZone(ACWBaseWorkZone* NewZone);

	bool IsLocallyControlledWorker() const;

public:
	// 존이 캐릭터에서 이 거리보다 멀면 작업할 수 없다. (cm)
	// 존마다 지정된 WorkRange 와 비교해서 더 짧은 쪽이 적용된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	float MaxWorkDistance = 300.f;

protected:
	// 작업 지점을 찾을 커서 트레이스 채널. 칸을 칠하는 존에서만 의미가 있다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	TEnumAsByte<ECollisionChannel> WorkTraceChannel = ECC_Visibility;

	// 작업 지점을 서버에 올리는 간격(초). 0.1 이면 초당 10번이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "0.02"))
	float WorkLocationSendInterval = 0.1f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Work")
	ECWToolType HeldTool = ECWToolType::None;

	// 소유 클라이언트는 반응성을 위해 즉시 세팅하고, 서버 값이 오면 덮인다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Work")
	bool bWorking = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Work")
	bool bWorkBlocked = false;

private:
	// 대상 존은 HUD 표시와 서버 판정에 각각 필요해서 양쪽이 로컬로 계산한다.
	UPROPERTY(Transient)
	TObjectPtr<ACWBaseWorkZone> TargetZone;

	// 서버가 마지막으로 받은 작업 지점. 서버에서만 의미가 있다.
	FVector ReceivedWorkLocation = FVector::ZeroVector;

	// 작업 지점을 마지막으로 올려보낸 시각. 소유 클라이언트에서만 쓴다.
	float LastWorkLocationSendTime = -1000.f;

	// 취소를 눌렀는가. 서버 응답을 기다리지 않고 이동 잠금을 즉시 풀기 위한 로컬 플래그다.
	// 리플리케이트하지 않는다. 작업을 다시 시작할 때 풀린다.
	bool bCancelRequested = false;

public:
	UPROPERTY(BlueprintAssignable, Category = "Work")
	FCWOnTargetZoneChangedSignature OnTargetZoneChanged;
};
