#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/Enums/CWToolTypes.h"
#include "Painting/Data/CWBrushStroke.h"
#include "Data/Structs/CWStageObjective.h"
#include "CWBaseWorkZone.generated.h"

class ACWBaseWorkZone;
class UBoxComponent;
class UCWWorkerComponent;
class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnWorkProgressSignature, ACWBaseWorkZone*, Zone, float, Progress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCWOnWorkCompletedSignature, ACWBaseWorkZone*, Zone);

/**
 * 소지한 도구를 기준으로 이 존을 화면에 어떻게 표시할 것인가. (작업존 하이라이트 기획서 5.2)
 *
 * 작업 판정(CanWorkNow)과 별개인 축이다. 두 상태를 같이 보여주는 이유는 순서 때문이다.
 * 작업 가능한 존만 켜면, 순서가 아직인 도구를 들었을 때 화면에 아무 변화가 없어서
 * "이 도구는 고장났나" 로 읽힌다. 잠긴 존을 흐리게라도 보여줘야 순서가 전달된다.
 */
UENUM(BlueprintType)
enum class ECWZoneHighlightState : uint8
{
	// 이 도구와 무관하거나 이미 끝난 존. 아무것도 그리지 않는다.
	None,
	// 이 도구를 받는 존이지만 앞 순서가 안 끝나 잠겨 있다. "여기 쓸 건데 아직 차례가 아니다"
	Pending,
	// 지금 이 도구로 작업할 수 있다.
	Active
};

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

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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
	 * 이 존에서 작업하는 동안 패링을 막는가.
	 *
	 * 미니게임처럼 화면과 입력을 붙잡는 존만 true 를 돌려준다.
	 * 미니게임 중에는 이미 WASD 가 선 방향 전환에 묶여 캐릭터가 움직이지 않는데,
	 * 패링만 되면 "못 피하는 대신 튕겨내면 된다" 가 되어 미니게임의 위험 부담이 사라진다.
	 * 차가 오면 취소 키로 빠져나오는 것이 이 존의 정해진 대응이다.
	 *
	 * 게이지나 칸을 칠하는 존은 캐릭터가 자유롭게 움직이므로 막지 않는다.
	 */
	virtual bool BlocksParry() const { return false; }

	/**
	 * 이 존에서 작업하는 동안 조준(커서 방향 회전)을 멈추는가.
	 *
	 * 차징처럼 "어디를 칠할지 정해놓고 버틴다" 는 작업만 true 를 돌려준다.
	 * 그런 존은 누르는 순간 대상 범위가 확정되는데, 그 뒤로도 몸이 커서를 따라 돌면
	 * 화면에 보이는 조준과 실제로 칠해질 자리가 어긋난다.
	 *
	 * 이동 잠금과 달리 별도 훅이 필요한 이유: 이동은 ConsumeDirectionInput 으로 입력을
	 * 삼켜서 막지만, 커서 회전은 입력이 아니라 매 프레임 도는 연출이라 삼킬 것이 없다.
	 *
	 * 게이지나 칸을 칠하는 존은 조준이 곧 붓 위치라 막으면 안 된다.
	 */
	virtual bool BlocksAiming() const { return false; }

	/**
	 * 지금 차징 단계. 0 이면 차징 중이 아니거나 아직 최소 홀드를 못 채웠다.
	 *
	 * 애니메이션이 자세를 고르는 데 쓴다. 차징 개념이 없는 존은 그대로 두면 된다.
	 * 차징 상태는 누르는 본인 기계에만 있으므로 이 값도 그 기계에서만 의미가 있다.
	 */
	virtual int32 GetChargeLevel() const { return 0; }

	/** 게이지 채움용 0~1. 풀차징 시간 대비 얼마나 눌렀는가. */
	virtual float GetChargeRatio() const { return 0.f; }

	/**
	 * 이 존이 체온 상승을 스스로 보고하는가.
	 *
	 * 기본은 false 다. 작업 컴포넌트가 ApplyWork 에 넘긴 시간만큼 체온을 올려준다.
	 *
	 * 미니게임처럼 진행이 자기 Tick 에서 도는 존만 true 로 바꾼다. 그런 존은 작업 키를 누른
	 * 프레임에만 ApplyWork 가 오기 때문에, 작업 컴포넌트에 맡기면 키를 누른 동안만 더워진다.
	 * true 를 돌려주면 작업 컴포넌트는 손을 떼고, 존이 UCWWorkerComponent::SubmitWorkHeat 로
	 * 직접 보고한다.
	 *
	 * 클래스 단위로 고정한 값이어야 한다. 미니게임이 도는 중인지 같은 로컬 상태로 판단하면,
	 * 그 상태를 모르는 서버와 판단이 갈려 체온이 두 번 오르거나 아예 안 오른다.
	 */
	virtual bool DrivesOwnWorkHeat() const { return false; }

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
	void ReceiveReportedProgress(UCWWorkerComponent* Worker, float NewProgress);

	/**
	 * 소유 클라이언트가 자기 기계에서 확정한 붓질 하나를 서버가 받는다.
	 *
	 * 서버가 커서 좌표를 샘플링해서 붓질을 재현하면 안 되기 때문이다.
	 * 좌표는 초당 10번밖에 올라오지 않는데 클라이언트는 매 프레임 칠한다. 그 차이만큼
	 * 서버에 안 칠해진 구멍이 남고, 되돌릴 경로가 없어서 화면이 영영 어긋난다.
	 * 그래서 "어디를 칠했다"를 클라이언트가 직접 올려보내고 서버는 그대로 적용만 한다.
	 *
	 *
	 * 붓질을 하나씩 받지 않고 배열로 받는다. 붓질은 작업 키를 누르고 있는 동안 매 프레임
	 * 생기는데 그때마다 RPC 를 보내면 커넥션마다 초당 수백 개가 쌓인다.
	 * 보내는 쪽이 일정 간격으로 모아서 한 번에 올린다.
	 *
	 * 칸을 칠하는 존만 오버라이드한다. 게이지 존은 좌표가 필요 없다.
	 */
	virtual void ReceiveBrushStrokes(UCWWorkerComponent* Worker, const TArray<FCWBrushStroke>& Strokes) {}

	/**
	 * 소유 클라이언트가 붓을 놓으며 확정한 칸 번호들을 서버가 받는다.
	 *
	 * 붓질 배열과 같은 이유로 클라이언트가 직접 올린다. 차징은 누르는 본인 기계에서만 돌아서
	 * 서버는 몇 단계까지 찼는지, 어느 칸을 겨눴는지를 알 방법이 없다. 서버가 리플리케이트된
	 * 값으로 따로 재면 핑만큼 늦게 시작해 단계가 한 칸씩 어긋난다.
	 *
	 * 좌표가 아니라 칸 번호를 보내는 이유: 칸 배열은 Area 크기에서 계산되는 값이라 모든
	 * 기계에서 같다. 번호 하나가 자리를 정확히 가리키므로 좌표를 실어 보낼 이유가 없다.
	 *
	 * 테두리 존만 오버라이드한다.
	 */
	virtual void ReceiveBorderCells(UCWWorkerComponent* Worker, const TArray<int32>& CellIndices) {}

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

	/**
	 * 이 존을 이 도구로 어떻게 표시할 것인가. (기획서 5.3)
	 *
	 * CanWorkNow 를 그대로 쓰지 않고 따로 여는 이유가 이 함수의 존재 이유다.
	 * 칸 존은 깨끗한 오염 레이어에도 낙서를 시작할 수 있어야 해서 채우기 도구에 대해 완료를
	 * 보지 않는데, 그 판정을 안내에 그대로 쓰면 도색이 끝난 스트라이프가 흰 롤러를 든 내내
	 * 계속 빛난다.
	 *
	 * 작업 판정과 표시 판정을 나누되 기존 함수는 건드리지 않는다. 판정 책임이 존에 남는 것은
	 * CanUseTool · HasWorkAt · IsWorkerInRange 와 같은 구조다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work|Highlight")
	virtual ECWZoneHighlightState GetHighlightStateFor(ECWToolType ToolType) const;

	/**
	 * 표시 상태를 바꾼다. 바뀐 경우에만 OnHighlightStateChanged 가 돈다.
	 *
	 * 완전히 로컬이다. 리플리케이트하지 않는다. 판정에 쓰이는 값이 이미 전부 복제되고 있어서
	 * 각 머신이 같은 답을 내고, 남의 화면에 내 안내가 뜰 이유도 없다.
	 * 그래서 이 값은 "이 기계의 플레이어 기준" 이라는 점에 주의한다.
	 */
	void SetHighlightState(ECWZoneHighlightState NewState);

	UFUNCTION(BlueprintPure, Category = "Work|Highlight")
	ECWZoneHighlightState GetHighlightState() const { return HighlightState; }

	/**
	 * 이 지점에서 이 작업자가 실제로 진행시킬 일이 남아 있는가.
	 *
	 * 존이 겹쳐 있을 때 어느 쪽을 잡을지 가르는 데 쓴다. 박스 거리만 보면 겹친 두 존이
	 * 똑같이 거리 0 이라 등록 순서에 따라 아무 쪽이나 잡힌다. 도로 전체를 덮는 넓은 정리 존을
	 * 도색 존 위에 겹쳐 놓으면 그게 바로 문제가 된다. 검은색 롤러를 들고 횡단보도에 서면
	 * 지울 것이 없는 쪽이 붓을 붙잡아서, 발밑의 페인트가 안 지워지고 체온만 오른다.
	 *
	 * 기본은 true 다. 지점 개념이 없는 존(게이지·미니게임)은 그대로 두면 된다.
	 */
	virtual bool HasWorkAt(const UCWWorkerComponent* Worker, const FVector& WorldLocation) const { return true; }

	UFUNCTION(BlueprintPure, Category = "Work")
	float GetProgress() const { return Progress; }

	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsCompleted() const { return bCompleted; }

	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsLocked() const { return bLocked; }

	UFUNCTION(BlueprintPure, Category = "Work")
	FText GetObjectiveName() const { return ObjectiveName; }

	/**
	 * 이 존이 체크리스트에 추가로 내는 목표 줄. (기획서 3.6)
	 *
	 * 보통은 존 하나가 목표 한 줄이다. ObjectiveName 과 IsCompleted 하나씩이면 충분하다.
	 * 그런데 한 존이 두 가지 일을 겸하는 경우가 있다. 도색 존은 Area 안쪽으로 "도색 작업" 을,
	 * 바깥 고리로 "페인트 정리" 를 동시에 들고 있다. 삐져나온 페인트는 도색 존의 격자 안에
	 * 물리적으로 들어 있어서 별도 존으로 쪼갤 수가 없다.
	 *
	 * 그래서 겸업하는 존이 이걸 오버라이드해서 줄을 더 낸다. GameMode 는 자기 줄과 똑같이
	 * 이름으로 묶어서 체크리스트에 올린다. 기본은 아무것도 내지 않는다.
	 */
	virtual void GetExtraObjectives(TArray<FCWStageObjective>& OutObjectives) const {}

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

	/**
	 * 이 작업자가 지금 이 존에서 작업할 수 있는 위치에 있는가.
	 *
	 * 기본은 Area 박스까지의 거리로 본다. 게이지 존이나 테두리 미니게임처럼
	 * "존 옆에 서서 키를 누른다" 는 작업은 이 기준이 맞다.
	 *
	 * 칸을 칠하는 존은 이걸 오버라이드해서 "붓이 닿는 곳에 이 존의 칸이 있는가" 로 바꾼다.
	 * 그쪽은 "존 안에 들어와야 한다" 가 아니라 "손이 닿는 데까지 칠한다" 가 맞는 작업이고,
	 * 격자가 Area 박스보다 넓게 뻗어 있어서(바깥 고리) 박스 거리로는 판단할 수가 없다.
	 */
	virtual bool IsWorkerInRange(const UCWWorkerComponent* Worker, const FVector& WorkLocation) const;

	float GetWorkRange() const { return WorkRange; }

	/**
	 * 이 지점이 이 존의 영역(Area 박스) 안인가.
	 *
	 * 높이는 보지 않는다. 오염 레이어의 칸 판정 자체가 평면이라, 여기서 높이를 보면
	 * 박스 높이를 어중간하게 잡았을 때 마스킹만 조용히 빗나간다.
	 */
	bool ContainsAreaPoint(const FVector& WorldLocation) const;

	/**
	 * 이 존의 영역에는 페인트 오염이 생기지 않아야 하는가. (오염 레이어가 칸을 빼는 기준)
	 *
	 * 두 종류가 이걸 켠다.
	 *   도색 스트라이프 : 오염이 쌓이면 도색과 정리가 같은 칸을 두고 다툰다
	 *   테두리 가이드라인 : 그려둔 선 위에 페인트가 덮여 선이 안 보이게 된다
	 *
	 * 칸 존은 모드로 자동 판단하고, 그 외 존은 bBlocksPaintSpill 로 켠다.
	 */
	virtual bool BlocksPaintSpill() const { return bBlocksPaintSpill; }

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

	/**
	 * 표시 상태가 바뀔 때 호출. 연출을 붙이는 자리다.
	 *
	 * ACWBaseTool::OnFocusChanged 와 같은 패턴이다. 상태는 C++ 이 정하고, 그걸로 무엇을
	 * 그릴지는 자식이나 BP 가 정한다. 기본 구현은 아무것도 하지 않는다.
	 *
	 * 3단계에서 여기에 바닥 데칼 토글이 들어간다. 그때까지는 하이라이트 컴포넌트가
	 * 디버그 박스로 그리고 있어서 화면이 비지 않는다.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Work|Highlight")
	void OnHighlightStateChanged(ECWZoneHighlightState NewState);
	virtual void OnHighlightStateChanged_Implementation(ECWZoneHighlightState NewState);

	/**
	 * 하이라이트 데칼과 MID 를 만든다. 처음 켜질 때 한 번만 돈다.
	 *
	 * 생성자나 BeginPlay 가 아니라 여기서 만드는 이유는 리티클(UCWWorkReticleComponent)과 같다.
	 * 하이라이트는 완전히 로컬이라 데디케이티드 서버에서는 켜질 일이 없다. 켜질 때 만들면
	 * 그런 머신에서는 데칼이 아예 생기지 않는다. 존이 수십 개인 레벨에서 그만큼 절약된다.
	 */
	bool EnsureHighlightDecal();

	/**
	 * 데칼 크기와 테두리 두께를 Area 크기에서 다시 계산한다.
	 *
	 * 만드는 자리가 아니라 켜지는 자리에서 부른다. 그래야 PIE 중에 디테일 패널에서
	 * 배율이나 두께를 바꾸고 도구를 다시 들기만 해도 결과가 바로 보인다.
	 * 만들 때 한 번만 하면 값을 만질 때마다 PIE 를 껐다 켜야 한다.
	 */
	void ApplyHighlightDecalSize();

	/**
	 * 디버그 줄 끝에 붙일 추가 정보. 자식이 자기 단위를 덧붙이는 자리다.
	 *
	 * 퍼센트만으로는 "왜 완료가 안 되는지" 를 알 수 없다. 칸 존이라면 남은 칸이 몇 개인지가
	 * 보여야 완료 기준이 빡빡한 건지 아직 덜 칠한 건지 구분된다.
	 */
	virtual FString GetDebugDetailText() const { return FString(); }

	/**
	 * 진행도 값은 그대로인데 존 상태가 바뀌었을 때 알린다.
	 *
	 * SetProgress 는 값이 같으면 조기 반환하므로, 진행도에 안 잡히는 변화는 아무도 모른다.
	 * 도색 존의 삐져나온 페인트가 그렇다. 안쪽 칸만 진행도로 세기 때문에 바깥 고리를
	 * 아무리 칠하고 지워도 Progress 가 안 움직이고, 체크리스트의 "페인트 정리" 줄이
	 * 갱신되지 않는다. 그런 변화를 만든 쪽이 이걸 직접 불러야 한다.
	 */
	void NotifyStateChanged();

	/**
	 * 진행한 만큼 작업자의 도구에서 페인트를 뺀다. (기획서 5-3)
	 *
	 * @param ProgressDelta 이번에 실제로 오른 진행도(0~1). 요청한 값이 아니라 정말 오른 값을 넘겨야 한다.
	 *                      완료된 존에 계속 키를 누르고 있으면 진행은 0 인데 페인트만 빠진다.
	 */
	void ConsumePaintForProgress(UCWWorkerComponent* Worker, float ProgressDelta) const;

