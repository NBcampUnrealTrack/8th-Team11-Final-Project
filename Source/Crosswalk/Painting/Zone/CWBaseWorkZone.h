#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/Enums/CWToolTypes.h"
#include "CWBaseWorkZone.generated.h"

class ACWBaseWorkZone;
class UBoxComponent;
class UCWWorkerComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnWorkProgressSignature, ACWBaseWorkZone*, Zone, float, Progress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCWOnWorkCompletedSignature, ACWBaseWorkZone*, Zone);

/**
 * 작업 단계 하나를 담당하는 액터의 공통 부모.
 *
 * 이 클래스가 하는 일은 딱 3개다.
 *   1. 진행도(0~1)를 들고 있는다
 *   2. 진행도가 1이 되면 완료 처리하고 알린다
 *   3. 선행 단계가 안 끝났으면 잠가둔다 (3단계는 2단계 완료 후)
 *
 * "어떻게 진행도를 올리는지"는 자식 클래스가 ApplyWork 를 오버라이드해서 정한다.
 */
UCLASS(Abstract)
class CROSSWALK_API ACWBaseWorkZone : public AActor
{
	GENERATED_BODY()

public:
	ACWBaseWorkZone();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void BeginPlay() override;

	/**
	 * 플레이어가 작업 키를 누르고 있는 동안 매 프레임 호출된다.
	 *
	 * 도구 종류만 받지 않고 작업자 컴포넌트를 받는 이유:
	 * 미니게임이 있는 존은 "누가" 작업 중인지 알아야 그 플레이어 화면에만 UI 를 띄울 수 있다.
	 *
	 * @param Worker        작업 중인 플레이어의 컴포넌트. 도구는 Worker->GetHeldTool() 로 얻는다.
	 * @param WorldLocation 커서가 가리키는 도로 위 지점 (커서가 허공이면 캐릭터 위치)
	 * @param DeltaSeconds  이번 프레임 시간
	 */
	virtual void ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds);

	/**
	 * 이동 입력(WASD)을 이 존이 가져다 쓰는가.
	 *
	 * 미니게임이 있는 존만 오버라이드한다. true 를 돌려주면 캐릭터는 그 프레임에 움직이지 않는다.
	 * @return 입력을 소비했으면 true
	 */
	virtual bool ConsumeDirectionInput(const FVector2D& MoveInput) { return false; }

	/**
	 * 플레이어가 작업을 그만두겠다고 알렸을 때. (취소 키)
	 *
	 * 미니게임처럼 입력을 붙잡고 있는 존만 오버라이드하면 된다.
	 * 게이지나 칸 채우기 존은 작업 키를 떼는 것으로 충분해서 할 일이 없다.
	 */
	virtual void CancelWork() {}

	/**
	 * 로컬에서 진행하는 존이 올려보낸 진행도를 서버가 받는다.
	 *
	 * 되돌아가지 않게 최대값만 취한다. 패킷이 순서 없이 도착해도 진행도가 튀지 않는다.
	 * 프로토타입이라 값 자체는 검증하지 않는다. 협동 PvE 이고 서로 속일 이유가 없다.
	 */
	void ReceiveReportedProgress(float NewProgress);

	/**
	 * 소유 클라이언트가 자기 기계에서 확정한 붓질 하나를 서버가 받는다.
	 *
	 * 서버가 커서 좌표를 샘플링해서 붓질을 재현하면 안 되기 때문이다.
	 * 좌표는 초당 10번밖에 올라오지 않는데 클라이언트는 매 프레임 칠한다. 그 차이만큼
	 * 서버에 안 칠해진 구멍이 남고, 되돌릴 경로가 없어서 화면이 영영 어긋난다.
	 * 그래서 "어디를 칠했다"를 클라이언트가 직접 올려보내고 서버는 그대로 적용만 한다.
	 *
	 * 칸을 칠하는 존만 오버라이드한다. 게이지 존은 좌표가 필요 없다.
	 */
	virtual void ReceiveBrushStroke(const FVector& WorldLocation, float Radius, bool bFilled, bool bSpillOnly) {}

	/** 이 도구로 이 존을 작업할 수 있는가. AcceptedTools 가 비어 있으면 도구 종류는 안 가린다. (맨손은 언제나 불가) */
	UFUNCTION(BlueprintPure, Category = "Work")
	virtual bool CanUseTool(ECWToolType ToolType) const;

	/**
	 * 지금 작업 입력을 받아줄 상태인가. (잠기지 않았고, 완료되지 않았고, 도구가 맞음)
	 *
	 * virtual 인 이유: 완료된 뒤에도 계속 받아야 하는 작업이 있다.
	 * 도색이 끝난 존에도 삐져나간 페인트는 남아 있어서 검은색 롤러는 계속 들어와야 한다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	virtual bool CanWorkNow(ECWToolType ToolType) const;

	UFUNCTION(BlueprintPure, Category = "Work")
	float GetProgress() const { return Progress; }

	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsCompleted() const { return bCompleted; }

	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsLocked() const { return bLocked; }

	UFUNCTION(BlueprintPure, Category = "Work")
	FText GetObjectiveName() const { return ObjectiveName; }

	UFUNCTION(BlueprintPure, Category = "Work")
	int32 GetWorkOrder() const { return WorkOrder; }

	UFUNCTION(BlueprintPure, Category = "Work")
	FName GetWorkGroup() const { return WorkGroup; }

	/**
	 * 어떤 지점에서 이 존까지의 거리(제곱).
	 *
	 * 액터 원점까지 재지 않고 Area 박스까지 재는 이유:
	 * 존은 횡단보도처럼 길게 뻗은 영역이라, 원점 기준으로 재면 끝쪽에 서 있을 때
	 * 존 안에 있는데도 "너무 멀다" 로 판정된다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	float GetSquaredDistanceToPoint(const FVector& Point) const;

	float GetWorkRange() const { return WorkRange; }

protected:
	/**
	 * 진행도를 설정한다. 완료 판정까지 여기서 같이 한다.
	 * 서버에서만 유효하다. 클라이언트는 리플리케이션으로 값을 받는다.
	 */
	void SetProgress(float NewProgress);

	/**
	 * 시작 시점 진행도를 델리게이트 없이 조용히 세팅한다.
	 * BeginPlay 순서 때문에 아직 아무도 델리게이트를 바인딩하지 않았을 수 있어서 따로 둔다.
	 */
	void InitializeProgress(float InProgress);

	/** 진행도가 바뀔 때마다 호출. 자식 클래스에서 메쉬/머티리얼을 갱신하는 자리. */
	virtual void OnProgressChanged(float NewProgress);

