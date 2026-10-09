#pragma once

#include "CoreMinimal.h"
#include "Painting/Zone/CWBaseWorkZone.h"
#include "CWWorkZone_BorderSketch.generated.h"

class ACWBaseTool;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UAnimInstance;
class UAnimMontage;
class UAnimSequence;
class USoundBase;
class UNiagaraSystem;
class UNiagaraComponent;
class ACWWorkZone_Cells;

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

	// 둘레를 한 바퀴 도는 방향이 메시의 U(+) 방향과 같으면 +1, 반대면 -1.
	// 변마다 진행 방향이 달라서(위·오른쪽 변은 +, 아래·왼쪽 변은 -) 스윕 방향을 정할 때 필요하다.
	float RingDirectionSign = 1.f;

	// 이 칸이 X 축 방향으로 누워 있는가. 인스턴스 배율을 어느 축에 줄지 정한다.
	bool bAlongX = false;
};

/**
 * 2단계 테두리용 존. 붓을 차징해서 테두리를 몇 칸씩 그어 나간다. (차징 붓 기획서)
 *
 * 규칙
 *   1. 테두리 직사각형의 둘레를 CellLength 길이의 칸으로 나눈다
 *   2. 라인 테두리 도구를 들고 존 옆에서 작업 키를 누르고 있으면 차징이 시작된다
 *   3. 누른 시간이 길수록 단계가 올라간다 (0.3초 1단계 → 0.7초 2단계 → 1.2초 3단계 → 1.8초 4단계)
 *      칸 수는 ChargeRunCellCounts 가 정한다. 캐릭터 발밑에서 가장 가까운 칸이 시작점이고, 캐릭터가
 *      바라보는 쪽으로 테두리를 따라 **한 줄**로 이어 간다
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
 * 찍는 플레이를 막기 위한 것이다.
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
	int32 GetChargeLevel() const;

	UFUNCTION(BlueprintPure, Category = "Work|Border")
	int32 GetPaintedCellCount() const;

	UFUNCTION(BlueprintPure, Category = "Work|Border")
	bool IsCellPainted(int32 CellIndex) const;

	/** 마지막 단계까지의 차징 진행도. 0 = 안 누름, 1 = 최대 단계. 차징 이펙트 세기에 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Work|FX")
	float GetChargeAlpha() const;

	/**
	 * 지금 놓으면 칠해질 구간의 끝(선두) 위치. 이 기계의 플레이어가 차징 중일 때만 유효하다.
	 * @return 선두가 있으면 true
	 */
	UFUNCTION(BlueprintPure, Category = "Work|FX")
	bool GetChargeTipLocation(FVector& OutTipLocation) const;

	virtual ECWZoneHighlightState GetHighlightStateFor(ECWToolType ToolType) const override;
	virtual bool ShouldDrawDebugHighlightBox() const override { return false; }

protected:
	/**
	 * 차징이 시작됐다. 도구 끝에 붙는 루프 이펙트를 켜는 자리다.
	 * 이 기계의 플레이어가 누를 때만 불린다. 남의 차징은 알려지지 않는다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Work|FX")
	void OnChargeStarted(ACWBaseTool* Tool);

	/**
	 * 차징 단계가 올랐다.
	 *
	 * @param TipLocation 지금 놓으면 칠해질 구간의 끝. 칠할 칸이 없으면 시작 칸이다.
	 * @param bWillPaint  지금 놓으면 실제로 칠해지는가. false 면 헛스윙 연출을 줘야 한다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Work|FX")
	void OnChargeLevelReached(int32 Level, int32 MaxLevel, const FVector& TipLocation, bool bWillPaint);

	/** 차징이 끝났다. 놓았든 중단됐든 불린다. OnChargeStarted 로 켠 것을 끄는 자리다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Work|FX")
	void OnChargeEnded();

	/**
	 * 붓질이 확정돼 선이 쓸려 칠해지기 시작한다. 모든 기계에서, 남이 칠한 것도 불린다.
	 *
	 * @param CellLocations 붓이 지나가는 순서대로의 칸 중심(월드). 이 선을 따라 스플래시를 뿌린다.
	 * @param StartDelays   칸마다 붓이 닿기까지의 시간(초). 머티리얼 쓸림과 같은 값이라 이펙트를
	 *                      이 시차로 터뜨리면 선과 어긋나지 않는다. CellLocations 와 같은 순서다.
	 * @param SweepSeconds  쓸림이 전부 끝나기까지 걸리는 시간.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Work|FX")
	void OnStrokeSwept(const TArray<FVector>& CellLocations, const TArray<float>& StartDelays, float SweepSeconds);

	/** 칸별 안내선이 도색 상태를 표시하므로 전체 사각형 데칼은 겹쳐 그리지 않는다. */
	virtual void OnHighlightStateChanged_Implementation(ECWZoneHighlightState NewState) override;

	/** 퍼센트만으로는 몇 칸이 남았는지 안 보인다. 완료가 안 되는 이유를 가르는 데 그 숫자가 필요하다. */
	virtual FString GetDebugDetailText() const override;

