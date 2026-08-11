#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Painting/Zone/CWBaseWorkZone.h"
#include "CWWorkZone_BorderSketch.generated.h"

class UCWBorderSketchWidget;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * 2단계 테두리용 존. 게이지가 아니라 미니게임으로 진행한다.
 *
 * 규칙
 *   1. 라인 테두리 도구를 들고 존에 들어가 작업 키를 누르면 시작된다
 *   2. 직사각형의 왼쪽 변 중앙에서 선이 위로 자동으로 그려진다
 *   3. 작업 키를 누르고 있으면 선이 BoostMultiplier 배로 빨라진다
 *   4. 꼭짓점에 닿으면 선이 멈추고, 다음 변 방향의 WASD 를 눌러야 다시 그려진다
 *   5. 한 바퀴 돌아 시작점으로 돌아오면 진행도 100% = 완료
 *
 * 실패 개념은 없다. 꼭짓점에서 방향을 못 넣으면 그냥 멈춰 있는다.
 * (차량이 오면 피해야 하므로, 멈춤 자체가 시간 손실이라는 압박이 된다)
 *
 * 중단 방법
 *   미니게임 중 WASD 는 방향 전환 전용이고 캐릭터는 제자리에 선다.
 *   빠져나오려면 취소 키(Q)를 누른다. 도구를 내려놓거나(E) 차량에 치여도 중단된다.
 *   어느 쪽이든 그려둔 진행도는 남으므로 돌아와서 이어서 할 수 있다.
 *
 *   이동 입력을 중단 신호로 겸용하지 않는 이유:
 *   방향을 꺾은 키가 몇 프레임 더 눌려 있는 것과 "이제 그만두겠다" 를 구분할 수 없어서,
 *   꼭짓점을 돌 때마다 작업이 끊기고 UI 가 닫혔다.
 *
 * UI 는 도구를 든 플레이어 화면에만 뜬다. 완료 여부는 부모의 진행도/델리게이트를 타고
 * 모두에게 공유되므로 결과는 전원이 볼 수 있다.
 *
 * 좌표계: 경로는 정규화 좌표로 다룬다. 가로 1.0, 세로 RectHeightRatio, Y 는 화면처럼 아래로 증가.
 * 실제 픽셀 변환은 위젯이 담당한다. 존은 화면 해상도를 몰라도 된다.
 *
 * 네트워크 — 미니게임은 그리는 본인 기계에서만 돈다.
 *
 *   선 위치, 현재 변, 꼭짓점 대기 상태는 전부 로컬이고 리플리케이트하지 않는다.
 *   그 화면을 보는 사람이 한 명뿐이라 남에게 보낼 이유가 없기 때문이다.
 *   서버로는 진행도 숫자만 초당 몇 번 올려보내고(완주는 신뢰 채널로 따로),
 *   서버가 그걸 부모의 Progress 에 반영하면 월드의 가이드 라인과 목표 체크리스트가
 *   모두에게 갱신된다.
 *
 *   값이 클라이언트 → 서버 한 방향으로만 흐르므로 되돌아오는 정정이 없다.
 *   그래서 예측/보정 로직 자체가 필요 없고, 꼭짓점 전환도 핑과 무관하게 즉시 반응한다.
 *
 *   협동 PvE 라 진행도 값을 검증하지 않는다. 서로 속일 이유가 없는 구조다.
 */
UCLASS()
class CROSSWALK_API ACWWorkZone_BorderSketch : public ACWBaseWorkZone
{
	GENERATED_BODY()

public:
	ACWWorkZone_BorderSketch();

	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void BeginPlay() override;