private:
	/** 같은 WorkGroup 안에서 나보다 WorkOrder 가 작은 존을 모아 잠금 감시 대상으로 등록한다. */
	void CollectWatchedZones();

	/** 감시 중인 존이 전부 끝났는지 다시 보고 잠금을 갱신한다. 서버에서만 유효하다. */
	void RefreshLock();

	/** 지금 잠겨 있어야 하는가. 앞 순서 존이 하나라도 안 끝났으면 true. */
	bool ShouldBeLocked() const;

	/** 진행도를 화면에 띄운다. HUD 가 붙기 전까지 프로토타입 확인용. */
	void ShowDebugProgress() const;

	/** 진행도 변화에 딸린 연출과 델리게이트. 서버와 클라이언트가 같은 경로를 탄다. */
	void HandleProgressChanged();

	UFUNCTION()
	void OnRep_Progress();

	UFUNCTION()
	void OnRep_Completed();

	/**
	 * 앞 순서 존의 진행도가 바뀔 때마다 잠금을 다시 계산한다.
	 *
	 * OnWorkCompleted 가 아니라 OnWorkProgress 에 붙는 이유:
	 * 완료는 풀릴 수도 있다. 차량이 페인트를 또 밟으면 끝났던 존이 다시 미완료가 되고,
	 * 그러면 뒤 단계는 다시 잠겨야 한다. 완료 델리게이트만 봐서는 그 순간을 잡을 수 없다.
	 */
	UFUNCTION()
	void HandleWatchedZoneProgress(ACWBaseWorkZone* Zone, float Progress);