private:
	/** 같은 작업 그룹의 도색 존. 연결이 모호하면 다른 테두리를 밝히지 않는다. */
	const ACWWorkZone_Cells* FindFillHighlightZone() const;

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

	/**
	 * 넘어온 칸들을 붓이 지나가는 순서대로 쓸어 칠한다.
	 *
	 * 칸마다 [2] 시작 시각, [3] 지속 시간, [4] 스윕 방향을 custom data 에 넣는다. 칸 안에서
	 * 선이 번져 나가는 모양은 머티리얼이 Time 으로 스스로 그리므로 이 뒤로는 틱이 필요 없다.
	 * 시작 시각을 칸 길이 누적 / StrokeSweepSpeed 로 어긋나게 줘서, 칸 여러 개가 하나의 붓질로
	 * 이어져 읽힌다. (칸을 하나씩 켜던 방식은 스탬프를 찍은 것처럼 보였다)
	 */
	void BeginRevealCells(const TArray<int32>& OrderedCells);

	/**
	 * 점등 상태를 칠해진 칸 목록에 맞춘다.
	 *
	 * 서버가 예측을 되돌렸으면 켜둔 칸을 즉시 끄고, 남이 칠했거나 늦게 들어와 새로 알게 된
	 * 칸은 쓸어 칠한다. 리플리케이션은 "누가 어떤 순서로 그었는지" 를 알려주지 않으므로
	 * 그쪽은 칸 번호 순으로 쓴다.
	 */
	void SyncRevealToFlags();

	/** 붓이 이 칸을 지나가는 방향이 메시의 U(+) 방향과 같은가. +1 / -1 */
	float ComputeSweepSign(const TArray<int32>& OrderedCells, int32 OrderIndex) const;

	/** 차징 선두 정보를 지금 상태에 맞춘다. 단계가 오른 프레임에만 이벤트를 낸다. */
	void UpdateChargeFX(int32 Level, const TArray<int32>& AffordableCells);

	/** 칸 중심의 월드 좌표. */
	FVector GetCellWorldLocation(int32 CellIndex) const;

	/** 칸 하나를 그릴 인스턴스 트랜스폼. */
	FTransform MakeCellTransform(const FCWBorderCell& Cell) const;

	/** 결과 띠 한 조각을 그릴 인스턴스 트랜스폼. 칸보다 넓고 조금 더 높다. */
	FTransform MakeBandTransform(const FCWBorderCell& Cell) const;

	/** 결과 띠를 이번에 칠해질 칸 목록에 맞춘다. 목록이 그대로면 아무것도 하지 않는다. */
	void UpdatePreviewBand(const TArray<int32>& AffordableCells, int32 Level);

	/** 칸 하나가 4등분(RailSlicesPerCell)된 레일 조각 하나의 트랜스폼. 칸 로컬 기준이다. */
	FTransform MakeRailTransform(const FCWBorderCell& Cell, int32 SliceIndex) const;

	/** 레일 조각 하나의 custom data 를 쓴다. 렌더 갱신은 호출한 쪽이 한 번에 한다. */
	void SetRailSlice(int32 CellIndex, int32 SliceIndex, float RiseStart, float Tier, float CollapseStart, float Crest);

	/**
	 * 새로 닿게 된 칸의 레일을 솟게 하고, 범위 밖으로 빠진 칸은 감춘다.
	 * 이미 솟은 칸은 건드리지 않는다. 그래야 단계가 오를 때 새 구간만 솟는다.
	 *
	 * @param Tier 차징 단계(1~4). 조준 프리뷰는 AimRailTier 를 넘긴다. 머티리얼이 이 값으로 모양을 가른다.
	 */
	void UpdateRail(float Tier, const TArray<int32>& TargetCells);

	/**
	 * 차징하기 전, 지금 누르면 어느 칸부터 칠해지는지 민트색으로 낮게 보여 준다.
	 *
	 * 시작 칸과 방향은 차징을 시작할 때와 같은 방식으로 정한다. 그래야 보이는 칸이 곧 칠해지는 칸이다.
	 * 칠할 칸이 없거나(이미 칠함 / 페인트 없음) 작업 범위 밖이면 아무것도 띄우지 않는다.
	 */
	void UpdateAimPreview(const UCWWorkerComponent* Worker);

	/**
	 * 칸의 레일을 무너뜨린다. 붓질이 지나가는 시각에 맞춰 조각마다 시차를 둔다.
	 * @param bCrest true 면 무너지기 직전 물마루처럼 한 번 더 솟는다. 칠해지는 칸만 쓴다.
	 */
	void CollapseRailCell(int32 CellIndex, float StartTime, float Duration, float SweepSign, bool bCrest);

	/** 차징이 끝난 시점에 아직 서 있는 레일(칠해지지 않는 칸)을 마저 내린다. */
	void FinishRail();

	/** 서 있거나 무너지는 레일을 전부 감춘다. */
	void HideRail();

	/** 차징을 시작하며 와인드업 자세로 간다. 로컬 작업자 전용이다. */
	void StartChargeAnimation(UCWWorkerComponent* Worker);

	/** 와인드업 끝 자세에 닿으면 얼린다. 차징 중 매 프레임 부른다. */
	void TickChargeAnimation();

	/** 얼려 둔 자세를 풀어 휘두르게 한다. 와인드업 도중이면 홀드 자세에 닿을 때까지 미룬다. */
	void ReleaseChargeAnimation();

	/** 최소 홀드 전에 놓았을 때, 당긴 길을 되감아 붓을 거둔다. */
	void RewindChargeAnimation();

	/**
	 * 키를 뗀 뒤의 몽타주를 끝까지 돌본다. 차징이 끝난 뒤에도 돌아야 해서 Tick 에서 부른다.
	 * 미뤄 둔 휘두르기를 내보내고, 되감기를 마무리하고, 잠금이 풀린 뒤 걷기 시작하면 피니시를 끊는다.
	 */
	void TickReleaseAnimation();

	/** 키를 뗀 뒤 지켜보던 몽타주를 놓는다. */
	void ClearReleaseAnimation();

	/** 와인드업을 ChargeAnimWindupSeconds 안에 끝내는 재생 배율. */
	float GetChargeWindupRate() const;

	/** 홀드 자세에서 릴리즈 배율로 풀고 궤적을 켠다. */
	void BeginReleaseSwing(UAnimInstance* AnimInstance, UAnimMontage* Montage);

	/** 휘두르는 동안 붓 소켓에 궤적을 붙여 켠다. */
	void StartReleaseTrail(UAnimInstance* AnimInstance);

	void StopChargeAnimation();

	/** 차징 단계에 맞는 소리를 이 기계에서만 낸다. */
	void PlayChargeLevelSound(int32 Level) const;

	/** 이 기계의 플레이어에게만 들리는 소리를 낸다. 데디케이티드 서버는 들을 사람이 없어 건너뛴다. */
	void PlayLocalSound(USoundBase* Sound, float Volume = 1.f) const;

	/**
	 * 결과 띠의 머티리얼 인스턴스를 만든다.
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

	/** 차징 단계에 맞는 한 줄의 칸 수. 0 단계는 0. */
	int32 GetChargeRunCount(int32 Level) const;

	/**
	 * 시작 칸에서 테두리 고리를 따라 칸을 한 줄로 이어 모은다. 캐릭터에서 가까운 순이다.
	 *
	 * 순서가 중요하다. 페인트가 모자라면 앞에서부터 끊기는데, 순서가 뒤죽박죽이면 한가운데가
	 * 비고 끝만 칠해지는 그림이 나온다.
	 */
	void CollectCellRun(int32 AnchorCell, bool bForward, int32 Count, TArray<int32>& OutCells) const;

	/** 작업자가 바라보는 쪽이 칸 인덱스가 늘어나는 방향인가. 차징을 시작할 때 한 번만 정한다. */
	bool ComputeChargeForward(const UCWWorkerComponent* Worker, int32 AnchorCell) const;

	/** 위치에서 가장 가까운 칸. 높이는 보지 않는다. */
	int32 FindNearestCell(const FVector& WorldLocation) const;

	/** 시작 칸이 이미 칠해져 있으면 진행 방향으로 훑어 첫 빈 칸을 찾는다. */
	int32 FindSnappedAnchor(int32 AnchorCell, bool bForward) const;

	/** 다음 칸을 향하는 수평 방향. */
	FVector GetCellTangent(int32 CellIndex) const;

	/** 인덱스를 고리 범위로 감는다. 음수도 받는다. */
	int32 WrapCellIndex(int32 CellIndex) const;

	void BeginCharge(UCWWorkerComponent* Worker);

	/** 확정된 단계만큼 칸을 칠하고 차징을 끝낸다. */
	void ReleaseCharge();

	/** 이번 릴리즈에 칠해질 칸 목록. 차징 중에만 부른다. */
	void GetChargeTargetCells(TArray<int32>& OutCells) const;

	/** 칸도 페인트도 건드리지 않고 차징을 버린다. (차에 치임 · 취소 · 도구 놓기) */
	void AbortCharge();

	void ResetChargeState();

	/** 차징하던 사람이 아직 조건을 만족하는가. 하나라도 어긋나면 차징을 버린다. */
	bool IsChargingWorkerEligible() const;

	/** 칸 하나를 칠하는 데 드는 페인트. */
	float GetCellPaintCost() const;

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
	 *   [2] 쓸림 시작   이 칸에 붓이 닿는 월드 시각(머티리얼 Time 과 같은 시계). 연출 없이 칠해진
	 *                   칸은 아주 먼 과거(-1000)라 쓸림이 이미 끝난 것으로 읽힌다.
	 *   [3] 쓸림 시간   붓이 이 칸을 지나가는 데 걸리는 시간(초). 0 으로 나누지 않게 하한을 둔다.
	 *   [4] 쓸림 방향   +1 이면 메시 U 가 커지는 쪽으로, -1 이면 작아지는 쪽으로 쓸려 간다.
	 *
	 * 쓸림 진행도 = saturate((Time - [2]) / [3]). [0] 이 1 이어도 이 값이 0 이면 아직 가이드로 보여야 한다.
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
	 * 이번 붓질로 실제로 칠해질 구간을 테두리 위에 덧그리는 띠.
	 *
	 * 지금은 3D 레일(RailMeshes)이 이 역할을 대신해서 BP 에서 숨겨 둔다.
	 *
	 * 칠해질 칸마다 인스턴스를 하나씩 놓는다. 칸보다 넓게 그려서 서로 붙으면 한 줄로 보이고,
	 * 중간에 이미 칠해진 칸이 있으면 그 자리만 비어서 그것도 그대로 정보가 된다.
	 * 모서리도 칸을 따라가므로 저절로 꺾인다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UInstancedStaticMeshComponent> PreviewBandMeshes;

	/**
	 * 차징 중 닿는 구간에 솟아오르는 3D 레일. 이 기계의 플레이어에게만 보인다.
	 *
	 * 칸마다 RailSlicesPerCell 개로 쪼갠 조각이 인스턴스 하나씩이고, 인덱스는
	 * CellIndex * RailSlicesPerCell + SliceIndex 다. 만든 뒤로는 인스턴스를 더하거나 빼지 않고
	 * **per-instance custom data 만 바꾼다.** 솟고 무너지는 모양은 머티리얼이 Time 으로 그린다.
	 *
	 *   [0] 솟기 시작한 월드 시각
	 *   [1] 단계(0 = 감춤, 1~4 = 그 칸이 닿은 차징 단계. 높을수록 진하고 밝다)
	 *   [2] 무너지기 시작하는 월드 시각(아직이면 아주 먼 미래)
	 *   [3] 물마루 여부(1 = 무너지기 전에 한 번 더 솟는다)
	 *   [4] 레일 높이(cm). 머티리얼이 높이를 줄이는 기준이다
	 *
	 * 이 컴포넌트의 머티리얼은 PerInstanceCustomData 를 읽고 WorldPositionOffset 으로 높이를
	 * 그려야 한다. 안 읽으면 모든 조각이 한 높이의 상자로 서 있다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UInstancedStaticMeshComponent> RailMeshes;

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
	 * 단계별로 한 번에 칠하는 칸 수. 인덱스 0 이 1단계다. (차징 붓 기획서 2.3)
	 *
	 * 캐릭터 발밑에서 가장 가까운 칸이 시작점이고, 캐릭터가 바라보는 쪽으로 테두리를 따라 한 줄로
	 * 이어서 칠한다. 마주 보는 두 변에 걸치는 일이 없다. 칸 길이가 100 이면 1칸은 약 1m 다.
	 *
	 * ChargeThresholds 와 개수가 같아야 한다. 모자라면 마지막 값을 계속 쓴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Charge", meta = (ClampMin = "1"))
	TArray<int32> ChargeRunCellCounts = { 1, 2, 4, 6 };

	/**
	 * 시작 칸이 이미 칠해져 있을 때 진행 방향으로 몇 칸까지 훑어 빈 칸을 찾을 것인가.
	 * 범위 안이 전부 칠해져 있으면 원래 칸을 그대로 쓰고, 그 릴리즈는 헛손질이 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Charge", meta = (ClampMin = "0"))
	int32 AnchorSnapCells = 3;

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

	// 휘두르기가 시작된 뒤 이동이 잠기는 시간(초). 패링은 이 동안에도 된다.
	// 휘두르는 구간만 덮는다. 그 뒤 피니시 도중에 움직이면 피니시를 끊고 걷는다.
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

	// 프리뷰(결과 띠, 레일)를 칸보다 얼마나 더 띄울 것인가. (cm)
	// 칸 위에 얹혀야 "이 칸들이 칠해진다" 로 읽힌다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preview", meta = (ClampMin = "0.01"))
	float PreviewBoxHeightOffset = 1.f;

	// 결과 띠를 테두리보다 몇 배 넓게 그릴 것인가.
	// 칸과 똑같은 폭이면 테두리에 묻혀서 "칠해질 자리" 가 따로 안 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preview", meta = (ClampMin = "1.0"))
	float PreviewBandWidthScale = 2.4f;

	// 결과 띠를 프리뷰 기준 높이보다 얼마나 더 띄울 것인가. (cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preview", meta = (ClampMin = "0.01"))
	float PreviewBandHeightOffset = 0.5f;

	/**
	 * 붓이 선을 쓸어 칠하는 속도(cm/s). (차징 붓 기획서 5.5 "싹—")
	 *
	 * 칸을 하나씩 툭 켜면 붓을 그은 것이 아니라 스탬프를 찍은 것처럼 보인다. 그래서 칸마다
	 * 시작 시각을 길이 누적 / 이 속도로 어긋나게 주고, 각 칸 안에서도 선두가 번져 나가게 한다.
	 * 5m 를 약 0.3초에 쓸려면 1600 안팎이다.
	 *
	 * 0 이하면 연출 없이 즉시 칠해진다. 쓸림은 연출일 뿐이라 게임플레이 판정(진행도·페인트·
	 * 완료)은 릴리즈한 프레임에 이미 끝나 있다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|FX", meta = (ClampMin = "0.0"))
	float StrokeSweepSpeed = 1600.f;

	/**
	 * 키를 놓고 쓸림이 시작되기까지의 지연(초). 붓을 휘두르는 모션이 앞으로 나가는 순간에
	 * 페인트가 닿게 맞추는 값이다. 0 이면 놓는 즉시 쓸린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|FX", meta = (ClampMin = "0.0"))
	float ReleaseSweepDelay = 0.1f;

	// 칸 하나를 몇 조각으로 쪼개 레일로 세울 것인가. 많을수록 무너지는 물마루가 부드럽다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Rail", meta = (ClampMin = "1", ClampMax = "8"))
	int32 RailSlicesPerCell = 4;

	// 레일 높이(cm). 한 칸 길이 100 에 비해 낮게 눕혀야 쿼터뷰에서 바닥 위 가이드로 읽힌다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Rail", meta = (ClampMin = "1.0"))
	float RailHeight = 22.f;

	// 레일 폭을 선 두께(BorderThickness)의 몇 배로 할 것인가. 선보다 넓어야 테두리를 덮고 솟아 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Rail", meta = (ClampMin = "1.0"))
	float RailWidthScale = 1.6f;

	// 레일이 선을 따라 이어서 솟는 속도(cm/s). 새로 닿은 구간의 조각들이 이 속도로 차례차례 솟는다.
	// 작을수록 천천히 쭉 자라는 느낌이고, 너무 크면 한꺼번에 솟는 것처럼 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Rail", meta = (ClampMin = "100.0"))
	float RailRiseSpeed = 900.f;

	// 무너지는 데 걸리는 시간(초). 머티리얼의 CollapseDuration 파라미터와 같은 값이어야 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Rail", meta = (ClampMin = "0.05"))
	float RailCollapseSeconds = 0.35f;

	/**
	 * 차징·휘두르기에 쓸 애니메이션. 비워두면 모션 없이 이펙트만 나간다.
	 * 와인드업이 앞에, 휘두르기가 뒤에 이어진 한 덩어리 시퀀스를 넣는다.
	 * 런타임에 다이내믹 몽타주로 감싸 재생하므로 몽타주 애셋은 필요 없다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Anim")
	TObjectPtr<UAnimSequence> ChargeAnimation;

	// 와인드업이 끝나는 시점(시퀀스 안의 초). 차징 중에는 이 자세에서 얼어 있다가 놓으면 여기서부터 풀린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Anim", meta = (ClampMin = "0.0"))
	float ChargeAnimHoldTime = 1.03f;

	// 와인드업 자세까지 가는 데 걸리는 실제 시간(초). 최소 홀드(첫 단계)보다 짧아야 자세가 먼저 잡힌다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Anim", meta = (ClampMin = "0.05"))
	float ChargeAnimWindupSeconds = 0.25f;

	// 놓았을 때 휘두르는 재생 배율. 클수록 "촥" 나가는 속도감이 난다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Anim", meta = (ClampMin = "0.1"))
	float ReleaseAnimPlayRate = 2.2f;

	// AnimBP 의 슬롯 이름. ParrySwing 몽타주와 같은 슬롯을 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Anim")
	FName ChargeAnimSlot = TEXT("DefaultSlot");

	/**
	 * 휘두르는 동안 붓 끝에 붙일 궤적. 비워두면 궤적 없이 휘두른다.
	 * 애니메이션 노티파이로 붙이지 않는다. 홀드에서 재생 위치를 되돌리고 배율을 바꾸는 사이
	 * 노티파이 구간이 어긋나 실제 플레이에서 궤적이 나오지 않았다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Anim")
	TObjectPtr<UNiagaraSystem> ReleaseTrailSystem;

	// 궤적을 붙일 캐릭터 메시 소켓. 붓이 붙는 소켓이고, 위치는 솔 끝이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Anim")
	FName ReleaseTrailSocket = TEXT("ToolSocket_Liner");

	// 소켓 기준 궤적 위치. 솔 끝에서 손잡이 쪽으로 조금 올려 솔 한가운데를 지나게 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Anim")
	FVector ReleaseTrailOffset = FVector(0.f, 0.f, 10.f);

	// 궤적을 켜 두는 길이(시퀀스 안의 초, 홀드 지점부터). 실제 시간은 ReleaseAnimPlayRate 로 나눈 값이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Anim", meta = (ClampMin = "0.05"))
	float ReleaseTrailClipSeconds = 0.4f;

	/**
	 * 차징 단계가 오를 때 나는 소리. 인덱스 0 이 1단계다. 이 기계의 플레이어에게만 들린다.
	 *
	 * 단계 수보다 짧으면 마지막 소리를 계속 쓴다. 소리 하나만 넣고 아래 피치 배열로 음계를
	 * 올려도 되고, 단계마다 다른 소리를 넣어도 된다. 비워두면 소리 없이 이펙트만 나간다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Sound")
	TArray<TObjectPtr<USoundBase>> ChargeLevelSounds;

	// 단계별 피치 배율. 기본값은 도-미-솔-높은 도(장3화음)로 점점 높아진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Sound")
	TArray<float> ChargeLevelPitches = { 1.f, 1.26f, 1.5f, 2.f };

	// 단계별 음량 배율. 높은 단계일수록 조금씩 커진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Sound")
	TArray<float> ChargeLevelVolumes = { 0.7f, 0.8f, 0.9f, 1.f };

	// 붓질이 확정돼 쓸리기 시작할 때 첫 칸 위치에서 나는 소리. 근처에 있는 모두에게 들린다.
	// 쓸림이 끝나는 순간 페이드아웃되므로 가장 긴 붓질보다 넉넉히 긴 소리를 넣는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Sound")
	TObjectPtr<USoundBase> StrokeSweepSound;

	// 쓸림이 끝난 뒤 StrokeSweepSound 가 잦아드는 시간(초). 짧은 붓질은 소리도 짧게 끝난다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Sound", meta = (ClampMin = "0.0"))
	float StrokeSweepFadeOutSeconds = 0.25f;

	/**
	 * 칸 하나가 칠해지는 순간마다 그 칸 위치에서 나는 짧은 소리. 근처에 있는 모두에게 들린다.
	 *
	 * 쓸림 연출이 칸마다 시작하는 시각에 맞춰 차례로 낸다. 칸 수가 단계마다 달라서 소리 개수와 길이도
	 * 저절로 단계를 따라간다. 시각은 각 기계가 받은 칸 목록으로 계산하므로 따로 동기화하지 않는다.
	 * StrokeSweepSound 와 함께 써도 되고, 하나만 써도 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Sound")
	TObjectPtr<USoundBase> StrokeCellSound;

	// 칸이 하나 지날 때마다 올라가는 피치 배율. 0 이면 같은 음이 반복된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Sound", meta = (ClampMin = "0.0"))
	float StrokeCellPitchStep = 0.04f;

	// StrokeCellSound 의 음량 배율.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Sound", meta = (ClampMin = "0.0"))
	float StrokeCellVolume = 1.f;

	// 버튼을 눌러 차징을 시작하는 순간 나는 소리. 붓을 뒤로 빼며 자세를 잡는 느낌이다. 본인에게만 들린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Sound")
	TObjectPtr<USoundBase> ChargeStartSound;

	// 놓아서 실제로 칠해질 때 휘두르는 순간의 소리. 본인에게만 들린다. 페인트가 닿는 소리는 StrokeSweepSound 가 맡는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Sound")
	TObjectPtr<USoundBase> ReleaseSwingSound;

	// 최소 홀드는 채웠지만 칠해진 칸이 없을 때(페인트 부족, 이미 칠한 칸) 나는 맥 빠진 소리. 본인에게만 들린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Sound")
	TObjectPtr<USoundBase> DryReleaseSound;

	// 차징 상태를 화면에 띄운다. 단계 판정을 눈으로 확인하는 용도다.
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

	// 지금 조준 프리뷰로 띄워 둔 칸. 목록이 그대로면 레일을 다시 세우지 않는다.
	TArray<int32> AimCells;

	// 조준 프리뷰가 마지막으로 가리킨 진행 방향. 칸은 같고 방향만 바뀌는 경우를 알아채는 데 쓴다.
	bool bAimForward = true;

	// 이번 차징의 시작 칸. 캐릭터 발밑에서 가장 가까운 칸이고, 차징을 시작할 때 한 번 정한 뒤 바뀌지 않는다.
	int32 ChargeAnchorCell = INDEX_NONE;

	// 시작 칸에서 칸 인덱스가 늘어나는 쪽으로 이어 갈 것인가. 캐릭터가 바라보는 방향으로 정해 얼려 둔다.
	bool bChargeForward = true;

	// 마지막으로 알린 단계. 값이 바뀔 때만 디버그를 띄운다.
	int32 LastReportedChargeLevel = 0;

	// ApplyWork 가 마지막으로 온 시각. 이 값으로 릴리즈를 감지한다.
	float LastApplyWorkTime = -1000.f;

	// 릴리즈 모션이 끝나는 시각. 그때까지는 이동이 잠기고 새 차징도 시작되지 않는다.
	float ReleaseEndTime = -1000.f;

	// 최대 단계에 머문 실제 시간(초). FullChargeHoldSeconds 를 넘기면 저절로 나간다.
	float FullChargeElapsed = 0.f;

	// 차징은 ApplyWork 가 로컬 작업자에게만 시작시키므로, 차징 중이면 늘 이 기계의 플레이어다.
	UPROPERTY(Transient)
	TObjectPtr<UCWWorkerComponent> ChargingWorker;

	// 테두리 자체에 로컬 도색 안내를 입힌다. 기하와 칸 도색 데이터는 그대로 유지한다.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FillHighlightMaterial;

	// 결과 띠의 머티리얼.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewBandMaterial;

	// 지금 띠에 떠 있는 칸들. 목록이 그대로면 인스턴스를 다시 만들지 않는다.
	TArray<int32> PreviewBandCells;

	// 이 기계의 플레이어. 매 프레임 컴포넌트를 찾지 않으려고 들고 있는다.
	UPROPERTY(Transient)
	TObjectPtr<UCWWorkerComponent> CachedLocalWorker;

	// 화면에 실제로 켜진 칸. 칠해진 칸(PaintedCellFlags)을 한 칸씩 따라간다.
	// 순수한 연출 상태라 리플리케이트하지 않는다. 각 기계가 자기 화면을 따라잡는다.
	TArray<uint8> RevealedCellFlags;

	// 레일이 서 있는 칸. 칸 인덱스 → 0/1. 이 기계의 연출 상태라 리플리케이트하지 않는다.
	TArray<uint8> RailCellActive;

	// 무너지기 시작한 칸. 서 있는 레일을 마저 내릴 때 이미 무너지는 칸을 건너뛰는 데 쓴다.
	TArray<uint8> RailCellCollapsing;

	// 이 시각이 지나면 남은 레일을 감춘다. 무너지는 모션이 끝날 때까지 컴포넌트를 살려 두려는 것이다.
	float RailLingerUntil = -1.f;

	// 차징 애니메이션. 다이내믹 몽타주를 들고 있다가 얼리고 푼다.
	TWeakObjectPtr<UAnimInstance> ChargeAnimInstance;
	TWeakObjectPtr<UAnimMontage> ChargeMontage;

	// 와인드업 자세에서 얼렸는가.
	bool bChargeAnimHeld = false;

	// 키를 뗀 뒤에도 끝까지 지켜보는 몽타주. 놓는 순간 ResetChargeState 가 위의 차징 포인터를
	// 비우므로 따로 들고 있는다.
	TWeakObjectPtr<UAnimInstance> ReleaseAnimInstance;
	TWeakObjectPtr<UAnimMontage> ReleaseMontage;

	// 와인드업 도중에 놓아 휘두르기를 홀드 자세까지 미뤄 두었는가.
	bool bReleaseSwingPending = false;

	// 최소 홀드 전에 놓아 와인드업을 되감는 중인가.
	bool bReleaseRewinding = false;

	// 켜 둔 휘두르기 궤적. ReleaseTrailTimer 가 끝나면 끈다.
	TWeakObjectPtr<UNiagaraComponent> ReleaseTrail;
	FTimerHandle ReleaseTrailTimer;

	// 지금 놓으면 칠해질 구간의 끝(월드). GetChargeTipLocation 이 돌려준다.
	FVector ChargeTipLocation = FVector::ZeroVector;

	// ChargeTipLocation 이 지금 유효한가. 차징 중이고 프리뷰가 떠 있을 때만 true 다.
	bool bHasChargeTip = false;

	// 마지막으로 이벤트를 낸 차징 단계. 값이 오를 때만 OnChargeLevelReached 를 낸다.
	int32 LastFxChargeLevel = 0;

	// 단계 소리를 마지막으로 냈을 때 칠할 수 있던 칸 수. 단계가 올라도 이 수가 늘지 않으면 소리를 내지 않는다.
	int32 LastFxAffordableCount = 0;
};