	virtual void Tick(float DeltaSeconds) override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds) override;

	/**
	 * 선은 작업 키를 떼도 계속 그려진다. 그동안에도 체온이 올라야 하므로 직접 보고한다.
	 * (작업 컴포넌트에 맡기면 스페이스바로 부스트하는 동안에만 더워진다)
	 */
	virtual bool DrivesOwnWorkHeat() const override { return true; }

	virtual bool ConsumeDirectionInput(const FVector2D& MoveInput) override;

	/** 미니게임이 도는 동안에는 패링을 막는다. 차가 오면 취소 키로 빠져나와야 한다. */
	virtual bool BlocksParry() const override { return bMinigameActive; }

	virtual void CancelWork() override;

	/** 도형 전체 경로(정규화 좌표). 위젯이 밑그림을 그리는 데 쓴다. */
	void GetFullPath(TArray<FVector2D>& OutPoints) const;

	/** 지금까지 그려진 부분(정규화 좌표). 마지막 점이 선의 머리다. */
	void GetDrawnPath(TArray<FVector2D>& OutPoints) const;

	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsMinigameActive() const { return bMinigameActive; }

	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsWaitingAtCorner() const { return bWaitingAtCorner; }

	/**
	 * 지금 꼭짓점에서 눌러야 하는 방향을 이동 입력 기준으로 돌려준다. (W = (0,1))
	 * 대기 중이 아니면 0 벡터. HUD 힌트에 쓸 수 있다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	FVector2D GetRequiredCornerInput() const;

private:
	/** 진행도를 서버에 올린다. bForce 면 주기를 무시하고 즉시 보낸다. */
	void ReportProgressIfDue(bool bForce);

	/** 지금 상태에 맞게 위젯을 띄우거나 내린다. */
	void RefreshWidget();

	/** 지금 이 인스턴스가 미니게임을 그리고 있는 본인인가. 위젯 표시 조건이다. */
	bool IsLocalDrawer() const;

	void StartMinigame(UCWWorkerComponent* Worker);

	/** UI 를 내리고 선을 멈춘다. 진행도는 남겨두므로 다시 오면 이어서 할 수 있다. */
	void StopMinigame();

	/** 작업자가 여전히 도구를 들고 범위 안에 있는가. */
	bool IsWorkerEligible() const;

	void AdvanceLine(float Amount);

	void ShowWidget();
	void HideWidget();

	/** Area 크기에 맞춰 테두리 막대 4개를 다시 배치한다. */
	void FitBorderToArea();

	/** 가이드(반투명) ↔ 도색 완료(불투명) 사이를 부드럽게 오간다. */
	void UpdateBorderVisual(float DeltaSeconds);

	/** 변의 개수. 시작점이 변의 중앙이라 반쪽 변 2개가 생겨서 5개다. */
	static constexpr int32 GetSegmentCount() { return 5; }

	/** 경로를 이루는 꼭짓점. 0 = 시작점(왼쪽 변 중앙), 5 = 시작점으로 복귀. */
	FVector2D GetPathPoint(int32 PointIndex) const;

	/** 세그먼트의 진행 방향(정규화 화면 좌표). */
	FVector2D GetSegmentDirection(int32 SegmentIndex) const;

	float GetSegmentLength(int32 SegmentIndex) const;

	/** 해당 세그먼트가 끝나는 지점까지의 누적 길이. */
	float GetSegmentEndLength(int32 SegmentIndex) const;

	float GetTotalLength() const;

	/** 대각 입력이 섞여도 지배적인 한 축만 남긴다. */
	static FVector2D GetDominantAxis(const FVector2D& Input);

public:
	// 바닥에 보이는 직사각형 테두리. 얇은 막대 4개를 인스턴스로 배치해 만든다.
	// 테두리 모양 메시 애셋이 따로 필요 없고, Area 크기를 바꾸면 자동으로 따라온다.
	// 판정은 부모의 Area 박스가 한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UInstancedStaticMeshComponent> BorderMeshes;