public:
	// 작업 영역. 루트 컴포넌트다.
	// 캐릭터가 이 박스 근처에 있으면 작업 대상이 되고, 커서 트레이스도 이 박스가 받는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UBoxComponent> Area;

	// HUD 체크리스트에 표시될 이름. 예) "도로 청소"
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	FText ObjectiveName;

protected:
	// 이 존에서 쓸 수 있는 도구 목록. 비워두면 도구 종류를 가리지 않는다. (단, 맨손은 여전히 불가)
	// 2단계 테두리 존은 Liner 하나만 넣어 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	TArray<ECWToolType> AcceptedTools;

	// 작업 순서. 값이 작을수록 먼저 한다.
	//
	// 같은 WorkGroup 안에서 이 값보다 작은 존이 하나라도 안 끝났으면 잠긴다.
	// 존을 서로 겹쳐 놓아도 순서대로만 열리므로, 겹친 위치에서 엉뚱한 단계가 잡히지 않는다.
	// 기획서 3.2 기준으로 1 = 도로 청소, 2 = 가이드라인 스케치, 3 = 도색 이다.
	//
	// 순서를 바꾸고 싶으면 이 숫자만 고치면 된다. 존끼리 서로를 가리키지 않으므로
	// 중간에 단계를 하나 끼워 넣어도 나머지를 다시 연결할 필요가 없다.
	// 같은 값을 준 존들은 동시에 열린다. 0 이면 아무것도 기다리지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Order")
	int32 WorkOrder = 0;

	// 순서를 함께 따지는 존 묶음. 횡단보도가 여러 개면 각각 다른 이름을 준다.
	// 비워두면 레벨의 모든 존이 한 묶음이다. (프로토타입 맵은 횡단보도가 하나라 비워도 된다)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Order")
	FName WorkGroup;

	// 순서와 별개로 반드시 먼저 끝나야 하는 존. WorkOrder 로 표현하기 어려운
	// 예외적인 의존이 생겼을 때만 쓴다. 보통은 비워두고 WorkOrder 만 쓰면 된다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Work|Order")
	TObjectPtr<ACWBaseWorkZone> PrerequisiteZone;

	// 캐릭터가 Area 박스에서 이 거리보다 멀면 작업할 수 없다. (cm)
	// 박스 표면 기준이라 150 정도면 "존 옆에 붙어 서 있다" 는 감각과 맞는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	float WorkRange = 150.f;

	// 진행도가 이 값을 넘으면 완료 처리하고, 그 뒤로는 작업 입력을 받지 않는다.
	//
	// 기본값이 1 보다 낮은 이유는 칸 채우기 존 때문이다. 격자 구석 칸까지 전부 칠하기가
	// 까다로워서 살짝 여유를 준다. 게이지가 차오르는 존(Hold)은 100% 까지 채워야 자연스러우므로
	// 자식 생성자에서 1.0 으로 올린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float CompleteThreshold = 0.98f;

	// 아래 3개는 서버가 정하고 전원에게 전파된다. 그래서 진행도와 완료가 팀 전체에서 하나다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Progress, Category = "Work")
	float Progress = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Completed, Category = "Work")
	bool bCompleted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Work")
	bool bLocked = false;

	// 진행도를 화면 좌상단에 띄운다. HUD 가 생기면 끄면 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Debug")
	bool bShowDebugProgress = true;

private:
	// 잠금을 풀기 위해 끝나기를 기다리는 존들. 서버에서만 채운다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACWBaseWorkZone>> WatchedZones;

public:
	UPROPERTY(BlueprintAssignable, Category = "Work")
	FCWOnWorkProgressSignature OnWorkProgress;

	UPROPERTY(BlueprintAssignable, Category = "Work")
	FCWOnWorkCompletedSignature OnWorkCompleted;
};
