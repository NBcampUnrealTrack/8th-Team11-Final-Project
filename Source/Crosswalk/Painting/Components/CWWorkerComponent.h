#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Enums/CWToolTypes.h"
#include "Painting/Data/CWBrushStroke.h"
#include "CWWorkerComponent.generated.h"

class ACWBaseTool;
class ACWBaseWorkZone;
class UCWTemperatureComponent;
class UCWWorkerComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnTargetZoneChangedSignature, UCWWorkerComponent*, WorkerComponent, ACWBaseWorkZone*, TargetZone);

/**
 * 페인트가 없어 작업이 막히기 시작했을 때 true, 다시 채워져 풀렸을 때 false.
 *
 * 머리 위 "페인트가 부족합니다!" 알림이 붙는 자리다. (기획서 5-4)
 * 작업 키를 누르고 있는 동안 매 프레임이 아니라 상태가 바뀌는 순간에만 발행된다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnPaintDepletedChangedSignature, UCWWorkerComponent*, WorkerComponent, bool, bPaintDepleted);

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

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * 체온 때문에 작업이 얼마나 느려졌는가. 1 이면 평상 속도다.
	 *
	 * 시간으로 진행하는 존은 이 값이 곱해진 시간을 ApplyWork 로 받으므로 따로 볼 필요가 없다.
	 * 커서로 칠하는 존처럼 시간이 아니라 붓질로 진행하는 곳만 직접 물어본다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	float GetWorkSpeedFactor() const;

	/**
	 * 존이 자기 진행량만큼 체온을 올려달라고 보고한다.
	 *
	 * 미니게임 존처럼 진행이 자기 Tick 에서 도는 존이 쓴다. 그런 존은 작업 키를 누른 프레임에만
	 * ApplyWork 가 오기 때문에, 이 컴포넌트의 Tick 에 체온을 맡기면 부스트할 때만 더워진다.
	 *
	 * 체온은 서버가 굴리므로 클라이언트에서 부르면 서버로 올려보낸다. 매 프레임 보내지 않고
	 * WorkHeatSendInterval 로 묶는다.
	 *
	 * @param EffectiveWorkSeconds 작업속도 계수가 이미 곱해진 시간(초). AddWorkHeat 와 같은 단위다.
	 */
	void SubmitWorkHeat(float EffectiveWorkSeconds);

	/**
	 * 존이 자기 진행량만큼 페인트를 써달라고 보고한다. (기획서 5-3)
	 *
	 * 체온의 SubmitWorkHeat 과 같은 자리다. 다만 이쪽은 서버로 올려보내지 않는다.
	 * 잔량은 도구 액터가 리플리케이트하고, 서버는 서버대로 같은 계산을 돌리기 때문이다.
	 *   - 게이지 존   : 서버에서만 ApplyWork 가 돌아 서버가 깎는다
	 *   - 칸 존       : 칠한 본인이 로컬로 깎고, 올라간 붓질을 받아 서버도 깎는다
	 *   - 미니게임 존 : 그리는 본인이 로컬로 깎고, 올라간 진행도를 받아 서버도 깎는다
	 * 어느 쪽이든 양쪽이 같은 양을 깎으므로 따로 RPC 를 태울 필요가 없고,
	 * 어긋나더라도 서버 값이 리플리케이션으로 덮는다.
	 */
	void ConsumePaint(float Amount);

	/**
	 * 존이 스스로 진행하다 페인트가 떨어져 멈췄다고 보고한다. (기획서 5-4)
	 *
	 * 테두리 미니게임처럼 작업 키를 누르지 않아도 진행하는 존이 쓴다. 이 컴포넌트는
	 * "작업 키를 누르고 있는가" 로 소진 시점을 잡기 때문에, 키를 떼고 있는 동안 멈춘 것은
	 * 스스로 알 수 없다. 그러면 UI 만 조용히 닫히고 이유가 안 보인다.
	 *
	 * 서버로도 올려보낸다. 미니게임은 그리는 본인 기계에서만 돌아 서버가 스스로 알 수 없는데,
	 * 머리 위 알림은 보급자가 봐야 의미가 있어서 서버를 거쳐야 남의 화면에도 뜬다.
	 *
	 * 켜기만 하고 끄지는 않는다. 페인트가 채워지거나, 존을 벗어나거나, 다운되면 Tick 이 알아서 푼다.
	 */
	void NotifyZonePaintDepleted();

	/**
	 * 지금 들고 있는 도구에 작업할 페인트가 남아 있는가. (기획서 5-1)
	 *
	 * 빗자루처럼 페인트를 쓰지 않는 도구는 언제나 true 다.
	 * 맨손도 true 를 돌려준다. 도구가 없어서 작업이 안 되는 것은 존이 따로 거른다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work|Paint")
	bool HasPaintForWork() const;

	/**
	 * 페인트가 없어 작업이 막힌 상태인가. 머리 위 알림의 현재 상태를 물어볼 때 쓴다.
	 *
	 * 위젯이 뒤늦게 만들어지면 이미 지나간 OnPaintDepletedChanged 를 받을 수 없다.
	 * 생성 시점에 이 값으로 한 번 맞추고, 이후는 델리게이트로 따라가면 된다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work|Paint")
	bool IsPaintDepleted() const { return bPaintDepleted; }

	/** 지금 들고 있는 도구 액터. 맨손이면 nullptr. 잔량을 물어볼 때 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Work|Paint")
	ACWBaseTool* GetHeldToolActor() const { return HeldToolActor; }

	/** 작업 키를 눌렀을 때. Enhanced Input 의 Started 에 연결한다. */
	UFUNCTION(BlueprintCallable, Category = "Work")
	void StartWork();

	/** 작업 키를 뗐을 때. Completed / Canceled 에 연결한다. */
	UFUNCTION(BlueprintCallable, Category = "Work")
	void StopWork();

	/**
	 * 도구를 집거나 놓을 때 호출한다. 놓을 때는 ECWToolType::None 과 nullptr.
	 *
	 * 종류와 액터를 둘 다 받는 이유: 종류는 남의 화면에도 필요해서 리플리케이트하지만,
	 * 잔량은 도구 액터가 들고 있어서 실물 포인터가 있어야 물어볼 수 있다. (기획서 5-1)
	 * 액터를 생략하면 종류만 바뀐다. 홀더 없이 이 함수만 불러 테스트하던 경로가 그대로 남는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Work")
	void SetHeldTool(ECWToolType NewTool, ACWBaseTool* NewToolActor = nullptr);

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

	/**
	 * 이번 프레임에 계산된 붓 중심. 커서 지점을 MaxBrushDistance 로 끌어당긴 뒤의 값이다.
	 *
	 * 조준 표시기(UCWWorkReticleComponent)가 이 값을 그대로 원의 중심으로 쓴다.
	 * 표시기가 커서 트레이스를 다시 쏘지 않는 이유가 여기 있다. 같은 값을 두 번 계산하면
	 * 화면에 보이는 자리와 실제로 칠해지는 자리가 어긋날 수 있고, 그게 이 기능이 없애려는 문제다.
	 *
	 * 원격 캐릭터에서는 갱신되지 않는다. TickComponent 가 조기 반환하기 때문이다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	FVector GetWorkLocation() const { return CurrentWorkLocation; }

	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsWorking() const { return bWorking; }

	/** 다운 등으로 작업이 막혀 있는가. 미니게임 존이 중단 판단에 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsWorkBlocked() const { return bWorkBlocked; }

	/**
	 * 작업이 실제로 진행되는 중인가. 작업 애니메이션과 이펙트가 이 값을 본다.
	 *
	 * IsWorking 과 다르다. 그쪽은 "작업 키를 누르고 있다" 일 뿐이라, 존도 없는 허공에 대고
	 * 키를 눌러도 true 다. 이 값은 대상 존이 있고 페인트도 있는 경우만 true 다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsWorkEffective() const { return bWorkEffective; }

	/**
	 * 지금 하는 작업 때문에 패링이 막혀 있는가. 캐릭터가 패링 입력을 무시할 때 쓴다.
	 *
	 * 이동 잠금과 같은 이유로 로컬에서만 판단한다. 서버 확정을 기다리면 그 사이 입력이
	 * 반쯤 먹는 상태가 되고, 미니게임 상태 자체가 그리는 본인 기계에만 있다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsParryBlocked() const;

	/**
	 * 이 컴포넌트가 이 기계에서 조종 중인 플레이어의 것인가.
	 * 로컬로 시뮬레이션하는 존이 "내가 돌려야 할 작업인지" 판단하는 데 쓴다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsLocalWorker() const;

	/** 존이 "이 작업자가 작업할 수 있는 위치인가" 를 판단할 때 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Work")
	float GetMaxWorkDistance() const { return MaxWorkDistance; }

	/**
	 * 로컬에서 계산한 진행도를 서버에 올린다. 테두리 미니게임처럼
	 * 진행 자체는 본인 기계에서 돌고 결과만 공유하면 되는 존이 쓴다.
	 */
	void ReportWorkProgress(ACWBaseWorkZone* Zone, float NewProgress);

	/**
	 * 로컬에서 확정한 붓질들을 서버로 올린다. 칸을 칠하는 존이 일정 간격으로 모아서 부른다.
	 *
	 * 서버가 ReceivedWorkLocation 으로 붓질을 재현하지 않고 이걸 쓰는 이유는
	 * ACWBaseWorkZone::ReceiveBrushStrokes 주석에 적어 뒀다.
	 *
	 * 붓질 하나마다 부르지 않는 이유: 붓질은 매 프레임 생긴다. 그대로 하나씩 Reliable 로
	 * 보내면 커넥션마다 초당 수백 개가 쌓인다. 존이 모아서 배열로 넘긴다.
	 * 존이 "바뀐 칸이 있을 때"만 모으므로 붓이 멈춰 있으면 패킷도 나가지 않는다.
	 */
	void SubmitBrushStrokes(ACWBaseWorkZone* Zone, const TArray<FCWBrushStroke>& Strokes);

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
	 * 미니게임이 페인트 소진으로 멈췄다는 보고. 놓치면 남의 화면에 알림이 안 뜨므로 Reliable 이다.
	 * 끄는 보고는 없다. 해제 조건은 서버도 자기 Tick 에서 같이 본다.
	 */
	UFUNCTION(Server, Reliable)
	void Server_NotifyZonePaintDepleted();

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
	 * 존이 보고한 체온 상승분. 몇 개 유실돼도 그만큼만 덜 오르고 끝이라 Unreliable 로 둔다.
	 * (진행도와 달리 누적값이 아니라 증분이라 다음 패킷이 덮어주지 않는다는 점은 감수한다)
	 */
	UFUNCTION(Server, Unreliable)
	void Server_SubmitWorkHeat(float EffectiveWorkSeconds);

	/** 모아둔 체온 상승분을 서버로 올린다. 소유 클라이언트에서만 의미가 있다. */
	void FlushPendingWorkHeat();

	/**
	 * 붓질 보고. 놓치면 그 자리만 영영 안 칠해지거나 안 지워지므로 Reliable 이어야 한다.
	 * 존이 모아서 넘긴 배열이 그대로 올라간다. 칸 상태가 실제로 바뀐 붓질만 들어 있다.
	 */
	UFUNCTION(Server, Reliable)
	void Server_SubmitBrushStrokes(ACWBaseWorkZone* Zone, const TArray<FCWBrushStroke>& Strokes);

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

	/**
	 * 커서가 가리키는 지점을 캐릭터 주변으로 끌어당긴다.
	 *
	 * 존을 "고르는" 거리 제한(MaxWorkDistance)만으로는 붓이 닿는 범위가 제한되지 않는다.
	 * 존 옆에 서기만 하면 커서로 존 반대편 끝까지 칠할 수 있었다. 캐릭터를 움직여 가며
	 * 칠하는 게 아니라 제자리에서 마우스만 휘두르면 끝나는 작업이 되어 버린다.
	 *
	 * 높이는 커서가 맞은 바닥 높이를 그대로 쓰고 평면 거리만 자른다.
	 */
	FVector ClampWorkLocationToPawn(const FVector& PawnLocation, const FVector& CursorLocation) const;

	void SetTargetZone(ACWBaseWorkZone* NewZone);

	/** 페인트 부족 상태가 바뀌었을 때만 델리게이트를 발행한다. */
	void SetPaintDepleted(bool bNewDepleted);

	UFUNCTION()
	void OnRep_PaintDepleted();

	/** 부족 상태가 실제로 바뀐 뒤의 처리. 서버·소유 클라이언트·원격 모두 각자 여기로 모인다. */
	void HandlePaintDepletedChanged();

	/** 들고 있는 도구의 잔량을 화면에 띄운다. 페인트 UI 가 붙기 전까지 프로토타입 확인용. */
	void ShowDebugPaint() const;

	bool IsLocallyControlledWorker() const;

