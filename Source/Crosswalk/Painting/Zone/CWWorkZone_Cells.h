#pragma once

#include "CoreMinimal.h"
#include "Painting/Zone/CWBaseWorkZone.h"
#include "Painting/Data/CWBrushStroke.h"
#include "CWWorkZone_Cells.generated.h"

class UBoxComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraSystem;
class UStaticMeshComponent;
class UTexture2D;

/** 작업이 칸을 채우는 쪽인지, 지우는 쪽인지. */
UENUM(BlueprintType)
enum class ECWCellWorkMode : uint8
{
	// 칸을 채운다. (3단계 도색)
	FillCells,
	// 칸을 지운다. (1단계 청소, 3단계+ 페인트 정리)
	ClearCells
};

/**
 * 영역을 격자로 나눠 칸 단위로 채우거나 지우는 존.
 * 1단계 청소 / 3단계 도색 / 3단계+ 페인트 정리를 이 클래스 하나로 다 만든다.
 *
 * 핵심 아이디어: 영역을 격자로 나눠 칸별 bool 하나만 들고 있는다.
 *   - 진행도 = 채워진 칸 수 / 전체 칸 수  (지우기 모드는 반대로 계산)
 *   - 붓질은 "어디를 얼마만큼" 만 주고받는다. 칸 배열을 통째로 보내지 않는다.
 *
 * 화면에 그리는 방법은 두 가지고, 칸 상태와는 완전히 분리돼 있다.
 *   1. 페인트 필드 (기본, bUsePaintField)
 *      칸 상태를 텍스처로 넘기고 판 하나에 셰이더로 그린다. 격자가 화면에서 사라지고
 *      드로우콜은 칸 수와 무관하게 하나다. 자세한 원리는 bUsePaintField 주석에 있다.
 *   2. 칸 메쉬 (PaintFieldMaterial 이 비었을 때의 대체 경로, 디버그용)
 *      칸 하나가 작은 사각형 메쉬 하나다. 칸이 그대로 눈에 보여서 격자무늬가 된다.
 *      진행도가 안 차는 원인을 찾을 때는 이쪽이 정확하다. (bShowCellMeshes)
 *
 * 격자는 두 겹이다. Area 박스 안쪽이 "점수 영역"이고, OverspillMargin 만큼 바깥으로
 * 더 뻗은 고리가 "삐져나간 영역"이다. 붓은 양쪽 다 칠하지만 진행도는 안쪽만 센다.
 * 그래서 테두리 밖으로 롤러가 나가면 페인트는 묻는데 작업은 진행되지 않는다.
 * 바깥 고리는 CleanupTools(검은색 롤러)로만 지울 수 있다.
 *
 *   OverspillMargin=0 이면 고리가 없다. 청소/정리 존은 그대로 두면 된다.
 *
 * 사용 예시
 *   1단계 청소  : WorkMode=ClearCells, bStartFilled=true   (먼지가 깔려 있고 쓸어서 없앤다)
 *   3단계 도색  : WorkMode=FillCells,  bStartFilled=false, OverspillMargin>0, CleanupTools=[BlackRoller]
 *   3단계+ 정리 : WorkMode=ClearCells, bStartFilled=false  (차량이 흘린 페인트만 지운다)
 */
UCLASS()
class CROSSWALK_API ACWWorkZone_Cells : public ACWBaseWorkZone
{
	GENERATED_BODY()

public:
	ACWWorkZone_Cells();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void BeginPlay() override;

	virtual void Tick(float DeltaSeconds) override;

