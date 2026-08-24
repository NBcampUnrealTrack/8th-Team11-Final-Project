#pragma once

#include "CoreMinimal.h"
#include "Painting/Zone/CWBaseWorkZone.h"
#include "Painting/Data/CWBrushStroke.h"
#include "CWWorkZone_Cells.generated.h"

class UBoxComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;

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
 * 핵심 아이디어: 칸 하나 = 작은 사각형 메쉬 하나.
 *   - 채워진 칸은 메쉬가 보이고, 빈 칸은 메쉬가 없다.
 *   - 진행도 = 채워진 칸 수 / 전체 칸 수  (지우기 모드는 반대로 계산)
 * 렌더 타깃이나 텍스처를 만들 필요가 없고, 에디터에서 눈으로 바로 확인된다.
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

	/** 이 존이 페인트 오염 레이어인가. (스트라이프 밖에 묻은 페인트를 전부 담는 존) */
	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsPaintSpillLayer() const { return bPaintSpillLayer; }

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

	/** 이 도구가 삐져나간 페인트를 지우는 도구인가. (검은색 롤러) */
	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsCleanupTool(ECWToolType ToolType) const;

	/** 이 도구가 칸을 채우는 도구인가. FillTools 가 비어 있으면 WorkMode 로 판단한다. */
	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsFillTool(ECWToolType ToolType) const;

	/**
	 * 이 도구를 썼을 때 칸이 채워지는가(true) 지워지는가(false).
	 * 붓질을 만드는 곳과 "할 일이 있는지 보는 곳" 이 같은 답을 써야 해서 함수로 뺐다.
	 */
	bool ResolveFillForTool(ECWToolType ToolType) const;

	/**
	 * 바깥 고리가 있는가. (OverspillMargin > 0)
	 *
	 * 정리 도구의 동작 범위를 가른다. 고리가 있으면 고리만, 없으면 격자 전체다.
	 * 고리가 없는데도 고리만 지우게 두면 오염 레이어에서 검은 롤러가 통째로 먹통이 된다.
	 */
	bool HasOverspillRing() const;

	/** 정리 도구로 지울 수 있는 칸이 남아 있는가. 고리 유무에 따라 세는 대상이 다르다. */
	int32 GetCleanableCellCount() const;

	/**
	 * 이 월드 지점의 칸이 칠해져 있는가. 격자 밖이면 false.
	 * 차량이 젖은 페인트를 밟았는지 판정하는 데 쓴다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsFilledAt(const FVector& WorldLocation) const;

	virtual bool CanUseTool(ECWToolType ToolType) const override;

	virtual bool CanWorkNow(ECWToolType ToolType) const override;
	virtual bool IsWorkerInRange(const UCWWorkerComponent* Worker, const FVector& WorkLocation) const override;

	virtual bool HasWorkAt(const UCWWorkerComponent* Worker, const FVector& WorldLocation) const override;

	/** 도색과 정리를 겸하는 존이라 "페인트 정리" 줄을 하나 더 낸다. */
	virtual void GetExtraObjectives(TArray<FCWStageObjective>& OutObjectives) const override;

	/**
	 * 페인트가 말랐는가. 마르면 차량이 밟아도 더 이상 번지지 않는다.
	 * 도색이 완료되고 PaintDryDelay 가 지나면 true 가 된다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsPaintDry() const { return bPaintDry; }

	UFUNCTION(BlueprintPure, Category = "Work")
	int32 GetFilledCellCount() const { return FilledCellCount; }

	/** 진행도로 세는 칸 중 채워진 수. 도색 진행도의 분자다. */
	UFUNCTION(BlueprintPure, Category = "Work")
	int32 GetCountedFilledCount() const { return CountedFilledCount; }

	/**
	 * Area 바깥으로 삐져나간 페인트 칸 수. 0 이면 정리가 끝난 상태다.
	 * HUD 의 "주변 페인트 정리" 목표를 이 값으로 만들 수 있다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	int32 GetSpillCellCount() const { return FilledCellCount - CountedFilledCount; }

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

	/** 격자를 만들고 초기 상태(bStartFilled)를 적용한다. */
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
	 * @return 실제로 바뀐 칸이 있으면 true
	 */
	int32 ApplyStroke(const FCWBrushStroke& Stroke);

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
	 * 내보낼 붓질을 모아둔다. 실제 전송은 FlushOutgoingStrokes 가 한다.
	 *
	 * @param Worker 클라이언트에서 부를 때만 필요하다. 존은 소유자가 없어 서버 RPC 를 직접
	 *               보낼 수 없고, 폰에 붙은 작업 컴포넌트를 거쳐야 한다. 서버면 nullptr.
	 */
	void QueueOutgoingStroke(UCWWorkerComponent* Worker, const FCWBrushStroke& Stroke);

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
	 */
	float GetEffectiveBrushRadius(const UCWWorkerComponent* Worker) const;

	/**
	 * 월드 좌표를 존 로컬 좌표로 옮긴다. (Area 박스 트랜스폼의 역변환)
	 *
	 * 아래 템플릿이 이걸 거쳐 가는 이유: 템플릿 본문이 헤더에 있어서 UBoxComponent 를
	 * 역참조할 수 없다. 전방 선언만 되어 있고, 헤더에 BoxComponent.h 를 끌어오면
	 * 이 헤더를 포함하는 모든 파일이 같이 무거워진다.
	 */
	FVector ToLocalLocation(const FVector& WorldLocation) const;

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
		const float EffectiveRadius = Radius + FMath::Max(CachedCellSize.X, CachedCellSize.Y) * 0.5f;
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
		const int32 MinX = FMath::Max(0,
			FMath::CeilToInt((LocalCenter.X - EffectiveRadius - FirstCellCenter.X) / CachedCellSize.X));
		const int32 MaxX = FMath::Min(FullGridSize.X - 1,
			FMath::FloorToInt((LocalCenter.X + EffectiveRadius - FirstCellCenter.X) / CachedCellSize.X));
		const int32 MinY = FMath::Max(0,
			FMath::CeilToInt((LocalCenter.Y - EffectiveRadius - FirstCellCenter.Y) / CachedCellSize.Y));
		const int32 MaxY = FMath::Min(FullGridSize.Y - 1,
			FMath::FloorToInt((LocalCenter.Y + EffectiveRadius - FirstCellCenter.Y) / CachedCellSize.Y));

		for (int32 CellY = MinY; CellY <= MaxY; ++CellY)
		{
			// 세로 거리는 이 줄 안에서 바뀌지 않는다. 안쪽 반복문 밖으로 뺀다.
			const float DeltaY = (FirstCellCenter.Y + CellY * CachedCellSize.Y) - LocalCenter.Y;
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
				const float DeltaX = (FirstCellCenter.X + CellX * CachedCellSize.X) - LocalCenter.X;
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
	 * @return 실제로 상태가 바뀐 칸이 하나라도 있으면 true
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
	 * (ApplyStroke 가 false 를 반환) 그 붓질을 다시 보내지 않기 때문에, 서버가 놓친 칸은
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

protected:
	// Area 안쪽(=진행도로 세는 영역)의 가로 x 세로 칸 수.
	// 영역 비율에 맞춰 칸이 정사각형이 되게 잡는 게 보기 좋다.
	//
	// 칸 하나가 8~10cm 쯤 되어야 붓 자국처럼 보인다. 20cm 를 넘으면 어떤 머티리얼을 써도
	// 네모가 눈에 띈다. 반대로 칸이 너무 잘면 붓질마다 인스턴스를 전부 다시 만드는 비용이
	// 커진다. 전체 3000칸 근처부터 프레임에 영향이 온다.
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
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Work", meta = (DisplayName = "칸 크기 (cm)"))
	FVector2D CellSizePreview = FVector2D::ZeroVector;

	/**
	 * 지금 설정으로 나오는 전체 칸 수. 바깥 고리까지 포함한다.
	 *
	 * 이 수가 곧 렌더 인스턴스 수다. **20,000 을 넘으면 프레임에 영향이 온다.**
	 * 넘었으면 GridSize 를 줄여 칸을 키우거나, 박스를 줄이면 된다.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Work", meta = (DisplayName = "전체 칸 수 (2만 이하 권장)"))
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

	// 칸을 지우는 도구. (검은색 롤러)
	//
	// 바깥 고리가 있는 존이면 고리 칸만 지운다. 완성된 횡단보도까지 지워지면 안 되기 때문이다.
	// 고리가 없는 존(오염 레이어)이면 격자 전체를 지운다. 그쪽은 격자 전체가 곧 정리 대상이다.
	//
	// 이 도구는 AcceptedTools 에 없어도 존에 들어올 수 있고, 존이 완료된 뒤에도 계속 쓸 수 있다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	TArray<ECWToolType> CleanupTools;

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

	// 바깥 고리 포함, 채워진 칸 전체 수. 메쉬를 그리는 데 쓴다.
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

	// Area 크기와 칸 크기. 붓질 한 번에 수백 번 쓰이는 값이라 RebuildGridLayout 에서 한 번만 잡는다.
	// 런타임에 박스를 늘리면 이 값이 낡는다. 그때는 ResetCells 를 다시 불러야 한다.
	FVector CachedAreaSize = FVector::OneVector;

	FVector2D CachedCellSize = FVector2D(1.f, 1.f);

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
};