public:
	// 존이 캐릭터에서 이 거리보다 멀면 작업할 수 없다. (cm)
	// 존마다 지정된 WorkRange 와 비교해서 더 짧은 쪽이 적용된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	float MaxWorkDistance = 150.f;

	// 붓이 캐릭터에서 닿을 수 있는 최대 평면 거리(cm). 커서가 이보다 멀면 여기까지만 칠해진다.
	//
	// MaxWorkDistance 와 역할이 다르다. 그쪽은 "어느 존에서 작업할지" 를 고르는 기준이고,
	// 이건 "붓을 어디에 찍을지" 의 한계다. 존은 횡단보도처럼 길게 뻗어 있어서, 존을 고르는
	// 기준만으로는 반대편 끝까지 칠하는 것을 막지 못한다.
	//
	// 0 이면 제한하지 않는다. (예전 동작)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "0.0"))
	float MaxBrushDistance = 100.f;

protected:
	// 작업 지점을 찾을 커서 트레이스 채널. 칸을 칠하는 존에서만 의미가 있다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	TEnumAsByte<ECollisionChannel> WorkTraceChannel = ECC_Visibility;

	// 작업 지점을 서버에 올리는 간격(초). 0.1 이면 초당 10번이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "0.02"))
	float WorkLocationSendInterval = 0.1f;

	// 들고 있는 도구의 페인트 잔량을 화면 좌상단에 띄운다. 페인트 HUD 가 생기면 끄면 된다.
	// 자기가 조종하는 캐릭터에서만 그린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Debug")
	bool bShowDebugPaint = false;

	// 존이 보고한 체온 상승분을 모아 서버로 올리는 간격(초).
	// 매 프레임 보낼 이유가 없다. 어차피 합산해서 더하는 값이라 묶어도 결과가 같다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "0.02"))
	float WorkHeatSendInterval = 0.2f;

	// 진행이 확인되기를 기다리며 쌓아둘 수 있는 작업 시간의 상한(초).
	//
	// 칸을 칠하는 존은 진행이 칸 단위다. 붓을 꾸준히 움직여도 새 칸이 붓에 들어오는 것은
	// 몇 프레임에 한 번이고, 천천히 칠할수록 그 간격이 길어진다. 그래서 프레임마다
	// "지금 뒤집을 칸이 있나" 로 바로 가르면 실제로 일하는 중인데 체온이 안 오른다.
	//
	// 대신 흐른 시간을 쌓아 두었다가 진행이 확인되는 순간 한 번에 넘긴다. 진행이 이어지는
	// 한 흐른 시간이 결국 전부 반영되므로 상승량은 예전과 같고, 기획서 3.3 의 균형점도 그대로다.
	// 이 값은 최대 체온을 바꾸지 않는다. 덧칠만 하고 있으면 여기서 잘려 더 쌓이지 않는다.
	//
	// 너무 작으면 천천히 칠할 때 체온이 덜 오르고, 너무 크면 한참 덧칠하다 한 칸 칠했을 때
	// 그만큼이 한 번에 들어온다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "0.0"))
	float MaxUnheatedWorkSeconds = 0.5f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Work")
	ECWToolType HeldTool = ECWToolType::None;

	// 소유 클라이언트는 반응성을 위해 즉시 세팅하고, 서버 값이 오면 덮인다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Work")
	bool bWorking = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Work")
	bool bWorkBlocked = false;

	// 작업이 실제로 진행되는 중인가. bWorking 과 달리 대상 존과 페인트까지 본 결과다.
	//
	// 복제하는 이유: 판정에 쓰이는 TargetZone 은 Transient 이고, 원격 캐릭터에서는
	// Tick 이 조기 반환해 계산조차 되지 않는다. 남의 화면에서 "저 사람이 지금 일하는 중인가"
	// 를 알 방법이 이 플래그밖에 없다.
	//
	// bWorking 과 같이 소유 클라이언트가 즉시 세팅하고, 서버 값이 오면 덮인다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Work")
	bool bWorkEffective = false;

