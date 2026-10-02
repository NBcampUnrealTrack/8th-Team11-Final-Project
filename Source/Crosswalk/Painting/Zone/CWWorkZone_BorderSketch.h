#pragma once

#include "CoreMinimal.h"
#include "Painting/Zone/CWBaseWorkZone.h"
#include "CWWorkZone_BorderSketch.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
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
 *   4. 키를 떼면 그 단계만큼의 칸이 한 번에 칠해진다. 최대 단계에 닿고 FullChargeHoldSeconds(1초)가
 *      지나면 떼지 않아도 칠해지고 작업이 끝난다. 다음 차징은 키를 다시 눌러야 시작된다
 *      (더 눌러도 얻는 게 없는데 계속 붙잡고 있으면 차에 노출되는 시간만 늘어난다)
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
	/** 칸별 안내선이 도색 상태를 표시하므로 전체 사각형 데칼은 겹쳐 그리지 않는다. */
	virtual void OnHighlightStateChanged_Implementation(ECWZoneHighlightState NewState) override;

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
	 * 손을 뗐을 때, 또는 최대 단계에서 FullChargeHoldSeconds 가 지나 저절로 나갔을 때. ChargeLevel 0 은 최소 홀드를 못 채운 허탕이다.
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
	 * 칸마다 "칠해졌는가" 를 custom data [0] 에 넣는다.
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

	/** 칸 하나를 그릴 인스턴스 트랜스폼. */
	FTransform MakeCellTransform(const FCWBorderCell& Cell) const;

	/** 결과 띠 한 조각을 그릴 인스턴스 트랜스폼. 칸보다 넓고 조금 더 높다. */
	FTransform MakeBandTransform(const FCWBorderCell& Cell) const;

	/** 결과 띠를 이번에 칠해질 칸 목록에 맞춘다. 목록이 그대로면 아무것도 하지 않는다. */
	void UpdatePreviewBand(const TArray<int32>& AffordableCells);

	/**
	 * 범위 사각형의 머티리얼 인스턴스를 만든다.
	 *
	 * 색과 불투명도는 여기서 넣지 않는다. 그건 머티리얼 인스턴스가 가진다.
	 * C++ 이 색까지 덮어쓰면 아티스트가 MI 에서 바꾼 값이 런타임에 조용히 무시되어,
	 * "머티리얼을 고쳤는데 게임에서는 안 변한다" 가 된다.
	 *
	 * C++ 은 상태(PreviewState)만 넘기고 그 상태를 무슨 색으로 그릴지는 머티리얼이 정한다.
	 * 칸 쪽이 per-instance custom data 로 하는 것과 같은 분담이다.
	 */
	void SetupMaterials();

	/** 차징 진행과 릴리즈 판정. Tick 에서 프리뷰보다 먼저 돈다. */
	void TickCharge(float DeltaSeconds);

	/**
	 * 바닥 프리뷰를 지금 상태에 맞춘다. 모든 기계에서 돌지만 각자 자기 플레이어 것만 그린다.
	 *
	 * 리플리케이트하지 않는다. 남의 조준을 보여주려면 그 값을 상시 올려보내야 하는데,
	 * 얻는 것(칸 겹침 방지)보다 비용이 크다. (차징 붓 기획서 결정 D)
	 */
	void UpdatePreview();

	/** 지금 이 기계의 플레이어에게 프리뷰를 보여줄 상황인가. */
	bool ShouldShowPreview(const UCWWorkerComponent* Worker) const;

	/** 이 기계에서 조종 중인 플레이어의 작업 컴포넌트. 데디케이티드 서버에서는 nullptr 이다. */
	UCWWorkerComponent* FindLocalWorker();

	/**
	 * 범위 사각형을 지금 상태에 맞게 그리거나 숨긴다.
	 *
	 * @param AffordableCount 이번에 실제로 칠해질 칸 수. 0 이면 빨강이다.
	 * @param bPaintShort     페인트가 모자라 끊겼는가. 이때만 주황이다.
	 * @param Level           차징 단계. 음수면 띄울 상황이 아니다.
	 */
	void UpdatePreviewBox(int32 AffordableCount, bool bPaintShort,
		const FVector& Origin, const FVector& Forward, int32 Level);

	/**
	 * 이 단계에서 붓이 덮는 범위의 크기. X = 앞으로 뻗는 길이, Y = 좌우 폭.
	 * 단계가 0 이면(최소 홀드 전) 1단계 크기를 돌려준다. 어디를 겨누는지는 보여줘야 한다.
	 */
	FVector2D GetChargeAreaSize(int32 Level) const;

	/**
	 * 범위 안에 중심이 들어온 칸을 모은다. 캐릭터에서 가까운 순으로 정렬된다.
	 *
	 * 정렬이 중요하다. 페인트가 모자라면 앞에서부터 끊기는데, 순서가 뒤죽박죽이면
	 * 범위 한가운데가 비고 끝만 칠해지는 그림이 나온다.
	 */
	void CollectCellsInArea(const FVector& Origin, const FVector& Forward, const FVector2D& AreaSize,
		TArray<int32>& OutCells) const;

	/**
	 * 지금 그릴 범위의 기준점과 방향. 차징 중이면 얼려둔 값, 아니면 지금 위치에서 계산한 값.
	 *
	 * 차징을 시작하며 얼릴 값을 정하는 자리에서는 쓰면 안 된다. ComputeWorkerFrame 을 쓴다.
	 */
	void GetChargeFrame(const UCWWorkerComponent* Worker, FVector& OutOrigin, FVector& OutForward) const;

	/** 작업자의 지금 위치와 정면으로 범위의 기준을 계산한다. 얼려둔 값을 보지 않는다. */
	void ComputeWorkerFrame(const UCWWorkerComponent* Worker, FVector& OutOrigin, FVector& OutForward) const;

	void BeginCharge(UCWWorkerComponent* Worker);

	/** 확정된 단계만큼 칸을 칠하고 차징을 끝낸다. */
	void ReleaseCharge();

	/** 칸도 페인트도 건드리지 않고 차징을 버린다. (차에 치임 · 취소 · 도구 놓기) */
	void AbortCharge();

	void ResetChargeState();

	/** 차징하던 사람이 아직 조건을 만족하는가. 하나라도 어긋나면 차징을 버린다. */
	bool IsChargingWorkerEligible() const;

	/** 칸 하나를 칠하는 데 드는 페인트. */
	float GetCellPaintCost() const;

	/** 지금 차징하는 사람이 이 기계의 플레이어인가. 연출과 디버그 표시 대상을 가른다. */
	bool IsLocalCharger() const;

	void ShowChargeDebug(const TCHAR* Event, int32 Level, int32 PaintedCount) const;

	UFUNCTION()
	void OnRep_PaintedCells();

