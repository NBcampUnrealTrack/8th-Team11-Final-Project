#pragma once

#include "CoreMinimal.h"
#include "Painting/Zone/CWBaseWorkZone.h"
#include "CWWorkZone_BorderSketch.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UNiagaraSystem;
class USoundBase;

/**
 * 테두리 둘레를 이루는 칸 하나. (차징 붓 기획서 2.1)
 *
 * 리플리케이트하지 않는다. Area 크기와 CellLength 만 있으면 모든 기계가 똑같은 배열을
 * 만들어내므로, 네트워크로 오가는 것은 "몇 번 칸이 칠해졌는가" 뿐이면 된다.
 */
struct FCWBorderCell
{
	// 칸의 중심. 존 로컬 좌표다.
	FVector Center = FVector::ZeroVector;

	// 칸이 둘레를 따라 차지하는 길이(cm).
	float Length = 0.f;

	// 이 칸이 X 축 방향으로 누워 있는가. 인스턴스 배율을 어느 축에 줄지 정한다.
	bool bAlongX = false;
};

/**
 * 2단계 테두리용 존. 붓을 차징해서 테두리를 몇 칸씩 그어 나간다. (차징 붓 기획서)
 *
 * 규칙
 *   1. 테두리 직사각형의 둘레를 CellLength 길이의 칸으로 나눈다
 *   2. 라인 테두리 도구를 들고 존 옆에서 작업 키를 누르고 있으면 차징이 시작된다
 *   3. 누른 시간이 길수록 단계가 올라간다 (0.3초 1칸 → 0.7초 2칸 → 1.2초 3칸 → 1.8초 4칸)
 *   4. 키를 떼면 그 단계만큼의 칸이 한 번에 칠해진다
 *   5. 진행도 = 칠해진 칸 수 / 전체 칸 수. 전부 칠하면 완료다
 *
 * 차징 중에는 움직일 수도, 조준을 바꿀 수도, 패링할 수도 없다. 이게 이 존의 핵심이다.
 * 차가 달려올 때 "2단계에서 놓고 피할까, 0.5초 더 버텨서 4칸 찍을까" 가 매번 서는 계산이고,
 * 셋 중 하나라도 풀어주면 그 계산이 성립하지 않는다.
 *
 * 최소 홀드 0.3초를 못 채우고 떼면 아무것도 칠해지지 않는다(허탕). 탭 연타로 칸을 하나씩
 * 찍는 플레이를 막기 위한 것인데, **허탕에도 반드시 눈에 보이는 반응이 있어야 한다.**
 * 아무 일도 안 일어나면 입력이 씹힌 것으로 느껴진다. OnChargeReleased 가 단계 0 으로 불린다.
 *
 * 실패 개념은 없다. 칠한 칸은 되돌아가지 않는다.
 *
 * 네트워크 — 칠해진 칸은 서버가 들고 있다. (차징 붓 기획서 7절)
 *
 *   PaintedCellFlags 를 배열째로 리플리케이트한다. 칸이 수십 개(수십 바이트)뿐이라
 *   도색 존(ACWWorkZone_Cells)처럼 붓질 이벤트로 쪼개 보낼 이유가 없고, 배열을 통째로
 *   보내면 늦게 들어온 플레이어도 추가 작업 없이 같은 그림을 받는다.
 *
 *   차징은 누르는 본인 기계에서만 돈다. 누적 시간·앵커·방향은 리플리케이트하지 않는다.
 *   그 값을 보는 사람이 본인뿐이고, 서버가 리플리케이트된 값으로 차징을 따로 재면
 *   핑만큼 늦게 시작해 단계가 한 칸씩 어긋나기 때문이다.
 *
 *   붓을 놓으면 본인 화면에 먼저 칠하고(예측), 칸 번호만 서버로 올린다.
 *   서버가 그대로 적용하면 PaintedCellFlags 가 전원에게 내려가고, 예측이 빗나갔으면
 *   (페인트가 모자랐거나 남이 먼저 칠했거나) 그 값이 덮으면서 저절로 맞춰진다.
 *
 *   협동 PvE 라 값을 검증하지 않는다. 서로 속일 이유가 없는 구조다.
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

	virtual void ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds) override;

	/** 차징 중과 릴리즈 모션 동안에는 이동 입력을 삼킨다. 캐릭터는 제자리에 선다. */
	virtual bool ConsumeDirectionInput(const FVector2D& MoveInput) override;

	/**
	 * 차징 중에는 패링을 막는다.
	 *
	 * 손을 떼면 그 프레임에 풀린다. 릴리즈 모션 중에는 패링이 되어야 한다.
	 * 모션까지 막으면 "다 칠하고 튕겨낸다" 가 불가능해져서 이 존의 재미가 사라진다.
	 */
	virtual bool BlocksParry() const override { return bCharging; }

	/** 차징 중에는 조준(커서 방향 회전)도 멈춘다. 프리뷰가 흔들리지 않게 하기 위해서다. */
	virtual bool BlocksAiming() const override { return bCharging; }

	virtual void CancelWork() override;

	/** 소유 클라이언트가 붓을 놓으며 확정한 칸들. 서버에서만 유효하다. */
	virtual void ReceiveBorderCells(UCWWorkerComponent* Worker, const TArray<int32>& CellIndices) override;

	/** 지금 차징 단계. 0 이면 아직 최소 홀드를 못 채웠다(허탕). */
	UFUNCTION(BlueprintPure, Category = "Work|Charge")
	virtual int32 GetChargeLevel() const override;

	/** 게이지 채움용 0~1. 풀차징 시간 대비 얼마나 눌렀는가. */
	UFUNCTION(BlueprintPure, Category = "Work|Charge")
	virtual float GetChargeRatio() const override;

	UFUNCTION(BlueprintPure, Category = "Work|Charge")
	bool IsCharging() const { return bCharging; }

	/** 이번 릴리즈에 칠해질 칸 목록. 4단계 프리뷰가 이걸 그대로 쓴다. */
	void GetChargeTargetCells(TArray<int32>& OutCells) const;

	UFUNCTION(BlueprintPure, Category = "Work|Border")
	int32 GetCellCount() const { return Cells.Num(); }

	UFUNCTION(BlueprintPure, Category = "Work|Border")
	int32 GetPaintedCellCount() const;

	UFUNCTION(BlueprintPure, Category = "Work|Border")
	bool IsCellPainted(int32 CellIndex) const;