private:
	// 대상 존은 HUD 표시와 서버 판정에 각각 필요해서 양쪽이 로컬로 계산한다.
	UPROPERTY(Transient)
	TObjectPtr<ACWBaseWorkZone> TargetZone;

	// 서버가 마지막으로 받은 작업 지점. 서버에서만 의미가 있다.
	FVector ReceivedWorkLocation = FVector::ZeroVector;

	// 이번 프레임의 붓 중심. GetWorkLocation 이 돌려주는 값이다.
	// TargetZone 과 같은 자리에서 같이 갱신된다.
	FVector CurrentWorkLocation = FVector::ZeroVector;

	// 작업 지점을 마지막으로 올려보낸 시각. 소유 클라이언트에서만 쓴다.
	float LastWorkLocationSendTime = -1000.f;

	// 아직 서버로 올리지 않은 체온 상승분. 소유 클라이언트에서만 쓴다.
	float PendingWorkHeat = 0.f;

	float LastWorkHeatSendTime = -1000.f;

	// 아직 체온으로 넘기지 못한 작업 시간. 진행이 확인되면 한 번에 넘기고 0 으로 돌린다.
	float UnheatedWorkSeconds = 0.f;

	// [진단로그용] H-3 HUD 갱신 지연 기준 실측용 - 측정 끝나면 제거. 마지막으로 로그를 찍은 값.
	ECWToolType LastLoggedHeldTool = ECWToolType::None;
	bool bLastLoggedWorking = false;
	bool bLastLoggedWorkBlocked = false;

	// 같은 캐릭터의 체온 컴포넌트. BeginPlay 에서 한 번 찾아 둔다. 없어도 동작한다.
	UPROPERTY(Transient)
	TObjectPtr<UCWTemperatureComponent> TemperatureComponent;

	/**
	 * 지금 들고 있는 도구 액터. 잔량을 묻고 깎는 대상이다.
	 *
	 * 리플리케이트하지 않는다. 서버는 PerformInteract 에서, 클라이언트는 OnRep_HeldTool 에서
	 * 각자 홀더가 넣어주므로 양쪽 모두 자기 값을 갖는다. 남의 도구 잔량은 그 도구 액터가
	 * 직접 리플리케이트하므로 여기를 거칠 이유가 없다.
	 */
	UPROPERTY(Transient)
	TObjectPtr<ACWBaseTool> HeldToolActor;

	/**
	 * 페인트가 없어 작업이 막힌 상태인가. 매 프레임 알리지 않고 바뀌는 순간만 잡는다.
	 *
	 * 리플리케이트하는 이유는 기획서 5-4 의 기획 의도 때문이다. 머리 위 "페인트가 부족합니다!" 는
	 * 본인이 아니라 **보급자가 봐야** 의미가 있다. 로컬 계산만 두면 판정이 서버와 본인 기계에서만
	 * 돌아서(TickComponent 가 원격 캐릭터에서 조기 반환한다) 참가자 화면에는 남의 알림이 뜨지 않는다.
	 *
	 * 소유 클라이언트는 반응성을 위해 로컬 판정으로 즉시 세팅하고, 서버 값이 오면 덮인다.
	 * bWorking 과 같은 모델이다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_PaintDepleted)
	bool bPaintDepleted = false;

	// 존이 스스로 진행하다 페인트 소진으로 멈췄다고 보고했는가. (NotifyZonePaintDepleted)
	//
	// 작업 키를 떼고 있어도 알림을 띄워두기 위한 플래그다. 리플리케이트하지 않는다.
	// 서버는 RPC 로 따로 받고, 이 값에서 나온 결과(bPaintDepleted)만 전원에게 나간다.
	bool bZoneReportedDepleted = false;

	// 취소를 눌렀는가. 서버 응답을 기다리지 않고 이동 잠금을 즉시 풀기 위한 로컬 플래그다.
	// 리플리케이트하지 않는다. 작업을 다시 시작할 때 풀린다.
	bool bCancelRequested = false;

public:
	UPROPERTY(BlueprintAssignable, Category = "Work")
	FCWOnTargetZoneChangedSignature OnTargetZoneChanged;

	UPROPERTY(BlueprintAssignable, Category = "Work|Paint")
	FCWOnPaintDepletedChangedSignature OnPaintDepletedChanged;
};