	virtual void ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds) override;

	virtual void ReceiveBrushStrokes(UCWWorkerComponent* Worker, const TArray<FCWBrushStroke>& Strokes) override;

	/**
	 * 외부에서 칸을 추가한다. 차량이 마르지 않은 페인트를 밟고 지나갈 때 쓴다.
	 * (3단계+ 정리 존에 이걸 호출하면 정리할 대상이 생긴다)
	 *
	 * @param bSpillOnly true 면 Area 바깥 고리에만 찍는다. 채우기 모드 존에 찍을 때 반드시 필요하다.
	 *                   안 그러면 차가 지나갈 때마다 도색 진행도가 올라간다.
	 *
	 * 서버에서만 유효하다. 클라이언트에서 부르면 조용히 무시된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Work")
	int32 AddCellsAt(const FVector& WorldLocation, float Radius, bool bSpillOnly = false);

	/** 칸을 채우는 존인가. (3단계 도색) 지우는 존이면 false. */
	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsFillMode() const { return WorkMode == ECWCellWorkMode::FillCells; }

	virtual bool BlocksPaintSpill() const override;

	/**
	 * 스트라이프의 점수 영역과 겹치는 칸을 격자에서 빼둔다. (오염 레이어 전용)
	 *
	 * 왜 필요한가
	 *   레이어는 도로 전체를 덮고 스트라이프는 그 위에 놓인다. 겹친 자리에 칸을 남겨두면
	 *   같은 지점을 두 존이 각자 기록한다. 스트라이프를 칠하는 동안 레이어에도 오염이 쌓여서
	 *   도색을 끝낼수록 치울 것이 늘어나는 이상한 상태가 된다.
	 *
	 *   빼두면 기획서 6-4 의 "도색 구역 점수 영역에는 오염이 생기지 않는다" 도 저절로 지켜진다.
	 *   그 자리에는 애초에 켤 칸이 없기 때문이다.
	 *
	 * 등록 목록을 보기 때문에 BeginPlay 가 아니라 다음 틱에 돌아야 한다.
	 * 액터의 BeginPlay 순서는 보장되지 않아서, 지금 바로 훑으면 아직 등록 전인 스트라이프를 놓친다.
	 */
	void RefreshSpillMask();

	/**
	 * 다른 존에서 넘어온 붓질을 받는다. (UCWPaintingSubsystem::BroadcastBrushStroke)
	 *
	 * 붓이 두 존에 걸쳐 있을 때 이음새를 없애는 장치다. FindTargetZone 은 언제나 한 존만
	 * 고르기 때문에, 스트라이프가 잡히면 그 밖으로 나간 부분이 아무 데도 기록되지 않는다.
	 *
	 * 받는 쪽이 자기 CanWorkNow 로 다시 판단하므로 잠금 규칙이 저절로 지켜진다.
	 * 도구가 채우는지 지우는지도 받는 존이 스스로 정한다. 보내는 쪽의 판단을 그대로 쓰면
	 * 흰 롤러가 오염 레이어에서 칸을 지우는 일이 생긴다.
	 */
	void ReceiveForwardedBrush(UCWWorkerComponent* Worker, const FVector& Center, float Radius);

	/** 면적 1cm² 당 페인트 소모량. 격자 해상도가 달라도 같은 붓질이면 같은 값이 나오게 한다. */
	float GetPaintCostPerArea() const;

	/** Area 바깥으로 칠할 수 있는 여유 폭(cm). 0 이면 바깥 고리가 없다. */
	UFUNCTION(BlueprintPure, Category = "Work")
	float GetOverspillMargin() const { return OverspillMargin; }

	/** 이 도구가 칸을 채우는 도구인가. FillTools 가 비어 있으면 WorkMode 로 판단한다. */
	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsFillTool(ECWToolType ToolType) const;

	/**
	 * 이 도구를 썼을 때 칸이 채워지는가(true) 지워지는가(false).
	 * 붓질을 만드는 곳과 "할 일이 있는지 보는 곳" 이 같은 답을 써야 해서 함수로 뺐다.
	 */
	bool ResolveFillForTool(ECWToolType ToolType) const;

	/** 도구 목록이 하나도 설정되지 않은 존을 시작할 때 한 번 짚는다. */
	void WarnIfToolSetupMissing() const;

	/**
	 * 바깥 고리가 있는가. (OverspillMargin > 0)
	 *
	 * 정리 도구의 동작 범위를 가른다. 고리가 있으면 고리만, 없으면 격자 전체다.
	 * 고리가 없는데도 고리만 지우게 두면 오염 레이어에서 검은 롤러가 통째로 먹통이 된다.
	 */
	bool HasOverspillRing() const;

	/**
	 * 이 도구의 붓질이 바깥 고리 칸만 건드리는가. 고리가 있는 존에서 정리 도구를 쓸 때다.
	 * 붓질을 만드는 곳과 "할 일이 있는지 보는 곳" 이 같은 답을 써야 해서 함수로 뺐다.
	 */
	bool IsSpillOnlyStroke(ECWToolType ToolType) const { return IsCleanupTool(ToolType) && HasOverspillRing(); }

	/** 정리 도구로 지울 수 있는 칸이 남아 있는가. 고리 유무에 따라 세는 대상이 다르다. */
	int32 GetCleanableCellCount() const;

	/**
	 * 이 월드 지점의 칸이 칠해져 있는가. 격자 밖이면 false.
	 * 차량이 젖은 페인트를 밟았는지 판정하는 데 쓴다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsFilledAt(const FVector& WorldLocation) const;

	/**
	 * 이 도구가 이 존에 들어올 수 있는가.
	 *
	 * 부모와 한 군데가 다르다. 부모는 AcceptedTools 가 비어 있으면 "도구를 가리지 않는다" 로
	 * 읽지만, 칸 존은 세 목록(AcceptedTools · FillTools · CleanupTools)이 전부 비어 있으면
	 * 아무 도구도 받지 않는다. 이유는 구현부 주석에 적어 뒀다.
	 */
	virtual bool CanUseTool(ECWToolType ToolType) const override;

	virtual bool CanWorkNow(ECWToolType ToolType) const override;

	/**
	 * 안내는 CanWorkNow 와 다르게 판단해야 한다.
	 *
	 * 그쪽은 깨끗한 오염 레이어에도 낙서를 시작할 수 있어야 해서 채우기 도구에 대해 완료를
	 * 보지 않는다. 작업 판정으로는 맞지만, 그대로 안내에 쓰면 도색이 끝난 스트라이프가
	 * 흰 롤러를 든 내내 계속 빛난다.
	 */
	virtual ECWZoneHighlightState GetHighlightStateFor(ECWToolType ToolType) const override;
	virtual bool ShouldDrawDebugHighlightBox() const override { return !IsFillMode() && !bHighlightPaintedCells; }

	/** 연결된 실제 테두리 머티리얼에 이 도색 존의 안내 스타일을 적용한다. */
	void ApplyPaintHighlightStyle(UMaterialInstanceDynamic* Material) const;

	/**
	 * 오염 레이어는 Area 박스가 아니라 칠해진 칸을 밝힌다. (기획서 5.6)
	 * 도색은 연결된 BorderSketch의 선 자체를 밝히고, 그 외 존은 박스 데칼을 쓴다.
	 */
	virtual void OnHighlightStateChanged_Implementation(ECWZoneHighlightState NewState) override;

	virtual bool IsWorkerInRange(const UCWWorkerComponent* Worker, const FVector& WorkLocation) const override;

	virtual bool HasWorkAt(const UCWWorkerComponent* Worker, const FVector& WorldLocation) const override;

	/** 도색과 정리를 겸하는 존이라 "페인트 정리" 줄을 하나 더 낸다. */
	virtual void GetExtraObjectives(TArray<FCWStageObjective>& OutObjectives) const override;

	/**
	 * Area 바깥으로 삐져나간 페인트 칸 수. 0 이면 정리가 끝난 상태다.
	 * HUD 의 "주변 페인트 정리" 목표를 이 값으로 만들 수 있다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	int32 GetSpillCellCount() const { return FilledCellCount - CountedFilledCount; }

	/**
	 * 체온을 반영한 실제 붓 반경.
	 *
	 * 이 존은 시간이 아니라 붓이 덮는 면적으로 진행해서, 넘어오는 시간을 줄여도 느려지지 않는다.
	 * 그래서 붓 자체를 줄인다. (체온 기획서 4.2)
	 *
	 * 계수를 반경에 그대로 곱한다. 면적 기준으로 맞추려면 제곱근을 씌워야 하지만(계수 0.2 → 반경 45%),
	 * 그러면 눈에 보이는 붓 크기가 거의 안 변해서 패널티가 걸린 줄을 모른다.
	 *
	 * ForEachCellInRadius 가 판정에 칸 크기의 절반을 더해주므로, 반경이 아무리 작아져도
	 * 커서 아래 칸은 항상 칠해진다. 붓이 먹통이 되지는 않는다.
	 *
	 * 붓질을 만드는 곳과 "할 일이 있는지 보는 곳"이 같은 값을 써야 해서 함수로 뺐다.
	 *
	 * 월드 cm 단위이며, 칸 크기 보정까지 포함한 프리뷰는 GetPaintedWorldRadius를 쓴다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	float GetEffectiveBrushRadius(const UCWWorkerComponent* Worker) const;

	/**
	 * 이 붓질이 실제로 칠하는 반경을 **월드 단위(cm)** 로 돌려준다.
	 * 조준 표시기가 그리는 원의 반경이 이 값이다.
	 *
	 * ForEachCellInRadius와 같이 체온을 반영한 붓 반경에 월드 칸 크기의 절반을 더한다.
	 * 스테이지의 X/Y 배율이 달라도 붓 반경 자체에 배율을 다시 곱하지 않는다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	float GetPaintedWorldRadius(const UCWWorkerComponent* Worker) const;

protected:
	virtual void OnProgressChanged(float NewProgress) override;

	/** 디버그 줄에 "칠한 칸 / 전체 칸" 과 삐져나온 칸 수를 덧붙인다. */
	virtual FString GetDebugDetailText() const override;