protected:
	/** 퍼센트만으로는 몇 칸이 남았는지 안 보인다. 완료가 안 되는 이유를 가르는 데 그 숫자가 필요하다. */
	virtual FString GetDebugDetailText() const override;

	/**
	 * 아래 셋은 차징하는 본인 기계에서만 불린다. 연출을 붙이는 자리다. (5단계)
	 * 남의 화면에서 남의 붓이 기를 모으는 소리가 나면 안 되므로 여기서 걸러 부른다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Work|Charge")
	void OnChargeStarted();

	/** 단계가 바뀔 때마다. "틱" 소리와 게이지 눈금이 여기에 붙는다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Work|Charge")
	void OnChargeLevelChanged(int32 NewLevel);

	/**
	 * 손을 뗐을 때. ChargeLevel 0 은 최소 홀드를 못 채운 허탕이다.
	 *
	 * PaintedCellCount 가 ChargeLevel 보다 작을 수 있다. 이미 칠해진 칸이 섞여 있거나
	 * 페인트가 모자라 되는 만큼만 칠한 경우다. 0 이면 붓이 헛돈 것이므로 마른 붓 소리를 낸다.
	 *
	 * bPaintShort 는 "잔량이 칸 하나 값에 못 미쳐 거기서 끊겼다" 는 뜻이다.
	 * 통이 비지는 않았으므로 기존 "페인트 없음" 알림에는 걸리지 않는 상태다.
	 * 이 경우에만 나는 소리를 따로 두어야 플레이어가 보충하러 갈 때라는 것을 안다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Work|Charge")
	void OnChargeReleased(int32 ChargeLevel, int32 PaintedCellCount, bool bPaintShort);

private:
	/**
	 * Area 크기에 맞춰 칸 배열과 가이드 인스턴스를 다시 만든다.
	 *
	 * 모든 기계가 같은 결과를 내야 한다. 여기에 난수나 시간, 플레이어 상태가 끼면
	 * 같은 칸 번호가 기계마다 다른 자리를 가리키게 된다.
	 */
	void RebuildCells();

	/** 변 하나를 칸으로 쪼개 배열에 붙인다. 시작점과 끝점은 존 로컬 좌표다. */
	void AppendSideCells(const FVector& Start, const FVector& End, bool bAlongX);

	/** 이 지점에서 가장 가까운 칸. 칸이 없으면 INDEX_NONE. */
	int32 FindNearestCell(const FVector& WorldLocation) const;

	/** 고리를 한 바퀴 도는 인덱스. 음수와 칸 수를 넘는 값을 모두 받아준다. */
	int32 WrapCellIndex(int32 CellIndex) const;

	/** 칸 하나를 칠한다. 서버에서만 유효하다. @return 실제로 새로 칠했으면 true */
	bool PaintCell(int32 CellIndex);

	/**
	 * 넘어온 칸들 중 이번에 실제로 칠해질 것만 골라낸다.
	 *
	 * 이미 칠해진 칸은 빼고, 남은 페인트로 살 수 있는 데까지만 남긴다. (차징 붓 기획서 2.6)
	 *
	 * 서버와 예측이 같은 함수를 쓴다. 판정을 두 벌로 적으면 잔량이 아슬아슬할 때만 결과가
	 * 갈려서, 평소에는 멀쩡하다가 페인트가 떨어질 즈음에만 칸이 칠해졌다 사라진다.
	 *
	 * @param bOutPaintShort 칠할 칸이 남았는데 잔량이 칸 하나 값에 못 미쳐 끊겼으면 true
	 */
	void FilterAffordableCells(const UCWWorkerComponent* Worker, const TArray<int32>& CellIndices,
		TArray<int32>& OutCells, bool& bOutPaintShort) const;

	/**
	 * 칸들을 실제로 칠하고 페인트를 뺀다. 서버에서만 유효하다.
	 * @return 이번에 새로 칠해진 칸 수
	 */
	int32 ApplyBorderCells(UCWWorkerComponent* Worker, const TArray<int32>& CellIndices, bool& bOutPaintShort);

	/**
	 * 서버 확정을 기다리지 않고 본인 화면에 먼저 칠한다. 비권위 클라이언트 전용이다.
	 *
	 * 진행도는 건드리지 않는다. 그 값은 서버 소유라 여기서 넣어도 다음 리플리케이션에
	 * 덮이고, 중간에 두 값이 어긋난 채로 체크리스트에 올라가기만 한다.
	 *
	 * @return 이번에 새로 칠한 것으로 보이게 한 칸 수
	 */
	int32 PredictBorderCells(const UCWWorkerComponent* Worker, const TArray<int32>& CellIndices,
		bool& bOutPaintShort);

	/** 칠해진 칸 수로 진행도를 다시 계산해 넣는다. 서버에서만 유효하다. */
	void RefreshProgressFromCells();

	/**
	 * 칠해진 칸 위에 덮는 불투명 인스턴스를 현재 상태에 맞게 다시 만든다.
	 *
	 * 기준은 PaintedCellFlags 가 아니라 RevealedCellFlags 다. 둘을 나눈 이유는
	 * 순차 점등 때문이다. 게임플레이는 릴리즈한 프레임에 이미 끝나 있고, 화면만 뒤따라간다.
	 */
	void RefreshPaintedInstances();

	/** 대기열에 있는 칸을 CellRevealInterval 간격으로 하나씩 켠다. */
	void TickCellReveal(float DeltaSeconds);

	/** 넘어온 순서 그대로 점등 대기열에 넣는다. 붓이 지나간 순서라 그대로 켜져야 한다. */
	void EnqueueRevealCells(const TArray<int32>& OrderedCells);

	/**
	 * 점등 상태를 칠해진 칸 목록에 맞춘다.
	 *
	 * 서버가 예측을 되돌렸으면 켜둔 칸을 즉시 끄고, 남이 칠했거나 늦게 들어와 새로 알게 된
	 * 칸은 대기열에 넣는다. 리플리케이션은 "누가 어떤 순서로 그었는지" 를 알려주지 않으므로
	 * 그쪽은 칸 번호 순으로 켠다.
	 */
	void SyncRevealQueueToFlags();

	void RevealCell(int32 CellIndex);

	/** 칸 중심의 월드 좌표. 파티클을 터트릴 자리다. */
	FVector GetCellWorldLocation(int32 CellIndex) const;

	/** 차징하는 본인에게만 들리는 소리. 남의 기계에서는 아무것도 하지 않는다. */
	void PlayLocalChargeSound(USoundBase* Sound, const UCWWorkerComponent* Worker) const;

	/** 칸 하나를 그릴 인스턴스 트랜스폼. ZOffset 만큼 띄운다. */
	FTransform MakeCellTransform(const FCWBorderCell& Cell, float ZOffset) const;

	/** 가이드/도색 머티리얼 인스턴스를 만들고 색을 넣는다. */
	void SetupMaterials();

	/** 차징 진행과 릴리즈 판정. Tick 에서 프리뷰보다 먼저 돈다. */
	void TickCharge(float DeltaSeconds);

	/**
	 * 바닥 프리뷰를 지금 상태에 맞춘다. 모든 기계에서 돌지만 각자 자기 플레이어 것만 그린다.
	 *
	 * 리플리케이트하지 않는다. 남의 조준을 보여주려면 그 값을 상시 올려보내야 하는데,
	 * 얻는 것(칸 겹침 방지)보다 비용이 크다. (차징 붓 기획서 결정 D)
	 */
	void UpdatePreview(float DeltaSeconds);

	/** 지금 이 기계의 플레이어에게 프리뷰를 보여줄 상황인가. */
	bool ShouldShowPreview(const UCWWorkerComponent* Worker) const;

	/** 이 기계에서 조종 중인 플레이어의 작업 컴포넌트. 데디케이티드 서버에서는 nullptr 이다. */
	UCWWorkerComponent* FindLocalWorker();

	/** 프리뷰 인스턴스를 새 목록에 맞춘다. 목록이 그대로면 아무것도 하지 않는다. */
	void ApplyPreviewCells(const TArray<int32>& ReadyCells, const TArray<int32>& BlockedCells);

	void UpdatePreviewPulse(float DeltaSeconds);

	/** 앵커부터 한 방향으로 Count 칸을 모은다. 고리를 넘어가도 같은 칸이 두 번 들어오지 않는다. */
	void CollectCellRun(int32 AnchorCell, bool bForward, int32 Count, TArray<int32>& OutCells) const;

	/**
	 * 이 작업자가 지금 어느 쪽으로 칠하게 되는가. 칸 인덱스가 늘어나는 쪽이면 true.
	 *
	 * 차징 시작과 상시 프리뷰가 같은 함수를 쓴다. 판정이 두 벌이면 "보이던 칸과 다른 칸이
	 * 칠해지는" 일이 생기는데, 그게 바로 프리뷰가 없애려는 문제다.
	 */
	bool ComputeChargeForward(const UCWWorkerComponent* Worker, int32 AnchorCell) const;

	void BeginCharge(UCWWorkerComponent* Worker, const FVector& WorldLocation);

	/** 확정된 단계만큼 칸을 칠하고 차징을 끝낸다. */
	void ReleaseCharge();

	/** 칸도 페인트도 건드리지 않고 차징을 버린다. (차에 치임 · 취소 · 도구 놓기) */
	void AbortCharge();

	void ResetChargeState();

	/** 차징하던 사람이 아직 조건을 만족하는가. 하나라도 어긋나면 차징을 버린다. */
	bool IsChargingWorkerEligible() const;

	/** 차징을 시작할 칸. 이미 칠해진 칸이면 진행 방향으로 조금 훑어 빈 칸을 찾는다. */
	int32 FindSnappedAnchor(int32 AnchorCell, bool bForward) const;

	/** 칸이 인덱스가 늘어나는 쪽으로 향하는 방향(존 로컬). 진행 방향을 정하는 데 쓴다. */
	FVector GetCellTangent(int32 CellIndex) const;

	/** 칸 하나를 칠하는 데 드는 페인트. */
	float GetCellPaintCost() const;

	/** 지금 차징하는 사람이 이 기계의 플레이어인가. 연출과 디버그 표시 대상을 가른다. */
	bool IsLocalCharger() const;

	void ShowChargeDebug(const TCHAR* Event, int32 Level, int32 PaintedCount) const;

	UFUNCTION()
	void OnRep_PaintedCells();

