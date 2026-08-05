#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Painting/Zones/CWBaseWorkZone.h"
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
 *   꼭짓점 방향 전환이 아닌 이동 입력을 넣으면 작업을 놓고 그 방향으로 움직인다.
 *   전용 취소 키를 두지 않은 이유: 차량을 피해야 하는 순간에 손이 먼저 가는 곳이 WASD 다.
 *   도구를 내려놓거나(E) 존에서 멀어져도 중단된다. 어느 쪽이든 진행도는 남는다.
 *
 * UI 는 도구를 든 플레이어 화면에만 뜬다. 완료 여부는 부모의 진행도/델리게이트를 타고
 * 모두에게 공유되므로 결과는 전원이 볼 수 있다.
 *
 * 좌표계: 경로는 정규화 좌표로 다룬다. 가로 1.0, 세로 RectHeightRatio, Y 는 화면처럼 아래로 증가.
 * 실제 픽셀 변환은 위젯이 담당한다. 존은 화면 해상도를 몰라도 된다.
 *
 * 네트워크
 *   선은 서버가 진행시키고, 그 결과(DrawnLength / CurrentSegmentIndex / bWaitingAtCorner)를 전파한다.
 *   다만 서버 값만 따라가면 그리는 사람 화면에서 선이 핑만큼 끊겨 보인다.
 *   그래서 그리는 클라이언트만 같은 속도로 선을 로컬 진행시키고(PredictedDrawnLength),
 *   서버 값이 도착하면 그쪽으로 정정한다. 나머지 플레이어는 UI 가 아예 없으므로 예측이 필요 없다.
 */
UCLASS()
class CROSSWALK_API ACWWorkZone_BorderSketch : public ACWBaseWorkZone
{
	GENERATED_BODY()

public:
	ACWWorkZone_BorderSketch();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void BeginPlay() override;

	virtual void Tick(float DeltaSeconds) override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds) override;

	virtual bool ConsumeDirectionInput(const FVector2D& MoveInput) override;

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
	UFUNCTION()
	void OnRep_MinigameState();

	/** 그리는 사람 화면에서만 선을 로컬로 진행시킨다. */
	void TickPrediction(float DeltaSeconds);

	/** 위젯이 실제로 그릴 길이. 서버는 권위값, 그리는 클라이언트는 예측값을 쓴다. */
	float GetDisplayDrawnLength() const;

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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	FLinearColor GuideColor = FLinearColor(0.25f, 0.85f, 1.f, 1.f);

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

	// 예측값이 서버 값과 이만큼 벌어지면 보간하지 않고 즉시 맞춘다. (정규화 길이)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch", meta = (ClampMin = "0.01"))
	float PredictionSnapTolerance = 0.25f;

	// 꼭짓점 방향 전환이 아닌 이동 입력을 "작업 중단" 으로 받아들인다.
	//
	// 끄면 미니게임 중 WASD 가 전부 방향 전환용으로 소비되어 캐릭터가 제자리에 고정된다.
	// 그 경우 도구를 내려놓는 것(E) 외에는 빠져나갈 방법이 없어지므로 기본값은 켬이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch")
	bool bMovementCancelsMinigame = true;

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

	// 아래 4개는 서버가 정하고 전파한다. 미니게임의 실제 상태다.
	UPROPERTY(ReplicatedUsing = OnRep_MinigameState)
	float DrawnLength = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_MinigameState)
	int32 CurrentSegmentIndex = 0;

	UPROPERTY(ReplicatedUsing = OnRep_MinigameState)
	bool bMinigameActive = false;

	UPROPERTY(ReplicatedUsing = OnRep_MinigameState)
	bool bWaitingAtCorner = false;

	// 누가 그리고 있는지. 각 클라이언트는 이게 자기 것인지 보고 UI 표시를 결정한다.
	UPROPERTY(ReplicatedUsing = OnRep_MinigameState)
	TObjectPtr<UCWWorkerComponent> ActiveWorker;

	// 그리는 클라이언트에서만 쓰는 로컬 진행값. 위젯은 이 값을 그린다.
	float PredictedDrawnLength = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UCWBorderSketchWidget> Widget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BorderMaterial;

	// 0 = 가이드 모습, 1 = 도색 완료 모습.
	float PaintedBlend = 0.f;

	// 마지막으로 머티리얼에 넘긴 진행도. 값이 그대로면 갱신을 건너뛴다.
	float AppliedProgress = -1.f;
};