private:
	/**
	 * 감시 대상을 모으고 잠금을 처음 판정한다. BeginPlay 가 아니라 다음 틱에 돈다.
	 *
	 * 감시 대상을 서브시스템 등록 목록에서 찾기 때문이다. 액터의 BeginPlay 순서는 보장되지
	 * 않으므로, BeginPlay 안에서 바로 모으면 아직 등록되지 않은 존을 놓친다.
	 * 그 사이에는 보수적으로 잠가둔다. (BeginPlay 의 bLocked 초기값)
	 */
	void InitializeLockWatch();

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

	/**
	 * 이 존을 처음부터 끝까지 완주하는 데 드는 페인트 총량. (기획서 5-3)
	 *
	 * 존 크기나 진행 방식이 달라도 밸런싱 창구는 이 값 하나다. 구역별 배율을 따로 두지 않는다.
	 *   - 게이지·미니게임 존 : 진행도 1% 당 PaintCostFull/100 을 쓴다
	 *   - 칸 존             : 칸 1개당 PaintCostFull/점수영역 칸 수 를 쓴다
	 *
	 * 기본값 100 은 도구 최대 잔량(ACWBaseTool::MaxPaintAmount)과 같은 값이다.
	 * 그래서 안쪽만 정확히 칠하면 가득 찬 도구 하나로 존 하나가 딱 끝나고,
	 * 삐져나간 만큼만 모자라진다. 한쪽만 바꾸면 그 관계가 깨진다.
	 *
	 * 큰 존을 비싸게 만들고 싶으면 이 값만 올린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Paint", meta = (ClampMin = "0.0"))
	float PaintCostFull = 100.f;

	/**
	 * 이 존의 Area 안에는 페인트 오염이 생기지 않게 막는다.
	 *
	 * 오염 레이어가 시작할 때 이 영역과 겹치는 칸을 격자에서 빼버린다. 그 자리에는
	 * 애초에 켤 칸이 없으므로 칠해질 수도, 차량 자국이 남을 수도 없다.
	 *
	 * 테두리 가이드라인 존이 이걸 켠다. 선 위에 페인트가 덮이면 가이드라인이 안 보인다.
	 * 도색 스트라이프는 켤 필요가 없다. 칸 존이 모드를 보고 알아서 판단한다.
	 *
	 * 주의: Area 가 넓은 존에 켜면 그만큼 낙서할 수 없는 영역이 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Paint")
	bool bBlocksPaintSpill = false;

	/**
	 * 이 존을 도구 안내(하이라이트) 대상으로 삼을 것인가. (기획서 5.4)
	 *
	 * 도구 목록만으로는 거를 수 없는 존이 있어서 명시적 스위치를 둔다.
	 * ACWWorkZone_Cells::CanUseTool 은 FillTools / CleanupTools 에 든 도구를 AcceptedTools 와
	 * 무관하게 통과시키기 때문에, 낙서용으로 흰 롤러를 받는 오염 레이어는 흰 롤러를 드는
	 * 순간 도로 전체가 안내 대상이 된다.
	 *
	 * 추측으로 거르는 규칙(ObjectiveName 이 비었는지 등)보다 이쪽이 낫다.
	 * 왜 안 뜨는지가 레벨에서 바로 보인다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Highlight")
	bool bHighlightable = true;

	// 하이라이트를 그릴 데칼 머티리얼. 비워두면 이 존은 아무것도 그리지 않는다.
	//
	// 아래 파라미터 이름 두 개를 갖고 있어야 한다. 이름이 안 맞으면 값이 조용히 버려져서
	// 머티리얼 기본값 그대로 그려진다. 그래서 처음 만들 때 한 번 검사하고 로그를 남긴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Highlight")
	TObjectPtr<UMaterialInterface> HighlightMaterial;

	// 지금 작업할 수 있는 상태의 색.
	//
	// 리티클의 진행 가능 색(UCWWorkReticleComponent::WorkableColor)과 같은 값이다.
	// 두 표시가 같은 화면에 뜨는데 초록의 의미가 서로 다르면 화면이 거짓말을 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Highlight|Color")
	FLinearColor HighlightActiveColor = FLinearColor(0.1f, 0.9f, 0.2f, 1.f);

	// 아직 차례가 아닌 상태의 색.
	//
	// 빨강을 쓰지 않는다. 리티클이 빨강을 "눌러도 진행되지 않는다" 에 이미 쓰고 있어서,
	// 여기에 또 쓰면 두 빨강의 의미가 갈린다. Pending 은 금지가 아니라 "아직" 이므로
	// 색상을 바꾸는 대신 채도를 뺀다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Highlight|Color")
	FLinearColor HighlightPendingColor = FLinearColor(0.42f, 0.44f, 0.46f, 1.f);

	// 아직 차례가 아닌 상태의 세기. Active 는 항상 1 이다.
	//
	// 색만 바꾸지 않고 세기까지 빼는 이유: 색상(hue) 차이만으로 가르면 색각 이상 플레이어가
	// 두 상태를 구분하지 못한다. 명도가 같이 갈려야 누구에게나 읽힌다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Highlight|Color", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HighlightPendingIntensity = 0.35f;

	// 데칼 정렬 순서. 같은 자리를 여러 데칼이 덮으면 이 값이 큰 쪽이 위로 온다.
	//
	// 참고 — 먼지 = -10, 도색 자국(ACWPaintDecalField) = 0, 조준 원 = 100.
	// 도색 결과보다는 위에 보여야 하고, 조준 원보다는 아래여야 한다.
	// 조준이 항상 최상위라는 기존 규약을 깨지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Highlight")
	int32 HighlightDecalSortOrder = 50;

	// 데칼이 아래로 투영되는 깊이(cm). 도로 요철과 연석을 타넘는 여유다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Highlight", meta = (ClampMin = "1.0"))
	float HighlightDecalProjectionDepth = 50.f;

	/**
	 * 하이라이트를 Area 박스 대비 얼마로 그릴 것인가. X 가 존의 X, Y 가 존의 Y 다.
	 *
	 * Area 를 줄여서 맞추면 안 되기 때문에 따로 둔다. 그 박스에는 작업 가능 거리와 커서
	 * 트레이스가 묶여 있어서, 표시를 맞추려고 건드리면 작업 판정이 같이 바뀐다.
	 *
	 * 작업 영역이 실제 그림보다 넉넉하게 잡혀 있는 존에서 1 미만으로 낮춘다.
	 * 1 을 넘기면 그림 바깥까지 테두리가 나간다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Highlight", meta = (ClampMin = "0.05"))
	FVector2D HighlightSizeScale = FVector2D(1.f, 1.f);

	/**
	 * 테두리 선의 두께(cm). 월드 기준이다.
	 *
	 * 머티리얼이 UV 로 두께를 잡으면 안 되기 때문에 여기서 cm 로 받아 UV 로 환산해 넘긴다.
	 * 존마다 가로세로 비율이 다른데 UV 두께를 그대로 쓰면 짧은 쪽만 굵어진다.
	 * 횡단보도 스트라이프는 비율이 극단적이라 테두리가 존을 통째로 덮어버린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Highlight", meta = (ClampMin = "0.1"))
	float HighlightEdgeThickness = 12.f;

	// 머티리얼 파라미터 이름. 머티리얼 쪽 이름을 바꿨다면 여기도 같이 바꾼다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Highlight|Material")
	FName HighlightColorParameterName = TEXT("Color");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Highlight|Material")
	FName HighlightIntensityParameterName = TEXT("Intensity");

	// 축별 테두리 두께를 UV 단위로 담아 넘긴다. R 이 U 축, G 가 V 축이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Highlight|Material")
	FName HighlightEdgeWidthParameterName = TEXT("EdgeWidth");

	// 진행도가 이 값을 넘으면 완료 처리하고, 그 뒤로는 작업 입력을 받지 않는다.
	//
	// 기본값이 1 보다 낮은 이유는 칸 채우기 존 때문이다. 격자 구석 칸까지 전부 칠하기가
	// 까다로워서 살짝 여유를 준다. 게이지가 차오르는 존(Hold)은 100% 까지 채워야 자연스러우므로
	// 자식 생성자에서 1.0 으로 올린다.
	//
	// 남는 칸은 완료 판정이 난 뒤 칸 존이 알아서 채운다. (ACWWorkZone_Cells::TryAutoCompleteCounted)
	// 그래서 이 값은 "몇 칸까지 대신 채워줄 것인가" 를 정하는 셈이고, 낮출수록 한 번에
	// 툭 채워지는 덩어리가 커진다. 5cm 칸 3000개짜리 스트라이프 기준 0.985 면 45칸이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float CompleteThreshold = 0.985f;

	// 배치 시점의 시작 진행도. 0 이면 처음부터, 1 이면 이미 끝난 상태로 시작한다.
	//
	// 2단계 테두리처럼 "선은 이미 그려져 있고 가이드라인으로만 쓰고 싶다" 는 경우에 1 을 준다.
	// 존을 지워버리면 도색 단계의 경계선이 사라지므로, 존은 남기고 작업만 건너뛰는 방법이다.
	// 완료로 잡히려면 CompleteThreshold 이상이어야 한다. (테두리 존은 1.0)
	//
	// 진행도 자체는 서버가 정하는 리플리케이트 값이라 BP 에서 직접 못 쓴다. 그래서 시작값만
	// 따로 받는다. 여기 넣은 값은 서버의 BeginPlay 에서 한 번 적용되고 전원에게 전파된다.
	//
	// 칸(도색) 존은 예외다. 시작 진행도를 격자에서 스스로 계산해 덮으므로 이 값이 무시된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InitialProgress = 0.f;

	// 아래 3개는 서버가 정하고 전원에게 전파된다. 그래서 진행도와 완료가 팀 전체에서 하나다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Progress, Category = "Work")
	float Progress = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Completed, Category = "Work")
	bool bCompleted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Work")
	bool bLocked = false;

	// 진행도를 화면 좌상단에 띄운다.
	//
	// 기본값이 false 인 이유: 이 줄은 진행도가 바뀔 때마다 다시 만들어진다. 칸을 칠하는 존은
	// 붓질 한 번이 곧 진행도 변화라서, 칠하는 동안에는 매 프레임 FString::Printf 가 여러 번 돈다.
	// 프로파일링할 때 이게 켜져 있으면 다른 수치가 묻힌다. 필요할 때 인스턴스에서 켜면 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Debug")
	bool bShowDebugProgress = false;

private:
	// 잠금을 풀기 위해 끝나기를 기다리는 존들. 서버에서만 채운다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACWBaseWorkZone>> WatchedZones;

	// 지금 이 기계에서의 표시 상태. 로컬 전용이라 복제하지 않는다.
	ECWZoneHighlightState HighlightState = ECWZoneHighlightState::None;

	// 하이라이트 하나에 데칼 하나. 껐다 켤 때 만들고 지우지 않고 가시성만 바꾼다.
	UPROPERTY(Transient)
	TObjectPtr<UDecalComponent> HighlightDecal;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HighlightMID;

	// 데칼을 만들려다 실패했는가. 실패 로그를 상태가 바뀔 때마다 찍지 않으려고 둔다.
	bool bHighlightDecalSetupFailed = false;

public:
	UPROPERTY(BlueprintAssignable, Category = "Work")
	FCWOnWorkProgressSignature OnWorkProgress;

	UPROPERTY(BlueprintAssignable, Category = "Work")
	FCWOnWorkCompletedSignature OnWorkCompleted;
};