public:
	// 바닥에 보이는 테두리. 둘레를 쪼갠 칸을 인스턴스로 배치해 만든다.
	// 테두리 모양 메시 애셋이 따로 필요 없고, Area 크기를 바꾸면 자동으로 따라온다.
	// 판정은 부모의 Area 박스가 한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UInstancedStaticMeshComponent> BorderMeshes;

	// 칠해진 칸만 그리는 덮개. 가이드 위에 살짝 띄워 불투명하게 덮는다.
	//
	// 인스턴스 하나를 두 모습 사이에서 오가게 하지 않고 컴포넌트를 둘로 나눈 이유:
	// 인스턴스마다 색을 다르게 하려면 머티리얼이 per-instance custom data 를 읽어야 하는데,
	// 그러면 전용 머티리얼 없이는 아무 변화도 보이지 않는다. 컴포넌트를 나누면 머티리얼
	// 슬롯이 둘로 갈려서, 지금 붙어 있는 기본 머티리얼 그대로도 칸이 칠해지는 것이 보인다.
	//
	// 칸이 수십 개뿐이라 드로우콜 하나가 느는 비용은 문제가 되지 않는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UInstancedStaticMeshComponent> PaintedMeshes;

	// 지금 손을 떼면 칠해질 칸. 이 기계의 플레이어에게만 보인다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UInstancedStaticMeshComponent> PreviewMeshes;

	/**
	 * 지금 손을 떼도 칠해지지 않을 칸. 두 가지가 여기로 온다.
	 *   1. 이미 칠해졌거나 남은 페인트로 살 수 없는 칸 (차징 붓 기획서 2.6)
	 *   2. 아직 최소 홀드(0.3초)를 못 채워서 확정되지 않은 칸
	 *
	 * 둘을 한 색으로 묶은 이유: 플레이어가 알아야 하는 것은 "지금 놓으면 이 칸이 나가느냐"
	 * 하나뿐이고, 둘 다 답이 아니오다. 2번은 0.3초를 넘기는 순간 확정색으로 바뀌므로
	 * 그 전환 자체가 최소 홀드를 눈에 보이게 만든다. (결정 B 의 위험을 덜어주는 장치)
	 *
	 * 색을 칸마다 다르게 하려면 머티리얼이 per-instance custom data 를 읽어야 해서
	 * 전용 머티리얼 없이는 아무것도 안 보인다. 그래서 컴포넌트를 나눈다. (PaintedMeshes 와 같은 이유)
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UInstancedStaticMeshComponent> PreviewBlockedMeshes;

protected:
	// 칸 하나의 목표 길이(cm). (차징 붓 기획서 2.1)
	//
	// 변 길이가 이 값으로 딱 나눠떨어지지 않으므로, 변마다 칸 수를 반올림해 정한 뒤
	// 그 변 안에서 균등하게 나눈다. 실제 칸 길이는 이 값 근처에서 조금씩 달라진다.
	//
	// 칸이 크면 한 번에 칠해지는 맛이 시원하고, 작으면 정교해진다.
	// 존 길이 8m 기준 100 이면 둘레 칸이 대략 20~24개다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Border", meta = (ClampMin = "10.0"))
	float CellLength = 100.f;

	/**
	 * 각 차징 단계에 들어가는 데 필요한 누적 홀드 시간(초). (차징 붓 기획서 2.2)
	 *
	 * 칸 수는 이 배열의 길이가 정한다. 단계를 하나 더 두고 싶으면 값만 추가하면 된다.
	 *
	 * **첫 값이 곧 최소 홀드 시간이다.** 그보다 짧게 떼면 칸도 페인트도 나가지 않는다.
	 * 0 으로 두면 탭도 1칸이 되어 물뿌리개와 같아진다. 플레이 테스트에서 0.3초가
	 * 입력 씹힘으로 느껴지면 여기를 0.2 나 0 으로 낮춘다.
	 *
	 * 오름차순이어야 한다. 순서가 뒤섞이면 단계 판정이 조용히 어긋난다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Charge")
	TArray<float> ChargeThresholds = { 0.3f, 0.7f, 1.2f, 1.8f };

	// 차징을 시작할 칸이 이미 칠해져 있을 때 진행 방향으로 훑어볼 칸 수. (차징 붓 기획서 2.3)
	//
	// 이게 없으면 절반쯤 칠한 뒤부터는 "딱 빈 칸 앞에 서기" 가 이 작업의 주된 일이 된다.
	// 너무 키우면 엉뚱하게 먼 칸부터 칠해져서 어디가 칠해질지 예측이 안 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Charge", meta = (ClampMin = "0"))
	int32 AnchorSnapCells = 2;

	// 릴리즈 붓질 모션 동안 이동이 잠기는 시간(초). 패링은 이 동안에도 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Charge", meta = (ClampMin = "0.0"))
	float ReleaseSeconds = 0.25f;

	/**
	 * 작업 키를 뗀 것으로 판정하기까지 기다리는 시간(초).
	 *
	 * ApplyWork 는 키를 누르고 있는 프레임에만 오고, 뗀 것을 알려주는 훅은 없다.
	 * 그래서 마지막으로 온 시각을 재서 판단한다. 두 프레임 정도면 충분하다.
	 *
	 * bool 플래그로 "이번 프레임에 왔나" 를 보면 안 된다. 컴포넌트 틱과 액터 틱 순서에 따라
	 * 한 프레임씩 어긋나서, 누르고 있는데도 릴리즈로 잡히는 프레임이 생긴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Charge", meta = (ClampMin = "0.02"))
	float ReleaseGraceSeconds = 0.05f;

	// 테두리 선의 두께(cm). 선은 Area 경계에 중심을 두고 그려져서 안팎으로 절반씩 걸친다.
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

	// 칠해진 칸의 모습. 불투명한 흰 선이 되어 실제로 도색된 것처럼 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	FLinearColor PaintedColor = FLinearColor(0.95f, 0.95f, 0.92f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PaintedOpacity = 1.f;

	// 머티리얼에 넘길 파라미터 이름. 없는 이름을 넘겨도 그냥 무시된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	FName ColorParameterName = TEXT("Color");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	FName OpacityParameterName = TEXT("Opacity");

	/**
	 * 테두리를 도로면보다 살짝 띄워 Z-파이팅을 막는다. (cm)
	 *
	 * 오염 레이어의 칸 메쉬(ACWWorkZone_Cells::CellHeightOffset, 기본 1)보다 **높아야 한다.**
	 * 같으면 두 면이 같은 높이에서 다투다가 페인트가 선을 덮어버려서,
	 * 어디까지 칠해야 하는지 알려주는 가이드라인이 사라진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	float GuideHeightOffset = 2.f;

	// 도색 덮개를 가이드보다 얼마나 더 띄울 것인가. (cm)
	// 둘이 같은 높이면 어느 쪽이 위로 나올지 정해지지 않아 칠한 칸이 지직거린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide", meta = (ClampMin = "0.01"))
	float PaintedHeightOffset = 0.5f;

	// 이번에 칠해질 칸의 색. 도색 색과 같은 계열로 두면 "이렇게 칠해진다" 가 바로 읽힌다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preview")
	FLinearColor PreviewColor = FLinearColor(1.f, 1.f, 0.96f, 1.f);

	// 칠해지지 않을 칸의 색. 회색이라 "여기는 안 나간다" 로 읽힌다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preview")
	FLinearColor PreviewBlockedColor = FLinearColor(0.18f, 0.18f, 0.18f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preview", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PreviewOpacity = 0.8f;

	// 프리뷰가 깜빡이는 속도(Hz). 0 이면 깜빡이지 않는다.
	//
	// 칠해질 칸만 깜빡인다. 칠해지지 않을 칸은 가만히 둬서 둘이 한눈에 갈리게 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preview", meta = (ClampMin = "0.0"))
	float PreviewPulseSpeed = 3.f;

	// 깜빡임의 깊이. 0 이면 안 깜빡이고, 1 이면 완전히 사라졌다 돌아온다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preview", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PreviewPulseDepth = 0.45f;

	// 프리뷰를 도색 덮개보다 얼마나 더 띄울 것인가. (cm)
	// 이미 칠해진 칸을 겨눴을 때 그 위에 회색이 얹혀야 "여기는 안 나간다" 가 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preview", meta = (ClampMin = "0.01"))
	float PreviewHeightOffset = 0.5f;

	/**
	 * 칸이 하나씩 켜지는 간격(초). (차징 붓 기획서 5.5 "싹—")
	 *
	 * 4칸을 한 번에 툭 켜면 붓을 그은 것이 아니라 스탬프를 찍은 것처럼 보인다.
	 * 진행 방향으로 차례차례 켜져야 한 번의 붓질로 읽힌다.
	 *
	 * 0 이면 즉시 전부 켜진다. 점등은 연출일 뿐이라 게임플레이 판정(진행도·페인트·완료)은
	 * 릴리즈한 프레임에 이미 끝나 있다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|FX", meta = (ClampMin = "0.0"))
	float CellRevealInterval = 0.05f;

	// 칸이 켜질 때마다 그 자리에 터지는 페인트 튀김. (3단계 도색 존의 PaintStrokeFX 와 같은 것을 쓰면 된다)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|FX")
	TObjectPtr<UNiagaraSystem> CellPaintFX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|FX", meta = (ClampMin = "0.01"))
	float CellPaintFXScale = 1.f;

	// 풀차징에 도달한 순간 붓 끝에서 터지는 반짝임. 차징하는 본인에게만 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|FX")
	TObjectPtr<UNiagaraSystem> ChargeFullFX;

	/**
	 * 아래 셋은 차징하는 본인에게만 들린다. 남의 붓이 기를 모으는 소리가 나면 안 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Sound")
	TObjectPtr<USoundBase> ChargeLevelUpSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Sound")
	TObjectPtr<USoundBase> ChargeFullSound;

	// 아무것도 안 나갔을 때의 마른 붓 소리. 최소 홀드 미달·헛스윙·페인트 부족에 모두 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Sound")
	TObjectPtr<USoundBase> ReleaseFailSound;

	/**
	 * 붓이 그어지는 소리. 이것만 전원에게 들린다.
	 *
	 * 칸이 칠해지는 것은 월드에서 실제로 일어나는 일이라 남에게도 보이고 들려야 한다.
	 * 점등이 시작될 때 한 번 난다. 칸마다 나면 4연타로 들린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Sound")
	TObjectPtr<USoundBase> ReleaseSound;

	// 차징 상태를 화면에 띄운다. 연출이 붙기 전까지 단계 판정을 눈으로 확인하는 용도다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Debug")
	bool bShowDebugCharge = false;

	// 어느 칸이 칠해졌는가. 칸 인덱스 → 0/1.
	//
	// 서버가 정하고 전원에게 전파된다. 그래서 테두리 모양이 팀 전체에서 하나다.
	// 칸 배열 자체는 각자 계산하므로 여기에는 켜짐/꺼짐만 담는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_PaintedCells, Category = "Work|Border")
	TArray<uint8> PaintedCellFlags;

private:
	// 둘레를 쪼갠 칸. 모든 기계가 Area 크기에서 똑같이 만들어낸다. 리플리케이트하지 않는다.
	TArray<FCWBorderCell> Cells;

	// RebuildCells 가 정한 실제 선 두께. 존이 아주 얇으면 BorderThickness 보다 작아진다.
	float CellThickness = 0.f;

	// 아래는 전부 차징하는 기계의 로컬 상태다. 리플리케이트하지 않는다.
	bool bCharging = false;

	// 누적 차징 시간. 체온 계수가 곱해진 시간이 쌓이므로 더울수록 천천히 찬다.
	float ChargeSeconds = 0.f;

	// 차징을 시작한 칸. 여기서부터 진행 방향으로 단계만큼 칠한다.
	int32 ChargeAnchorCell = INDEX_NONE;

	// 칸 인덱스가 늘어나는 쪽으로 칠할 것인가. 차징 시작 시점에 정해지고 중간에 바뀌지 않는다.
	bool bChargeForward = true;

	// 마지막으로 알린 단계. 값이 바뀔 때만 OnChargeLevelChanged 를 부른다.
	int32 LastReportedChargeLevel = 0;

	// ApplyWork 가 마지막으로 온 시각. 이 값으로 릴리즈를 감지한다.
	float LastApplyWorkTime = -1000.f;

	// 릴리즈 모션이 끝나는 시각. 그때까지는 이동이 잠기고 새 차징도 시작되지 않는다.
	float ReleaseEndTime = -1000.f;

	UPROPERTY(Transient)
	TObjectPtr<UCWWorkerComponent> ChargingWorker;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GuideMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PaintedMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewBlockedMaterial;

	// 지금 프리뷰에 떠 있는 칸들. 목록이 그대로면 인스턴스를 다시 만들지 않는다.
	TArray<int32> PreviewReadyCells;

	TArray<int32> PreviewBlockedCells;

	// 깜빡임 위상. 누적 시간이라 프레임레이트와 무관하게 같은 속도로 돈다.
	float PreviewPulseTime = 0.f;

	// 이 기계의 플레이어. 매 프레임 컴포넌트를 찾지 않으려고 들고 있는다.
	UPROPERTY(Transient)
	TObjectPtr<UCWWorkerComponent> CachedLocalWorker;

	// 화면에 실제로 켜진 칸. 칠해진 칸(PaintedCellFlags)을 한 칸씩 따라간다.
	// 순수한 연출 상태라 리플리케이트하지 않는다. 각 기계가 자기 화면을 따라잡는다.
	TArray<uint8> RevealedCellFlags;

	// 아직 안 켜진 칸. 붓이 지나간 순서로 들어 있다.
	TArray<int32> PendingRevealCells;

	// 다음 칸을 켤 시각.
	float NextRevealTime = -1000.f;
};