protected:
	// 테두리 선의 두께(cm).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide", meta = (ClampMin = "1.0"))
	float BorderThickness = 12.f;

	// 작업 전 상태. 반투명하게 띄워 "여기를 칠해야 한다" 를 알린다.
	//
	// 에디터 컬러 픽커의 Hex(sRGB) 기준 FFE200 에 해당하는 값이다.
	// FLinearColor 는 선형 색이라 sRGB 값을 그대로 넣으면 실제보다 밝게 나온다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	FLinearColor GuideColor = FLinearColor(1.f, 0.76f, 0.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GuideOpacity = 0.35f;

	// 완료 후 상태. 불투명한 흰 선이 되어 실제로 도색된 것처럼 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	FLinearColor PaintedColor = FLinearColor(0.95f, 0.95f, 0.92f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PaintedOpacity = 1.f;

	// 가이드에서 도색 완료 모습으로 넘어가는 데 걸리는 시간(초).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide", meta = (ClampMin = "0.0"))
	float PaintTransitionSeconds = 0.6f;

	// 머티리얼에 넘길 파라미터 이름. 없는 이름을 넘겨도 그냥 무시된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	FName ColorParameterName = TEXT("Color");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	FName OpacityParameterName = TEXT("Opacity");

	// 도형의 세로/가로 비율. 1.0 이면 정사각형.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float RectHeightRatio = 0.6f;

	// 아무 입력 없이 선이 나아가는 속도(정규화 길이/초).
	// 전체 경로 길이는 2 + RectHeightRatio*2 이므로 0.22 면 한 바퀴에 약 15초다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch", meta = (ClampMin = "0.01"))
	float DrawSpeed = 0.22f;

	// 작업 키를 누르고 있을 때의 속도 배수.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch", meta = (ClampMin = "1.0"))
	float BoostMultiplier = 3.f;

	// 진행도를 서버에 올리는 간격(초). 월드의 가이드 라인이 이 주기로 갱신된다.
	// 완주는 이 주기와 무관하게 즉시 신뢰 채널로 보낸다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch", meta = (ClampMin = "0.02"))
	float ProgressReportInterval = 0.1f;

	// 이동 입력이 이 시간 안에 다시 들어오면 "계속 누르고 있는 중" 으로 본다. (초)
	// 이동 입력은 누르는 내내 매 프레임 들어오므로, 한 프레임만 끊겨도 키를 뗀 것이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch", meta = (ClampMin = "0.02"))
	float DirectionInputHoldWindow = 0.15f;


	// 도구를 든 플레이어 화면에 띄울 위젯. 비워두면 C++ 기본 위젯이 쓰인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch")
	TSubclassOf<UCWBorderSketchWidget> WidgetClass;

	// 진행도(0~1)를 머티리얼에 넘길 스칼라 파라미터 이름.
	// 선이 차오르는 연출을 머티리얼에서 하고 싶을 때 쓴다. 안 써도 무방하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	FName ProgressParameterName = TEXT("Progress");

	// 테두리를 도로면보다 살짝 띄워 Z-파이팅을 막는다. (cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	float GuideHeightOffset = 1.f;

private:
	// 작업 키가 마지막으로 눌린 시각. 이 값으로 가속 여부를 판단한다.
	// bool 플래그를 쓰면 컴포넌트 틱과 액터 틱 순서에 따라 한 프레임씩 어긋난다.
	// 시작 시점에 가속으로 오판하지 않도록 충분히 과거로 초기화한다.
	float LastBoostTime = -1000.f;

	// 아래는 전부 미니게임을 돌리는 기계에만 있는 로컬 상태다. 리플리케이트하지 않는다.
	float DrawnLength = 0.f;

	int32 CurrentSegmentIndex = 0;

	bool bMinigameActive = false;

	bool bWaitingAtCorner = false;

	// 누가 그리고 있는지. UI 표시 대상을 정하는 데 쓴다.
	UPROPERTY(Transient)
	TObjectPtr<UCWWorkerComponent> ActiveWorker;

	// 진행도를 마지막으로 서버에 올린 시각.
	float LastProgressReportTime = -1000.f;

	// 이 기계에서 한 바퀴를 끝냈는가.
	//
	// 완료를 서버에 올려도 bCompleted 가 리플리케이트되어 돌아오기까지 한 왕복이 걸린다.
	// 그동안 CanWorkNow 는 아직 true 라서, 작업 키를 누르고 있으면 미니게임이 다시 시작된다.
	// 그러면 선이 마지막으로 받은 진행도까지 뒤로 물러났다가 다시 채워진다.
	// 서버 확정이 올 때까지 재시작을 막는 로컬 잠금이다.
	bool bLocallyCompleted = false;

	float LastDirectionInputTime = -1000.f;

	// 방향 전환에 쓴 키를 아직 떼지 않았는가. 떼기 전까지는 중단 입력으로 보지 않는다.
	bool bSuppressCancelUntilRelease = false;

	UPROPERTY(Transient)
	TObjectPtr<UCWBorderSketchWidget> Widget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BorderMaterial;

	// 0 = 가이드 모습, 1 = 도색 완료 모습.
	float PaintedBlend = 0.f;

	// 마지막으로 머티리얼에 넘긴 진행도. 값이 그대로면 갱신을 건너뛴다.
	float AppliedProgress = -1.f;
};