private:
	/**
	 * 젖은 페인트를 밟은 차량을 찾고, 이미 묻은 차량이 지나는 자리에 자국을 남긴다.
	 * 서버에서만 돌고, bStainsVehicles 를 켠 존만 타이머를 건다. (기획서 3.2 "차량이 밟아서 튄 페인트")
	 */
	void UpdateVehicleStains();

	/** 차량 확인 타이머를 켜고 끈다. 이미 원하는 상태면 아무것도 하지 않는다. */
	void SetVehicleStainTimerEnabled(bool bEnabled);

	/**
	 * 도색 완료 여부를 보고 마름 대기를 시작하거나 취소한다.
	 * 진행도가 바뀔 때마다 불린다. 서버에서만 유효하다.
	 */
	void RefreshPaintDryState();

	/** 마름 대기가 끝났다. 이 시점부터 차량에 페인트가 묻지 않는다. */
	void HandlePaintDried();

	/** 차량을 찾을 반경. 존 크기에 자국 반경만큼 여유를 더한다. */
	float GetVehicleSearchRadius() const;

	/** 격자를 만들고 초기 상태(bStartFilled, bStartSpillFilled)를 적용한다. */
	void BuildGrid();

	/** 칸 배열을 시작 상태로 되돌린다. 진행도는 건드리지 않는다. (에디터 미리보기와 공용) */
	void ResetCells();

	/** Area 와 GridSize, OverspillMargin 으로 전체 격자 크기와 점수 영역 위치를 다시 계산한다. */
	void RebuildGridLayout();

	/** CellMaterial 을 지정했으면 칸 메쉬에 입힌다. */
	void ApplyCellMaterial();

	/** 도색 영역 경계선을 Area 박스에 맞춰 다시 만든다. 에디터에서 박스를 조절하면 바로 따라온다. */
	void RefreshAreaBorder();

	/**
	 * 붓질 한 번을 이 기계에 적용한다. 칸 상태 → 진행도까지 처리한다.
	 *
	 * 메쉬는 여기서 건드리지 않는다. 바뀐 칸만 표시해 두고 프레임 끝에 한 번에 반영한다.
	 * (FlushCellMeshUpdates) 한 프레임에 여러 명이 칠하거나 차량 자국이 겹쳐도 렌더 상태는 한 번만 더러워진다.
	 *
	 * @return 실제로 바뀐 칸 수
	 */
	int32 ApplyStroke(const FCWBrushStroke& Stroke);

	/**
	 * 붓질 하나가 칸을 실제로 뒤집은 자리에 페인트 튀기는 이펙트를 낸다.
	 *
	 * ApplyStroke 에서 부르니 서버·칠한 본인·남의 화면 세 경로가 모두 여기를 지난다.
	 * 그릴 화면이 없는 데디케이티드 서버만 스스로 걸러낸다.
	 */
	void SpawnPaintStrokeFX(const FCWBrushStroke& Stroke);

	/**
	 * 칠하거나 지운 칸 수만큼 작업자의 도구에서 페인트를 뺀다. (기획서 5-3)
	 *
	 * 칸 수로 계산하는 이유: 진행도로 계산하면 바깥 고리가 공짜가 된다. 고리는 진행도에
	 * 잡히지 않으므로, 삐져나가게 칠하나 정확히 칠하나 총액이 같아져 낭비에 대가가 없어진다.
	 * 칸 수로 세면 고리에 묻은 칸도 그대로 비용이 되고, 그걸 검은색 롤러로 지울 때 또 든다.
	 */
	void ConsumePaintForCells(UCWWorkerComponent* Worker, int32 ChangedCellCount) const;

	/**
	 * 서버가 확정한 붓질을 전원에게 뿌린다.
	 *
	 * 칸 배열 자체는 실시간으로 리플리케이트하지 않는다. 수백~수만 개 bool 을 통째로 보내는 대신
	 * "어디를 얼마만큼 칠했다" 만 보내면 각자 같은 그림에 도달한다.
	 * 놓치면 그 자리만 영영 비므로 Reliable 이어야 한다.
	 *
	 * 붓질 하나마다 부르지 않고 StrokeSendInterval 만큼 모아서 배열로 보낸다.
	 * 매 프레임 보내면 4명이 동시에 칠할 때 초당 240개의 Reliable 패킷이 커넥션마다 쌓인다.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ApplyBrushes(const TArray<FCWBrushStroke>& Strokes);

	/**
	 * 완료 판정이 난 존의 점수 영역에 빈 칸이 남아 있으면 마저 채운다. 서버에서만 판단한다.
	 *
	 * 왜 필요한가
	 *   CompleteThreshold 가 1 보다 낮아서(기본 0.99) 구석 몇 칸이 비어도 완료로 친다.
	 *   그런데 완료된 존은 작업 입력을 받지 않으므로, 그 칸들은 이제 칠할 방법이 없다.
	 *   화면에는 다 칠한 횡단보도에 구멍이 뚫린 채로 영원히 남는다.
	 *
	 *   진행도만 1 로 올리면 숫자와 그림이 따로 논다. 칸을 실제로 채워서 둘을 맞춘다.
	 *   (자국 지우기 쪽의 ACWPaintDecalField::RemoveRemainingThreshold 와 같은 취지다)
	 *
	 * 지우기 모드는 대상이 아니다
	 *   오염 레이어처럼 도중에 할 일이 생기는 존은 평소 진행도가 1 근처다. 그대로 두면
	 *   차가 자국을 하나 남긴 순간 이미 완료 판정이라 저절로 지워진다. 치울 일이 사라진다.
	 *
	 * 진행도가 바뀔 때마다 불리므로 조건을 싸게 거른다.
	 */
	void TryAutoCompleteCounted();

	/**
	 * 자동 완성을 전원에게 알린다.
	 *
	 * 좌표를 싣지 않는다. "점수 영역을 전부 채운다" 는 격자만 같으면 어느 기계에서든 같은 결과라,
	 * 붓질처럼 위치를 주고받을 이유가 없다. 놓치면 그 기계만 영영 덜 칠해진 채로 남으므로 Reliable 이다.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_CompleteCounted();

	/** 점수 영역의 빈 칸을 전부 채우고 화면·진행도를 맞춘다. 모든 머신이 각자 부른다. */
	void FillRemainingCountedCells();

	/**
	 * 내보낼 붓질을 모아둔다. 실제 전송은 FlushOutgoingStrokes 가 한다.
	 *
	 * @param Worker 클라이언트에서 부를 때만 필요하다. 존은 소유자가 없어 서버 RPC 를 직접
	 *               보낼 수 없고, 폰에 붙은 작업 컴포넌트를 거쳐야 한다. 서버면 nullptr.
	 */
	void QueueOutgoingStroke(UCWWorkerComponent* Worker, const FCWBrushStroke& Stroke);

	/**
	 * 월드 좌표를 존 로컬 좌표로 옮긴다. (Area 박스 트랜스폼의 역변환)
	 *
	 * 아래 템플릿이 이걸 거쳐 가는 이유: 템플릿 본문이 헤더에 있어서 UBoxComponent 를
	 * 역참조할 수 없다. 전방 선언만 되어 있고, 헤더에 BoxComponent.h 를 끌어오면
	 * 이 헤더를 포함하는 모든 파일이 같이 무거워진다.
	 */
	FVector ToLocalLocation(const FVector& WorldLocation) const;

	/** 실제 칸 선택과 프리뷰가 공유하는 월드 반경. 붓 반경에 칸 크기 보정을 더한다. */
	float GetCellSelectionWorldRadius(float BrushWorldRadius) const;

	/** 붓이 닿는 곳에 이 존의 칸이 하나라도 있는가. 칸 상태는 보지 않는다. (작업 범위 판정) */
	bool HasAnyCellInRadius(const FVector& WorldCenter, float Radius, bool bSpillOnly) const;

	/** 이 붓질이 뒤집을 수 있는 칸이 하나라도 있는가. 실제로 칠하지는 않는다. */
	bool HasChangeableCellInRadius(const FVector& WorldCenter, float Radius, bool bNewFilled, bool bSpillOnly) const;

	/**
	 * 붓 반경 안의 칸을 훑는다. Visitor 가 false 를 돌려주면 즉시 멈춘다.
	 *
	 * 붓질을 적용하는 쪽(SetCellsInRadius)과 할 일이 있는지 보는 쪽(HasChangeableCellInRadius)이
	 * 반드시 같은 범위·같은 판정을 써야 한다. 한쪽만 고치면 "존은 잡히는데 칠이 안 되는"
	 * 상태가 생기고, 그러면 체온만 오르고 작업은 진행되지 않는다.
	 */
	template <typename VisitorType>
	void ForEachCellInRadius(const FVector& WorldCenter, float Radius, bool bSpillOnly, VisitorType&& Visitor) const
	{
		if (CellFilled.Num() == 0 || CachedCellSize.X <= KINDA_SMALL_NUMBER || CachedCellSize.Y <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		// 칸 중심만 보면 붓이 칸의 대부분을 덮고도 그 칸은 안 칠해진다.
		// 게다가 CellPadding 이 음수라 칸 메쉬가 격자보다 크게 그려져서, 이웃 칸의 메쉬가
		// 빈 칸을 덮어버린다. 그래서 화면은 빈틈 없이 칠해졌는데 진행도는 안 차는 상황이 생긴다.
		//
		// 칸 크기의 절반을 반경에 더해 "칸의 절반 이상이 덮이면 칠해진 것"으로 본다.
		//
		// 여기서 쓰는 모든 길이는 월드 cm 다. Radius 가 월드 값이라 그쪽에 기준을 맞춘다.
		// 로컬 좌표로 재면 액터 스케일이 붓 모양을 축마다 다르게 늘려서, 세로로 그을 때와
		// 가로로 그을 때 칠해지는 폭이 달라진다. (CachedAreaScale 주석 참고)
		const float EffectiveRadius = GetCellSelectionWorldRadius(Radius);
		const float RadiusSquared = FMath::Square(EffectiveRadius);

		// 월드 좌표를 존 로컬로 한 번만 옮긴다.
		//
		// 예전에는 칸마다 GetCellWorldLocation 을 불렀다. 그 안에서 박스 크기를 다시 묻고,
		// 나눗셈을 하고, 행렬 변환을 한 번씩 했다. 붓질 한 번에 그게 칸 개수만큼 돌았다.
		// 로컬 좌표에서 재면 변환은 한 번이면 되고, IsFilledAt 과도 같은 기준을 쓰게 된다.
		const FVector LocalCenter = ToLocalLocation(WorldCenter);
		const FVector2D FirstCellCenter = GetFirstCellCenter();

		// 붓이 닿을 수 있는 칸 범위만 훑는다. 격자 전체를 도는 것과 결과는 같고 비용만 다르다.
		// (붓 반경 20cm 에 칸 10cm 면 5x5 정도다. 예전에는 매번 1600칸을 전부 돌았다)
		//
		// 이 사각형은 로컬 좌표로 잡아야 한다. 월드 반경을 축마다 스케일로 나눠 로컬로 되돌린다.
		const float LocalRadiusX = EffectiveRadius / CachedAreaScale.X;
		const float LocalRadiusY = EffectiveRadius / CachedAreaScale.Y;

		const int32 MinX = FMath::Max(0,
			FMath::CeilToInt((LocalCenter.X - LocalRadiusX - FirstCellCenter.X) / CachedCellSize.X));
		const int32 MaxX = FMath::Min(FullGridSize.X - 1,
			FMath::FloorToInt((LocalCenter.X + LocalRadiusX - FirstCellCenter.X) / CachedCellSize.X));
		const int32 MinY = FMath::Max(0,
			FMath::CeilToInt((LocalCenter.Y - LocalRadiusY - FirstCellCenter.Y) / CachedCellSize.Y));
		const int32 MaxY = FMath::Min(FullGridSize.Y - 1,
			FMath::FloorToInt((LocalCenter.Y + LocalRadiusY - FirstCellCenter.Y) / CachedCellSize.Y));

		for (int32 CellY = MinY; CellY <= MaxY; ++CellY)
		{
			// 세로 거리는 이 줄 안에서 바뀌지 않는다. 안쪽 반복문 밖으로 뺀다.
			// 스케일을 곱해 월드 거리로 바꾼 뒤에 재야 반경과 단위가 맞는다.
			const float DeltaY = ((FirstCellCenter.Y + CellY * CachedCellSize.Y) - LocalCenter.Y) * CachedAreaScale.Y;
			const float DeltaYSquared = DeltaY * DeltaY;

			for (int32 CellX = MinX; CellX <= MaxX; ++CellX)
			{
				const int32 CellIndex = GetCellIndex(CellX, CellY);

				// 스트라이프에 가려진 칸은 이 격자에 없는 것으로 친다. (오염 레이어 전용)
				// 그 자리는 도색 시스템 소관이라 오염이 쌓여서도, 지워져서도 안 된다.
				if (CellMasked.IsValidIndex(CellIndex) && CellMasked[CellIndex])
				{
					continue;
				}

				const bool bCounted = IsCountedCell(CellX, CellY);

				// 검은색 롤러는 삐져나간 페인트만 지운다. 완성된 횡단보도 위를 지나가도 안전하다.
				if (bSpillOnly && bCounted)
				{
					continue;
				}

				// 높이는 무시하고 평면 거리로만 판정한다. (탑뷰 클릭 감각 우선)
				const float DeltaX = ((FirstCellCenter.X + CellX * CachedCellSize.X) - LocalCenter.X) * CachedAreaScale.X;
				if (DeltaX * DeltaX + DeltaYSquared > RadiusSquared)
				{
					continue;
				}

				if (!Visitor(CellIndex, bCounted))
				{
					return;
				}
			}
		}
	}

	void FlushOutgoingStrokes(bool bForce);

	/**
	 * 중심점 주변 반경 안의 칸들을 켜거나 끈다.
	 * @return 실제로 상태가 바뀐 칸 수
	 */
	int32 SetCellsInRadius(const FVector& WorldCenter, float Radius, bool bNewFilled, bool bSpillOnly);

	/** 이 칸이 진행도로 세는 영역(Area 안쪽) 안에 있는가. */
	bool IsCountedCell(int32 CellX, int32 CellY) const;

	/** 칸 하나의 크기(cm). Area 를 GridSize 로 나눈 값이고, 바깥 고리도 같은 크기를 쓴다. */
	FVector2D GetCellSize() const { return CachedCellSize; }

	/**
	 * (0,0) 칸 중심의 컴포넌트 로컬 좌표.
	 *
	 * 붓질 판정 / 메쉬 배치 / 디버그 표시 / 좌표 역산이 전부 이 기준 하나를 써야 한다.
	 * 하나라도 어긋나면 "화면은 칠해졌는데 진행도가 안 찬다" 가 된다.
	 */
	FVector2D GetFirstCellCenter() const;

	/** 칸 중심의 컴포넌트 로컬 좌표. */
	FVector2D GetCellLocalLocation(int32 CellX, int32 CellY) const;

	/** 이 월드 지점이 어느 칸인가. 격자 밖이면 INDEX_NONE. (GetCellLocalLocation 의 역산) */
	int32 FindCellIndexAt(const FVector& WorldLocation) const;

	/**
	 * 붓질 좌표를 네트워크로 나갈 때와 같은 정밀도로 미리 맞춘다.
	 *
	 * 붓질 좌표는 FVector_NetQuantize 로 나가면서 1cm 단위로 반올림된다. 그런데 붓질을 찍는
	 * 본인 기계는 커서의 정확한 실수 좌표로 로컬 적용을 해서, 다른 기계와 최대 0.5cm 어긋난다.
	 *
	 * 칸이 크면 무시할 차이지만 이 존의 칸은 한 변이 4cm 도 안 될 수 있다. 그러면 붓 원의
	 * 경계에 걸친 칸들이 기계마다 다르게 뒤집힌다. 게다가 본인 기계는 이미 칠한 것으로 알아서
	 * (ApplyStroke 가 0 을 반환) 그 붓질을 다시 보내지 않기 때문에, 서버가 놓친 칸은
	 * 영영 안 채워진다. 칠한 사람 기계만 조금씩 더 칠해진 상태로 굳는다.
	 *
	 * 그래서 로컬에 적용하기 전에 먼저 반올림한다. 모든 기계가 같은 입력을 적용하게 된다.
	 */
	static FVector QuantizeStrokeLocation(const FVector& WorldLocation);

	/**
	 * 남은 칸을 눈에 보이게 그린다.
	 *   빨강 = 아직 안 칠한 점수 영역 칸 (도색이 덜 끝난 자리)
	 *   주황 = 바깥 고리로 삐져나간 칸 (검은색 롤러로 지워야 하는 자리)
	 */
	void ShowDebugCells() const;

	/**
	 * 격자 전체를 인스턴스로 깔아 둔다. 인스턴스 인덱스 = 칸 인덱스다.
	 *
	 * 예전에는 붓질마다 ClearInstances 후 채워진 칸만 다시 만들었다. 그러면 칸 하나가 바뀌어도
	 * 인스턴스 버퍼를 통째로 다시 만들어 올리게 되고, 많이 칠할수록 느려진다.
	 * 지금은 격자가 바뀔 때만 여기를 부르고, 붓질은 바뀐 칸의 스케일만 건드린다.
	 */
	void RebuildAllCellInstances();

	/** 칸 하나의 인스턴스 트랜스폼. 빈 칸은 스케일 0 이라 화면에서 사라진다. */
	FTransform GetCellInstanceTransform(int32 CellX, int32 CellY, bool bFilled) const;

	/** 이 칸을 다음 메쉬 갱신 대상으로 표시한다. 같은 칸이 여러 번 들어와도 한 번만 처리된다. */
	void QueueCellMeshUpdate(int32 CellIndex);

	/** 모아둔 칸 갱신을 인스턴스에 반영한다. 렌더 상태는 여기서 한 번만 더럽힌다. */
	void FlushCellMeshUpdates();

	/** 이 기계가 칸 메쉬를 만들 필요가 있는가. 데디케이티드 서버는 그리지 않으므로 필요 없다. */
	bool ShouldBuildCellMeshes() const;

	/**
	 * 이 기계가 페인트 필드를 그리는가.
	 *
	 * 머티리얼이 안 꽂혀 있으면 false 다. 그러면 칸 메쉬 방식으로 되돌아간다.
	 * 판 하나에 기본 머티리얼이 입혀져 존 전체가 회색으로 덮이는 것보다는 네모라도 보이는 편이 낫다.
	 */
	bool ShouldBuildPaintField() const;

	/**
	 * 페인트 필드를 격자 구성에 맞춰 다시 잡는다. 텍스처 · 판 크기 · 머티리얼 파라미터까지 전부.
	 *
	 * 격자가 바뀌는 자리(OnConstruction, BuildGrid)에서만 부른다. 붓질은 텍셀만 건드린다.
	 */
	void RefreshPaintField();

	/**
	 * 다 칠한 존을 셰이더의 솔리드 경로로 넘긴다. (M_PaintField 의 SolidFill)
	 *
	 * 필드는 칸 상태를 bilinear 로 읽어 임계값에서 자른다. 그래서 다 칠한 존이라도 가장자리가
	 * 마지막 칸 중심에서 (1 - EdgeThreshold) 칸 나간 자리에서 끝나고, 그 자리가 도메인 워프
	 * 진폭만큼 흔들린다. 스트라이프 테두리와 겹치는 폭이 그 흔들림보다 얇으면 도로가 점점이 비친다.
	 *
	 * 솔리드 경로는 칸 상태 대신 점수 영역(텍스처 G)으로 자르고 워프를 끈다. 경계가 고정값이
	 * 되므로 겹침 폭을 계산으로 보장할 수 있고, 칸 크기나 액터 스케일을 바꿔도 안 깨진다.
	 */
	void RefreshPaintFieldSolidState(float NewProgress);

	/**
	 * 지금 표시 상태를 페인트 필드 셰이더에 반영한다. (bHighlightPaintedCells 전용)
	 *
	 * OnHighlightStateChanged 에서만 부르면 안 된다. 격자 텍스처가 다시 만들어질 때
	 * PaintFieldMID 가 nullptr 로 끊기는데, 그 사이 상태가 안 바뀌면 다시 켜줄 사람이 없어
	 * 발광이 영영 꺼진 채로 남는다. 그래서 MID 를 다시 물리는 자리에서도 같이 부른다.
	 */
	void RefreshPaintFieldHighlight();

	/**
	 * 칸 상태를 담을 텍스처를 만든다. 크기가 그대로면 아무것도 하지 않는다.
	 *
	 * 텍스처는 격자보다 가로세로 한 칸씩 크다. 그 테두리 텍셀을 0 으로 남겨 두면
	 * 셰이더가 격자 밖을 "안 칠한 곳" 으로 읽어서, 칠한 자리의 끝이 판 경계에서
	 * 직선으로 잘리지 않고 자연스럽게 빠진다. (TA_Clamp 라 테두리 값이 밖으로 이어진다)
	 */
	void RebuildFieldTexture();

	/** 격자 전체를 텍스처에 다시 쓴다. 격자를 새로 만들거나 스냅샷을 통째로 적용했을 때 쓴다. */
	void WriteAllFieldTexels();

	/**
	 * 칸 하나를 텍셀에 쓰고 더티 사각형을 넓힌다. 업로드는 하지 않는다.
	 * (FlushFieldTextureUpdates 가 프레임 끝에 한 번에 올린다)
	 */
	void WriteFieldTexel(int32 CellX, int32 CellY, bool bFilled);

	/** 더티 사각형만 GPU 텍스처로 올린다. 한 프레임에 한 번이면 된다. */
	void FlushFieldTextureUpdates();

	/** 늦게 들어온 플레이어에게 넘길 칸 상태가 낡았다고 표시한다. 서버에서만 유효하다. */
	void MarkCellSnapshotDirty();

	/** 낡았고 간격이 지났으면 칸 상태 스냅샷을 다시 만든다. 서버에서만 유효하다. */
	void RebuildCellSnapshotIfDue();

	/** 받은 스냅샷을 칸 배열에 적용한다. 격자가 아직 없으면 아무것도 하지 않는다. */
	void TryApplyCellSnapshot();

	UFUNCTION()
	void OnRep_CellSnapshot();

	/**
	 * 할 일이 남아 있으면 틱을 켜고, 없으면 끈다.
	 *
	 * 평소에는 틱이 꺼져 있다. 존이 레벨에 수십 개 깔려도 아무도 안 칠하는 동안에는 비용이 0 이다.
	 */
	void RefreshTickEnabled();

	/** 칸 개수를 세서 진행도를 다시 계산한다. */
	void RefreshProgress();

	float CalculateProgress() const;

	FVector GetCellWorldLocation(int32 CellX, int32 CellY) const;

	// 인덱스는 바깥 고리를 포함한 전체 격자 기준이다. GridSize 가 아니라 FullGridSize 로 나눈다.
	FORCEINLINE int32 GetCellIndex(int32 CellX, int32 CellY) const { return CellY * FullGridSize.X + CellX; }

public:
	// 칸 하나를 그리는 메쉬. 인스턴스 하나가 칸 하나다. (격자는 부모의 Area 박스를 나눈다)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UInstancedStaticMeshComponent> CellMeshes;

	// 도색 영역의 경계선. 인스턴스 4개가 Area 박스의 네 변이다.
	//
	// 이게 없으면 삐져나간 페인트를 알아볼 수가 없다. 바깥 고리에 묻은 페인트나 안쪽에 칠한
	// 페인트나 똑같이 생겨서, 기준선이 보여야 "이 선 밖은 지워야 할 것" 으로 읽힌다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UInstancedStaticMeshComponent> AreaBorderMeshes;

	/**
	 * 칠한 자리를 그리는 판. 격자 전체를 덮는 판 하나다. (bUsePaintField 를 켰을 때)
	 *
	 * 이게 칸 메쉬를 대신한다. 칸 상태는 텍스처로 넘기고 경계는 셰이더가 만들기 때문에,
	 * 격자가 몇 칸이든 드로우콜은 하나다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UStaticMeshComponent> PaintFieldMesh;

protected:
	// Area 안쪽(=진행도로 세는 영역)의 가로 x 세로 칸 수.
	// 영역 비율에 맞춰 칸이 정사각형이 되게 잡는 게 보기 좋다.
	//
	// 칸 하나가 8~10cm 쯤 되어야 붓 자국처럼 보인다. 20cm 를 넘으면 어떤 머티리얼을 써도
	// 네모가 눈에 띈다. 칸 수의 실질적인 상한은 칸 상태 스냅샷 크기다. (BuildGrid 참고)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	FIntPoint GridSize = FIntPoint(40, 20);

	/**
	 * 지금 설정으로 나오는 칸 하나의 크기(cm). 읽기 전용이고, 박스나 GridSize 를 만지면 바로 갱신된다.
	 *
	 * 에디터에서 크기를 맞출 때 쓰라고 띄운다. 예전에는 PIE 를 돌려야만 알 수 있어서,
	 * 박스를 조금 늘릴 때마다 플레이해 보고 되돌아오는 일이 반복됐다.
	 *
	 * 8~10cm 면 붓 자국처럼 보이고, 20cm 를 넘으면 어떤 머티리얼을 써도 네모가 눈에 띈다.
	 * 오염 레이어는 스트라이프의 1~1.5배로 맞추면 경계에서 이음새가 티나지 않는다.
	 *
	 * 이 값은 **월드 크기**다. 액터 스케일이 1 이 아니면 Area 박스 크기를 GridSize 로 나눈
	 * 값과 다르다. 두 축이 크게 다르면 칸이 찌그러진 것이니 GridSize 를 다시 잡아야 한다.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Work", meta = (DisplayName = "칸 크기 (cm)"))
	FVector2D CellSizePreview = FVector2D::ZeroVector;

	/**
	 * 지금 설정으로 나오는 전체 칸 수. 바깥 고리까지 포함한다.
	 *
	 * 칸 하나가 칸 상태 스냅샷의 비트 하나라, 이 수가 Reliable 로 나가는 스냅샷 크기를 정한다.
	 * 스냅샷이 8KB 를 넘으면 BuildGrid 가 경고한다. 그때는 GridSize 를 줄여 칸을 키우거나, 박스를 줄이면 된다.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Work", meta = (DisplayName = "전체 칸 수"))
	int32 CellCountPreview = 0;

	// Area 바깥으로 몇 cm 까지 칠할 수 있는가. 여기 묻은 페인트는 진행도에 포함되지 않고,
	// CleanupTools 로 지워야 하는 "삐져나간 페인트" 가 된다. 0 이면 Area 밖으로는 칠해지지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "0.0"))
	float OverspillMargin = 0.f;

	/**
	 * 칸을 "채우는" 도구를 직접 지정한다. 비워두면 WorkMode 로 판단한다. (기존 존들은 비워 두면 된다)
	 *
	 * 왜 필요한가
	 *   WorkMode 하나가 두 가지를 동시에 정하고 있었다. "진행도를 어떻게 세는가" 와
	 *   "도구가 칸을 채우는가 지우는가" 다. 오염 레이어는 그 둘이 서로 반대여야 한다.
	 *   흰 롤러로는 칠해지고(채움), 검은 롤러로는 지워지고, 진행도는 "얼마나 깨끗한가"(ClearCells) 다.
	 *   WorkMode 만으로는 이 조합을 표현할 수 없다.
	 *
	 *   그래서 WorkMode 는 진행도 계산에만 쓰고, 채움/지움은 이 목록과 CleanupTools 가 정한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	TArray<ECWToolType> FillTools;

	// 삐져나간 페인트 정리를 체크리스트에 별도 줄로 낼 때 쓸 이름. ("페인트 정리")
	//
	// 이 존은 도색과 정리를 겸한다. Area 안쪽 칸은 ObjectiveName("도색 작업") 의 진행도가 되고,
	// 바깥 고리에 묻은 페인트는 이 이름으로 한 줄이 더 생긴다. 바깥 고리가 깨끗해지면 완료다.
	// 줄이 서는 위치는 이 존의 WorkOrder 바로 다음이다. (도색 작업 → 페인트 정리)
	//
	// 비워두면 줄이 생기지 않는다. 청소/테두리 존은 비워 두면 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	FText CleanupObjectiveName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	ECWCellWorkMode WorkMode = ECWCellWorkMode::FillCells;

	/**
	 * 이 존이 페인트 오염 레이어인가. 스테이지에 배치하는 넓은 정리 존이 이걸 켠다.
	 *
	 * 켜면 두 가지가 달라진다.
	 *   1. 다른 스트라이프의 점수 영역과 겹치는 칸을 격자에서 뺀다 (RefreshSpillMask)
	 *   2. 붓질 전달의 대상이 된다. 스트라이프 밖으로 나간 페인트가 여기로 모인다
	 *
	 * 권장 설정
	 *   WorkMode      = ClearCells      (진행도 = 얼마나 깨끗한가)
	 *   bStartFilled  = false
	 *   FillTools     = [WhiteRoller]   (흰 롤러로 칠하면 더러워진다)
	 *   CleanupTools  = [BlackRoller]   (검은 롤러로 지운다)
	 *   OverspillMargin = 0             (격자 전체가 정리 대상)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	bool bPaintSpillLayer = false;

	/**
	 * 면적당 페인트 소모량. 0 이면 PaintCostFull 과 점수 영역 면적으로 자동 계산한다.
	 *
	 * 스트라이프는 0 으로 두면 된다. "이 구역을 다 칠하면 PaintCostFull" 이라는 기획서 정의가
	 * 그대로 단가가 되기 때문이다.
	 *
	 * 오염 레이어는 그 계산이 맞지 않는다. 레이어는 도로 전체라 면적이 훨씬 넓어서
	 * 자동 계산하면 단가가 바닥까지 떨어지고 낙서가 사실상 공짜가 된다.
	 * 그래서 레이어도 0 으로 두면 RefreshSpillMask 가 스트라이프의 단가를 그대로 물려받는다.
	 * 직접 넣은 값이 있으면 그쪽이 우선한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "0.0"))
	float PaintCostPerArea = 0.f;

	// 시작할 때 모든 칸이 채워져 있는지. (1단계 청소는 true)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	bool bStartFilled = false;

	// 시작할 때 바깥 고리(=삐져나간 영역)가 페인트로 채워져 있는지.
	//
	// 이전 조가 흘려 놓고 간 페인트처럼, 도색을 시작하기 전부터 정리할 대상이 있는 상황을 만든다.
	// 점수 영역은 건드리지 않아서 도색 진행도는 그대로 0 에서 시작하고, CleanupTools 로 고리를
	// 지워야 CleanupObjectiveName 줄이 완료된다.
	// OverspillMargin 이 0 이면 고리 자체가 없어서 아무 효과도 없다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	bool bStartSpillFilled = false;

	// 붓 반경(cm). 한 번에 이 반경 안의 칸들이 처리된다.
	// 체온이 오르면 이 값에 계수가 곱해져 실제 반경이 줄어든다. (bScaleBrushByWorkSpeed)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "1.0"))
	float BrushRadius = 20.f;

	// 체온이 오를 때 붓 반경을 줄일 것인가. (체온 기획서 4.2)
	//
	// 이 존은 시간이 아니라 붓이 덮는 면적으로 진행해서, 넘어오는 시간을 줄여도 느려지지 않는다.
	// 그래서 붓 자체를 줄인다. 끄면 이 존만 체온에 따른 작업속도 저하를 받지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	bool bScaleBrushByWorkSpeed = true;

	// 캐릭터가 격자 평면에서 이 높이 안에 있어야 작업할 수 있다(cm).
	//
	// 이 존은 평면 거리 제약이 없다. 붓이 닿는 칸이 있으면 어디에 서 있어도 작업이 된다.
	// 그런데 칸 판정은 높이를 무시하므로(ForEachCellInRadius) 이걸 안 보면 다리 위나
	// 건물 안에서도 아래 도로가 칠해진다. 층이 갈리는 것만 막는 값이라 넉넉하게 잡으면 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "0.0"))
	float WorkerHeightTolerance = 300.f;

	// 아직 안 칠해진 점수 영역 칸을 빨간 상자로 그린다.
	//
	// "화면은 다 칠해졌는데 진행도가 안 찬다" 를 확인하는 용도다. 칸 메쉬는 CellPadding 이
	// 음수라 격자보다 크게 그려지고 페인트 머티리얼의 가장자리도 부드러워서, 빈 칸이
	// 이웃 메쉬에 덮여 눈에 안 보일 수 있다. 이걸 켜면 남은 칸의 위치가 그대로 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Debug")
	bool bShowDebugUnfilledCells = false;

	// 격자 구성(전체 크기, 점수 영역 원점, 칸 크기)을 진행도 줄 뒤에 같이 찍는다.
	// 서버와 클라이언트의 칸 상태가 어긋날 때, 붓질 문제인지 격자 문제인지 가르는 데 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Debug")
	bool bShowDebugGridLayout = false;

	/**
	 * 페인트 필드를 쓰는 중에도 칸 메쉬를 같이 그린다.
	 *
	 * 필드는 셰이더가 경계를 만들기 때문에, 화면에 보이는 자리와 실제로 칠해진 칸이
	 * 정확히 같지 않다. 진행도가 안 차는 원인을 찾을 때 둘을 겹쳐 보면 바로 갈린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Debug")
	bool bShowCellMeshes = false;

	// 바깥 고리로 삐져나간 칸을 주황 상자로 그린다.
	//
	// 체크리스트에는 "페인트 정리 N칸 남음" 이라고 뜨는데, 삐져나간 페인트는 도로 위에
	// 얇게 묻어 있어서 어디에 남았는지 눈으로 찾기가 어렵다. 검은색 롤러로 지우러 갈 때
	// 위치를 바로 알 수 있게 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Debug")
	bool bShowDebugSpillCells = false;

	// 칸 메쉬를 도로보다 살짝 띄워 Z-파이팅을 막는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	float CellHeightOffset = 1.f;

	// 칸 메쉬 사이 여백 비율. 0.05 면 칸끼리 5% 떨어져 격자가 보인다.
	//
	// 음수면 칸끼리 겹친다. 기본값이 음수인 이유: 가장자리가 부드러운 페인트 머티리얼과
	// 같이 쓰면 겹친 부분이 이어져서 네모 칸이 아니라 하나의 칠한 자국처럼 보인다.
	// CellMaterial 을 안 꽂았다면 0 으로 두는 편이 낫다. 딱딱한 사각형이 겹쳐 봐야 두꺼워질 뿐이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "-0.5", ClampMax = "0.5"))
	float CellPadding = -0.3f;

	// 도색 영역 경계선을 그릴 것인가. 끄면 선이 사라진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Border")
	bool bShowAreaBorder = true;

	// 경계선 두께(cm). 선의 중심이 Area 경계에 놓이므로 안팎으로 절반씩 걸친다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Border", meta = (ClampMin = "0.1"))
	float AreaBorderThickness = 6.f;

	// 경계선을 도로에서 띄우는 높이(cm).
	//
	// 칸 메쉬(CellHeightOffset)보다 높아야 한다. 같거나 낮으면 페인트가 선을 덮어버려서,
	// 정작 삐져나간 페인트가 많을수록 기준선이 안 보이는 상황이 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Border")
	float AreaBorderHeightOffset = 2.f;

	// 경계선에 입힐 머티리얼. 비워두면 AreaBorderMeshes 컴포넌트에 꽂혀 있는 것을 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Border")
	TObjectPtr<UMaterialInterface> AreaBorderMaterial;

	// 칸에 입힐 페인트 머티리얼. 비워두면 CellMeshes 컴포넌트에 이미 꽂혀 있는 것을 쓴다.
	//
	// 가장자리가 원형으로 부드럽게 빠지는 Masked 머티리얼이어야 도색이 깔끔하게 보인다.
	// 사각형 그대로면 칸이 그대로 눈에 보여서 격자무늬가 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Visual")
	TObjectPtr<UMaterialInterface> CellMaterial;

	/**
	 * 칸 메쉬 대신 판 하나에 셰이더로 그린다. 격자가 화면에서 사라진다.
	 *
	 * 어떻게 사라지는가
	 *   칸 상태를 텍스처로 넘기고 bilinear 로 읽으면 칸 경계가 보간으로 녹아 0~1 연속 필드가 된다.
	 *   거기에 노이즈로 좌표를 흔들고 임계값으로 자르면, 축에 맞춰 정렬되지 않은 출렁이는
	 *   외곽선이 나온다. 안쪽은 플랫하게 채워져서 페인트를 칠한 자국처럼 읽힌다.
	 *
	 * 대가
	 *   경계의 해상도가 칸 크기에 묶인다. 칸이 8cm 면 그보다 잔 붓 디테일은 표현되지 않는다.
	 *   붓 반경(BrushRadius, 기본 20cm)이 칸보다 훨씬 크므로 실사용에는 걸리지 않는다.
	 *
	 * PaintFieldMaterial 이 비어 있으면 이 값과 무관하게 칸 메쉬 방식으로 돈다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Visual")
	bool bUsePaintField = true;

	/**
	 * 페인트 필드에 입힐 머티리얼. (M_PaintField)
	 *
	 * 규약이 있어서 아무 머티리얼이나 꽂으면 안 된다.
	 *   CellField      Texture2D  R = 칠했나, G = 점수 영역인가. 이 클래스가 런타임에 넣는다
	 *   FieldWorldSize Vector     판의 실제 크기(cm). 노이즈 진폭을 UV 로 환산하는 데 쓴다
	 *   WarpAmountCm   Scalar     외곽선을 흔드는 폭(cm). 칸 크기에서 자동 계산해 넣는다
	 * 나머지 파라미터(PaintColor, RimWidth, StreakTex 등)는 머티리얼 인스턴스에서 잡는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Visual")
	TObjectPtr<UMaterialInterface> PaintFieldMaterial;

	/**
	 * 외곽선을 흔드는 폭을 칸 크기의 몇 배로 잡을 것인가. (WarpAmountCm 으로 들어간다)
	 *
	 * 칸 크기에 비례해서 넣는 이유: 격자 해상도가 다른 존들이 같은 값을 써도 비슷하게 보인다.
	 * 0 이면 흔들지 않아서 칸 경계의 보간만 남는다. 그러면 격자 방향이 희미하게 드러난다.
	 * 1 을 넘기면 칠한 자리가 원래 칸에서 눈에 띄게 벗어나 진행도와 그림이 어긋나 보인다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Visual", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float PaintFieldWarpCells = 0.7f;

	/**
	 * 하이라이트를 Area 박스가 아니라 "칠해진 칸" 으로 그린다. (기획서 5.6)
	 *
	 * 오염 레이어가 이걸 켠다. 도로 전체를 덮는 단일 존이라 Area 박스를 그리면 화면 전체가
	 * 켜지고 아무 정보도 되지 않는다. 플레이어가 알아야 하는 것은 존의 위치가 아니라
	 * 칠해진 칸이 어디 있는가다. 스테이지 마무리에 맵 오브젝트 사이에 남은 페인트를
	 * 찾는 것이 이 옵션이 푸는 문제다.
	 *
	 * 모드로 자동 판단하지 않고 스위치로 두는 이유는 bHighlightable 과 같다.
	 * 청소 존도 "고리 없는 지우기 모드" 라서 자동 판단으로는 같이 걸린다.
	 *
	 * 켜면 PaintFieldMaterial 이 Highlight 스칼라 파라미터를 갖고 있어야 한다.
	 * (bUsePaintField 가 꺼져 있거나 머티리얼이 없으면 아무 일도 일어나지 않는다)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Highlight")
	bool bHighlightPaintedCells = false;

	/** 미완성 테두리 가이드와 구분하는 도색 작업 안내색. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Highlight|Painting")
	FLinearColor PaintHighlightColor = FLinearColor(0.02f, 0.8f, 0.65f, 1.f);

	/** 사선 무늬의 이동 속도. 0이면 멈춘다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Highlight|Painting", meta = (ClampMin = "0.0"))
	float PaintHighlightScrollSpeed = 0.65f;

	/** 사선 무늬 밀도. 값이 클수록 촘촘해진다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Highlight|Painting", meta = (ClampMin = "0.01"))
	float PaintHighlightStripeDensity = 0.5f;

	// 붓이 칸을 바꾼 자리에 터질 파티클. 비워 두면 이펙트가 나오지 않는다.
	//
	// 도색(칠하기)과 오염 정리(지우기) 양쪽에서 나간다. 바깥 고리 전용 붓질
	// (차량 자국·검은색 롤러)만 제외된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|FX")
	TObjectPtr<UNiagaraSystem> PaintStrokeFX;

	// 파티클을 다시 터트리기까지의 최소 간격(초).
	//
	// 붓질은 칠하는 동안 매 프레임 생긴다. 간격을 두지 않으면 초당 수십 개가 겹쳐
	// 한 덩어리로 보이고, GPU 시뮬 인스턴스도 그만큼 늘어난다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|FX", meta = (ClampMin = "0.0"))
	float PaintStrokeFXInterval = 0.04f;

	// 파티클 크기 배수. 발자국용 이미터를 가져다 쓰는 만큼 붓 크기에 맞출 수단이 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|FX", meta = (ClampMin = "0.01"))
	float PaintStrokeFXScale = 1.f;

	// 차량이 이 존의 젖은 페인트를 밟고 지나가면 자국을 남기는가. (3단계 도색 존에서 켠다)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Vehicle")
	bool bStainsVehicles = false;

	// 자국을 남길 존. 자기 자신을 지정해도 된다.
	//
	// 대상이 채우기 모드(도색 존)면 자국은 Area 바깥 고리에만 찍힌다.
	// 점수 영역까지 채우면 차가 지나갈 때마다 도색이 저절로 완성되기 때문이다.
	// 그래서 자기 자신을 가리키면 "횡단보도 가장자리에 페인트가 묻는" 그림이 되고,
	// 넓은 정리 존(지우기 모드)을 가리키면 "도로에 자국이 길게 남는" 그림이 된다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Work|Vehicle")
	TObjectPtr<ACWWorkZone_Cells> VehicleStainZone;

	// 차량이 남기는 자국 하나의 반경(cm).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Vehicle", meta = (ClampMin = "1.0"))
	float VehicleStainRadius = 60.f;

	// 한 번 묻은 페인트가 바퀴에서 떨어질 때까지의 시간(초). 이 시간만큼 자국이 이어진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Vehicle", meta = (ClampMin = "0.0"))
	float VehicleStainDuration = 1.5f;

	// 차량 위치를 확인하고 자국을 찍는 간격(초). 짧을수록 자국이 촘촘하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Vehicle", meta = (ClampMin = "0.02"))
	float VehicleStainInterval = 0.1f;

	// 도색이 완료되고 이 시간이 지나면 페인트가 마른다. (초)
	// 마른 뒤에는 차량이 밟아도 페인트가 묻지 않는다.
	//
	// 이게 없으면 차량이 5~8초마다 계속 새 자국을 만들어서 "페인트 정리" 목표가
	// 끝나지 않고, 전 목표 동시 완료를 요구하는 스테이지 클리어가 뜨지 않는다.
	// 0 이면 완료 즉시 마른다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Vehicle", meta = (ClampMin = "0.0"))
	float PaintDryDelay = 5.f;

	// 모아둔 붓질을 내보내는 간격(초). 0 이면 매 프레임 보낸다.
	//
	// 붓질은 Reliable 로 나간다. 놓치면 그 자리만 영영 안 칠해지기 때문이다. 그런데 매 프레임
	// 보내면 4명이 동시에 칠할 때 커넥션마다 초당 240개가 쌓인다. 0.066(초당 15회)이면
	// 패킷 수가 1/4 로 줄고, 붓 궤적은 연속이라 점 몇 개를 묶어도 그림은 같다.
	//
	// 로컬 화면은 이 값과 무관하게 매 프레임 그대로 칠해진다. 늦춰지는 것은 전송뿐이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Network", meta = (ClampMin = "0.0"))
	float StrokeSendInterval = 0.066f;

	// 한 패킷에 담을 붓질 최대 개수. 넘으면 간격을 기다리지 않고 바로 내보낸다.
	// 차량 여러 대가 동시에 자국을 남길 때 목록이 무한정 자라는 것을 막는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Network", meta = (ClampMin = "1"))
	int32 MaxStrokesPerPacket = 16;

	// 칸 상태 스냅샷을 다시 만드는 간격(초).
	//
	// 이 값은 "도중에 들어온 플레이어가 처음 화면을 그릴 때" 쓰는 밑그림이다. 붓질 멀티캐스트는
	// 그때 접속해 있던 사람에게만 가기 때문에, 늦게 들어온 사람은 진행도만 70%로 보이고
	// 화면은 백지인 상태가 된다. 스냅샷이 그 구멍을 막는다.
	//
	// 실시간 그림은 붓질이 이미 맞추고 있으므로 초당 한 번이면 충분하다. 칸 하나가 비트
	// 하나라서 1600칸이면 200바이트다. 격자를 아주 크게 잡을 거면 이 값을 올려라.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Network", meta = (ClampMin = "0.0"))
	float CellSnapshotInterval = 1.f;

	// 이 기계가 마지막으로 칠한 뒤 이만큼 지나야 받은 스냅샷을 적용한다(초).
	//
	// 스냅샷은 서버가 만든 시점의 그림이다. 방금 내가 칠한 칸이 아직 안 들어가 있을 수
	// 있어서, 칠하는 도중에 덮으면 그 칸이 잠깐 사라졌다 돌아오는 되감기가 보인다.
	// 붓을 놓고 잠깐 쉬면 그때 맞춰진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Network", meta = (ClampMin = "0.0"))
	float SnapshotApplyIdleDelay = 1.f;

private:
	// 칸별 채움 상태. 이게 이 존의 전부다. 바깥 고리를 포함한 전체 격자 크기다.
	TArray<bool> CellFilled;

	/**
	 * 격자에서 빼둔 칸. 오염 레이어에서만 채워진다. (RefreshSpillMask)
	 *
	 * 비어 있으면 마스킹이 없는 것이다. 일반 존은 이 배열을 쓰지 않으므로 메모리도 안 든다.
	 */
	TArray<bool> CellMasked;

	// 바깥 고리까지 포함한 전체 격자 크기. GridSize + 여백 칸 수 * 2.
	FIntPoint FullGridSize = FIntPoint(1, 1);

	// 전체 격자 안에서 점수 영역이 시작되는 칸 좌표. 여백이 0 이면 (0, 0).
	FIntPoint CountedOrigin = FIntPoint::ZeroValue;

	// 바깥 고리 포함, 채워진 칸 전체 수. 바깥 고리 칸 수와 치울 칸 수를 셀 때 쓴다.
	int32 FilledCellCount = 0;

	// 점수 영역 안에서 채워진 칸 수. 진행도는 이것만 본다.
	int32 CountedFilledCount = 0;

	// 지우기 모드에서 진행도의 분모로 쓴다. 지금까지 최대로 채워졌던 점수 영역 칸 수.
	int32 PeakCountedFilledCount = 0;

	// 페인트가 묻은 차량과 남은 시간(초). 서버에서만 채운다.
	TMap<TWeakObjectPtr<AActor>, float> StainedVehicles;

	FTimerHandle VehicleStainTimerHandle;

	// 페인트가 말랐는가. 서버에서만 쓴다. 자국을 찍는 것도 서버뿐이라 리플리케이트하지 않는다.
	bool bPaintDry = false;

	FTimerHandle PaintDryTimerHandle;

	// 도색 이펙트를 마지막으로 터트린 시각. PaintStrokeFXInterval 을 재는 기준이다.
	// 기계마다 따로 세는 값이라 리플리케이트하지 않는다.
	float LastPaintStrokeFXTime = -1000.f;

	// Area 크기와 칸 크기. 붓질 한 번에 수백 번 쓰이는 값이라 RebuildGridLayout 에서 한 번만 잡는다.
	// 런타임에 박스를 늘리면 이 값이 낡는다. 그때는 ResetCells 를 다시 불러야 한다.
	FVector CachedAreaSize = FVector::OneVector;

	FVector2D CachedCellSize = FVector2D(1.f, 1.f);

	/**
	 * Area 컴포넌트의 월드 스케일(XY). 로컬 길이를 월드 cm 로 바꾸는 계수다.
	 *
	 * 격자는 로컬 좌표로 깔고 메쉬도 로컬로 배치한다. 거기까지는 스케일을 몰라도 된다.
	 * 하지만 붓 반경 · OverspillMargin · 페인트 단가 · 워프 진폭은 "몇 cm" 라는 월드 값이라,
	 * 로컬 값과 섞이는 지점마다 이 계수로 환산해야 한다.
	 */
	FVector2D CachedAreaScale = FVector2D(1.f, 1.f);

	// 칸 하나의 실제 월드 크기(cm). CachedCellSize * CachedAreaScale 이다.
	// 디테일 패널에 띄우는 값도, 붓 판정에 쓰는 값도 이쪽이다.
	FVector2D CachedCellWorldSize = FVector2D(1.f, 1.f);

	// 이번 프레임에 상태가 바뀐 칸. 프레임 끝에 한 번에 인스턴스로 반영한다.
	TArray<int32> PendingCellMeshUpdates;

	// 같은 칸이 한 프레임에 여러 번 뒤집혀도 목록에 한 번만 들어가게 하는 중복 방지용.
	TBitArray<> PendingCellMeshUpdateMask;

	// 아직 안 보낸 붓질.
	TArray<FCWBrushStroke> PendingOutgoingStrokes;

	// 클라이언트에서 붓질을 올릴 때 거쳐 갈 작업 컴포넌트. 서버에서는 쓰지 않는다.
	TWeakObjectPtr<UCWWorkerComponent> PendingStrokeWorker;

	float LastStrokeSendTime = -1000.f;

	// 칸 상태를 비트로 눌러 담은 것. 늦게 들어온 플레이어가 밑그림으로 쓴다.
	UPROPERTY(ReplicatedUsing = OnRep_CellSnapshot)
	TArray<uint8> CellSnapshot;

	bool bCellSnapshotDirty = false;

	// 스냅샷을 이미 한 번 적용했는가. 클라이언트에서만 쓴다.
	bool bCellSnapshotApplied = false;

	float LastCellSnapshotBuildTime = -1000.f;

	// 이 기계에서 마지막으로 붓질한 시각. 스냅샷을 덮어도 되는지 판단하는 데 쓴다.
	float LastLocalStrokeTime = -1000.f;

	// 칸 상태를 담아 셰이더로 넘기는 텍스처. 텍셀 하나가 칸 하나다. (테두리 한 줄은 항상 0)
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> PaintFieldTexture;

	// PaintFieldMesh 에 입힌 머티리얼 인스턴스. 존마다 텍스처가 달라서 인스턴스도 존마다 따로다.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PaintFieldMID;

	// 텍스처의 CPU 쪽 사본. BGRA 순서로 텍셀당 4바이트다.
	//
	// 이걸 들고 있는 이유: 부분 업로드는 원본 배열이 있어야 한다. 매번 GPU 텍스처를
	// 락해서 읽어오는 것보다 CPU 에 사본을 두는 편이 싸다. 40x20 격자면 3.7KB 다.
	TArray<uint8> FieldPixels;

	// 텍스처 크기. 격자보다 가로세로 한 칸씩 크다. (RebuildFieldTexture)
	FIntPoint FieldTextureSize = FIntPoint::ZeroValue;

	// 이번 프레임에 바뀐 텍셀을 감싸는 사각형. Max 는 미포함이다.
	FIntRect FieldDirtyRect = FIntRect();

	bool bFieldDirty = false;

	// 자동 완성을 처리하는 중인가. 칸을 채우면 진행도가 다시 바뀌어 OnProgressChanged 가
	// 또 불리므로, 그 재진입에서 같은 판단을 반복하지 않게 막는다.
	bool bAutoCompleting = false;
};
