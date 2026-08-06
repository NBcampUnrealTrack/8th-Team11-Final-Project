#pragma once

#include "CoreMinimal.h"
#include "Painting/Zone/CWBaseWorkZone.h"
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

	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void BeginPlay() override;

	virtual void ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds) override;

	virtual void ReceiveBrushStroke(const FVector& WorldLocation, float Radius, bool bFilled, bool bSpillOnly) override;

	/**
	 * 외부에서 칸을 추가한다. 차량이 마르지 않은 페인트를 밟고 지나갈 때 쓴다.
	 * (3단계+ 정리 존에 이걸 호출하면 정리할 대상이 생긴다)
	 *
	 * 서버에서만 유효하다. 클라이언트에서 부르면 조용히 무시된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Work")
	void AddCellsAt(const FVector& WorldLocation, float Radius);

	/** 이 도구가 삐져나간 페인트를 지우는 도구인가. (검은색 롤러) */
	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsCleanupTool(ECWToolType ToolType) const;

	/**
	 * 이 월드 지점의 칸이 칠해져 있는가. 격자 밖이면 false.
	 * 차량이 젖은 페인트를 밟았는지 판정하는 데 쓴다.
	 */
	UFUNCTION(BlueprintPure, Category = "Work")
	bool IsFilledAt(const FVector& WorldLocation) const;

	virtual bool CanUseTool(ECWToolType ToolType) const override;

	virtual bool CanWorkNow(ECWToolType ToolType) const override;

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

private:
	/**
	 * 젖은 페인트를 밟은 차량을 찾고, 이미 묻은 차량이 지나는 자리에 자국을 남긴다.
	 * 서버에서만 돌고, bStainsVehicles 를 켠 존만 타이머를 건다. (기획서 3.2 "차량이 밟아서 튄 페인트")
	 */
	void UpdateVehicleStains();

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

	/**
	 * 붓질 한 번을 이 기계에 적용한다. 칸 상태 → 메쉬 → 진행도까지 한 번에 처리한다.
	 * @param bSpillOnly Area 바깥 칸만 건드린다. 검은색 롤러가 완성된 횡단보도를 지우지 못하게 막는다.
	 * @return 실제로 바뀐 칸이 있으면 true
	 */
	bool ApplyBrush(const FVector& WorldCenter, float Radius, bool bNewFilled, bool bSpillOnly);

	/**
	 * 서버가 확정한 붓질을 전원에게 뿌린다.
	 *
	 * 칸 배열 자체는 리플리케이트하지 않는다. 수백 개 bool 을 통째로 보내는 대신
	 * "어디를 얼마만큼 칠했다" 만 보내면 각자 같은 그림에 도달한다.
	 * 놓치면 그 자리만 영영 비므로 Reliable 이어야 한다. (바뀐 칸이 있을 때만 나간다)
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ApplyBrush(FVector_NetQuantize WorldCenter, float Radius, bool bNewFilled, bool bSpillOnly);

	/**
	 * 중심점 주변 반경 안의 칸들을 켜거나 끈다.
	 * @return 실제로 상태가 바뀐 칸이 하나라도 있으면 true
	 */
	bool SetCellsInRadius(const FVector& WorldCenter, float Radius, bool bNewFilled, bool bSpillOnly);

	/** 이 칸이 진행도로 세는 영역(Area 안쪽) 안에 있는가. */
	bool IsCountedCell(int32 CellX, int32 CellY) const;

	/** 칸 하나의 크기(cm). Area 를 GridSize 로 나눈 값이고, 바깥 고리도 같은 크기를 쓴다. */
	FVector2D GetCellSize() const;

	/** 칸 상태에 맞춰 메쉬 인스턴스를 다시 만든다. */
	void RefreshCellMeshes();

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

protected:
	// Area 안쪽(=진행도로 세는 영역)의 가로 x 세로 칸 수.
	// 영역 비율에 맞춰 칸이 정사각형이 되게 잡는 게 보기 좋다.
	//
	// 칸 하나가 8~10cm 쯤 되어야 붓 자국처럼 보인다. 20cm 를 넘으면 어떤 머티리얼을 써도
	// 네모가 눈에 띈다. 반대로 칸이 너무 잘면 붓질마다 인스턴스를 전부 다시 만드는 비용이
	// 커진다. 전체 3000칸 근처부터 프레임에 영향이 온다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	FIntPoint GridSize = FIntPoint(40, 20);

	// Area 바깥으로 몇 cm 까지 칠할 수 있는가. 여기 묻은 페인트는 진행도에 포함되지 않고,
	// CleanupTools 로 지워야 하는 "삐져나간 페인트" 가 된다. 0 이면 Area 밖으로는 칠해지지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "0.0"))
	float OverspillMargin = 0.f;

	// 삐져나간 페인트를 지우는 도구. (3단계+ 검은색 롤러)
	// 이 도구는 Area 바깥 칸만 지운다. 완성된 횡단보도는 지울 수 없고,
	// 존이 완료된 뒤에도 정리를 계속할 수 있다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	TArray<ECWToolType> CleanupTools;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	ECWCellWorkMode WorkMode = ECWCellWorkMode::FillCells;

	// 시작할 때 모든 칸이 채워져 있는지. (1단계 청소는 true)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	bool bStartFilled = false;

	// 붓 반경(cm). 한 번에 이 반경 안의 칸들이 처리된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "1.0"))
	float BrushRadius = 70.f;

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

	// 칸에 입힐 페인트 머티리얼. 비워두면 CellMeshes 컴포넌트에 이미 꽂혀 있는 것을 쓴다.
	//
	// 가장자리가 원형으로 부드럽게 빠지는 Masked 머티리얼이어야 도색이 깔끔하게 보인다.
	// 사각형 그대로면 칸이 그대로 눈에 보여서 격자무늬가 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Visual")
	TObjectPtr<UMaterialInterface> CellMaterial;

	// 차량이 이 존의 젖은 페인트를 밟고 지나가면 자국을 남기는가. (3단계 도색 존에서 켠다)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work|Vehicle")
	bool bStainsVehicles = false;

	// 자국을 남길 존. 3단계+ 정리 존을 지정한다.
	//
	// 자기 자신을 지정하면 안 된다. 차가 지나갈 때마다 도색 진행도가 공짜로 올라간다.
	// (횡단보도는 도로 위에 있어서 차량이 상시 지나간다)
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

private:
	// 칸별 채움 상태. 이게 이 존의 전부다. 바깥 고리를 포함한 전체 격자 크기다.
	TArray<bool> CellFilled;

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
};