public:
	/**
	 * 바닥에 보이는 테두리. 둘레를 쪼갠 칸을 인스턴스로 배치해 만든다.
	 *
	 * 테두리 모양 메시 애셋이 따로 필요 없고, Area 크기를 바꾸면 자동으로 따라온다.
	 * 판정은 부모의 Area 박스가 한다.
	 *
	 * 칸 하나가 인스턴스 하나이고, 인스턴스 인덱스가 곧 칸 인덱스다. 한 번 만든 뒤로는
	 * 인스턴스를 더하거나 빼지 않고 **per-instance custom data 만 바꾼다.**
	 *
	 *   [0] 도색 정도   0 = 가이드(노랑 점선), 1 = 도색 완료(흰색 실선)
	 *   [1] 대시 개수   이 칸 안에 들어가는 점선 주기 수. 칸을 만들 때 한 번 넣고 바꾸지 않는다.
	 *
	 * 점선은 머티리얼이 UV.U 로 그린다. frac(U * [1]) 가 대시 비율보다 작은 곳만 남기고,
	 * [0] 이 1 이면 전부 남긴다. 칠하기 전인지 후인지가 선 모양만 봐도 읽혀야 한다.
	 * 노란 실선은 이미 칠해진 선처럼 보인다.
	 *
	 * 모든 칸에서 U 가 선을 따라가도록 세로 변 칸은 90도 돌려서 놓는다. (MakeCellTransform)
	 * 스케일 축만 바꿔 놓으면 세로 변에서는 U 가 선 폭 방향이 되어 점선이 가로로 잘린다.
	 *
	 * 예전에는 가이드 / 도색 / 프리뷰 / 프리뷰차단을 컴포넌트 4개로 나눠서 위에 겹쳐 덮었다.
	 * 전용 머티리얼 없이도 보이게 하려던 것인데, 칠해진 칸 밑에 가이드 노란색이 그대로
	 * 남아 있는 것이 문제였다. 한 칸은 한 번만 그려져야 한다.
	 *
	 * 그래서 이 컴포넌트의 머티리얼은 **PerInstanceCustomData 를 읽어야 한다.**
	 * 안 읽는 머티리얼을 꽂으면 모든 칸이 한 가지 색으로 납작하게 보인다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UInstancedStaticMeshComponent> BorderMeshes;

	/**
	 * 붓이 쓸고 갈 범위를 바닥에 그리는 사각형. 이 기계의 플레이어에게만 보인다.
	 *
	 * 칸에 표시를 켜는 대신 범위 자체를 도구로 보여준다. 차징할수록 앞으로 길어지고,
	 * 이 안에 중심이 들어온 칸이 칠해진다.
	 *
	 * 사각형은 통째로 한 색이라 per-instance custom data 가 필요 없다. 그래서 칸과
	 * 머티리얼을 공유하지 않는다. 색 파라미터 하나, 불투명도 파라미터 하나면 되는
	 * 훨씬 단순한 머티리얼(M_BorderPreview)을 쓴다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UStaticMeshComponent> PreviewBox;

	/**
	 * 이번 붓질로 실제로 칠해질 구간을 테두리 위에 덧그리는 띠.
	 *
	 * 사각형(PreviewBox)은 "붓이 닿는 영역" 이고 이쪽은 "그래서 칠해지는 자리" 다.
	 * 둘을 나눈 이유: 사각형만 그리면 그게 결과인 줄 알고 보게 되는데, 실제로는 그 안에서
	 * 테두리와 겹치는 부분만 칠해진다. 각도를 잘못 잡으면 사각형은 넓은데 띠가 짧게 나오고,
	 * 플레이어는 그 차이를 보고 스스로 자세를 고친다.
	 *
	 * 칠해질 칸마다 인스턴스를 하나씩 놓는다. 칸보다 넓게 그려서 서로 붙으면 한 줄로 보이고,
	 * 중간에 이미 칠해진 칸이 있으면 그 자리만 비어서 그것도 그대로 정보가 된다.
	 * 모서리도 칸을 따라가므로 저절로 꺾인다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UInstancedStaticMeshComponent> PreviewBandMeshes;

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

	/**
	 * 단계별 붓 범위의 크기(cm). X = 앞으로 뻗는 길이, Y = 좌우 폭. (차징 붓 기획서 2.3)
	 *
	 * 캐릭터 발밑에서 바라보는 방향으로 뻗는 직사각형이고, 차징할수록 커진다.
	 * 이 안에 중심이 들어온 칸이 칠해진다.
	 *
	 * ChargeThresholds 와 개수가 같아야 한다. 모자라면 마지막 값을 계속 쓴다.
	 *
	 * 폭을 테두리 두께(BorderThickness, 기본 12)에 맞추지 않고 넉넉히 주는 이유는 관용이다.
	 * 딱 맞추면 선 위에 정확히 서지 않은 순간 아무것도 안 칠해진다. 대충 맞춰 서도 걸리되,
	 * 각도를 잘 잡을수록 더 많이 걸리는 정도가 적당하다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Charge")
	TArray<FVector2D> ChargeAreaSizes = {
		FVector2D(120.f, 110.f),
		FVector2D(270.f, 120.f),
		FVector2D(430.f, 130.f),
		FVector2D(590.f, 140.f)
	};

	/**
	 * 최대 단계에 닿은 뒤 저절로 나가기까지 기다리는 시간(초).
	 *
	 * 닿자마자 나가면 최대 범위가 어디까지인지 볼 틈이 없다. 이 동안은 최대 범위 프리뷰가
	 * 떠 있고, 그 사이 키를 떼면 평소처럼 그 자리에서 나간다.
	 *
	 * 체온 계수를 곱하지 않은 실제 시간이다. 보고 판단하는 시간이라 더워도 늘어나면 안 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Charge", meta = (ClampMin = "0.0"))
	float FullChargeHoldSeconds = 1.f;

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

	/**
	 * 가이드 점선의 한 주기 길이(cm). 대시 하나와 그 뒤 간격을 합친 길이다.
	 *
	 * 칸 길이가 변마다 조금씩 달라서, 칸마다 이 값에 가장 가까운 정수 개로 나눠 넣는다.
	 * 개수가 정수라야 칸 경계에서 대시가 반토막 나지 않고 옆 칸과 같은 박자로 이어진다.
	 * 대시와 간격의 비율은 머티리얼 인스턴스(M_BorderCell 의 DashRatio)가 정한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide", meta = (ClampMin = "1.0"))
	float DashPitch = 30.f;

	/**
	 * 테두리를 도로면보다 살짝 띄워 Z-파이팅을 막는다. (cm)
	 *
	 * 오염 레이어의 칸 메쉬(ACWWorkZone_Cells::CellHeightOffset, 기본 1)보다 **높아야 한다.**
	 * 같으면 두 면이 같은 높이에서 다투다가 페인트가 선을 덮어버려서,
	 * 어디까지 칠해야 하는지 알려주는 가이드라인이 사라진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	float GuideHeightOffset = 2.f;

	// 범위 사각형을 칸보다 얼마나 더 띄울 것인가. (cm)
	// 칸 위에 얹혀야 "이 칸들이 범위 안에 있다" 로 읽힌다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preview", meta = (ClampMin = "0.01"))
	float PreviewBoxHeightOffset = 1.f;

	// 결과 띠를 테두리보다 몇 배 넓게 그릴 것인가.
	// 칸과 똑같은 폭이면 테두리에 묻혀서 "칠해질 자리" 가 따로 안 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preview", meta = (ClampMin = "1.0"))
	float PreviewBandWidthScale = 2.4f;

	// 결과 띠를 범위 사각형보다 얼마나 더 띄울 것인가. (cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preview", meta = (ClampMin = "0.01"))
	float PreviewBandHeightOffset = 0.5f;

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

	// 범위 사각형의 기준점(월드). 캐릭터 발밑이고, 여기서 앞으로 뻗는다.
	FVector ChargeOriginLocation = FVector::ZeroVector;

	// 범위가 뻗는 방향(월드, 수평). 차징 시작 시점에 얼어붙고 중간에 바뀌지 않는다.
	FVector ChargeForwardDirection = FVector::ForwardVector;

	// 마지막으로 알린 단계. 값이 바뀔 때만 OnChargeLevelChanged 를 부른다.
	int32 LastReportedChargeLevel = 0;

	// ApplyWork 가 마지막으로 온 시각. 이 값으로 릴리즈를 감지한다.
	float LastApplyWorkTime = -1000.f;

	// 릴리즈 모션이 끝나는 시각. 그때까지는 이동이 잠기고 새 차징도 시작되지 않는다.
	float ReleaseEndTime = -1000.f;

	// 최대 단계에 머문 실제 시간(초). FullChargeHoldSeconds 를 넘기면 저절로 나간다.
	float FullChargeElapsed = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UCWWorkerComponent> ChargingWorker;

	// 범위 사각형의 머티리얼. 상태 숫자 하나만 넘긴다.
	//
	// 칸 쪽(BorderMeshes)에는 MID 가 없다. C++ 이 넣을 값이 없기 때문이다.
	// 칸의 상태는 per-instance custom data 로 가고, 색은 머티리얼 인스턴스가 갖는다.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewBoxMaterial;

	// 결과 띠의 머티리얼. 사각형과 같은 부모 머티리얼을 쓰되 인스턴스가 달라서
	// 불투명도를 따로 줄 수 있다. 상태(PreviewState)는 둘 다 같은 값을 받는다.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewBandMaterial;

	// 지금 띠에 떠 있는 칸들. 목록이 그대로면 인스턴스를 다시 만들지 않는다.
	TArray<int32> PreviewBandCells;

	// 마지막으로 머티리얼에 넣은 상태. 값이 그대로면 다시 쓰지 않는다.
	int32 LastPreviewState = -1;

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
