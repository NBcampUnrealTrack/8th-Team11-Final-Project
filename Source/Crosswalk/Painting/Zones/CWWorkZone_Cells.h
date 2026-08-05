#pragma once

#include "CoreMinimal.h"
#include "Painting/Zones/CWBaseWorkZone.h"
#include "CWWorkZone_Cells.generated.h"

class UBoxComponent;
class UInstancedStaticMeshComponent;

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
 * 사용 예시
 *   1단계 청소  : WorkMode=ClearCells, bStartFilled=true   (먼지가 깔려 있고 쓸어서 없앤다)
 *   3단계 도색  : WorkMode=FillCells,  bStartFilled=false  (빈 도로를 흰색으로 채운다)
 *   3단계+ 정리 : WorkMode=ClearCells, bStartFilled=false  (차량이 흘린 페인트만 지운다)
 */
UCLASS()
class CROSSWALK_API ACWWorkZone_Cells : public ACWBaseWorkZone
{
	GENERATED_BODY()

public:
	ACWWorkZone_Cells();

	virtual void BeginPlay() override;

	virtual void ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds) override;

	/**
	 * 외부에서 칸을 추가한다. 차량이 마르지 않은 페인트를 밟고 지나갈 때 쓴다.
	 * (3단계+ 정리 존에 이걸 호출하면 정리할 대상이 생긴다)
	 */
	UFUNCTION(BlueprintCallable, Category = "Work")
	void AddCellsAt(const FVector& WorldLocation, float Radius);

	UFUNCTION(BlueprintPure, Category = "Work")
	int32 GetFilledCellCount() const { return FilledCellCount; }

private:
	/** 격자를 만들고 초기 상태(bStartFilled)를 적용한다. */
	void BuildGrid();

	/**
	 * 중심점 주변 반경 안의 칸들을 켜거나 끈다.
	 * @return 실제로 상태가 바뀐 칸이 하나라도 있으면 true
	 */
	bool SetCellsInRadius(const FVector& WorldCenter, float Radius, bool bNewFilled);

	/** 칸 상태에 맞춰 메쉬 인스턴스를 다시 만든다. */
	void RefreshCellMeshes();

	/** 칸 개수를 세서 진행도를 다시 계산한다. */
	void RefreshProgress();

	float CalculateProgress() const;

	FVector GetCellWorldLocation(int32 CellX, int32 CellY) const;

	FORCEINLINE int32 GetCellIndex(int32 CellX, int32 CellY) const { return CellY * GridSize.X + CellX; }

public:
	// 칸 하나를 그리는 메쉬. 인스턴스 하나가 칸 하나다. (격자는 부모의 Area 박스를 나눈다)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UInstancedStaticMeshComponent> CellMeshes;

protected:
	// 가로 x 세로 칸 수. 영역 비율에 맞춰 칸이 정사각형이 되게 잡는 게 보기 좋다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	FIntPoint GridSize = FIntPoint(20, 10);

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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float CellPadding = 0.02f;

private:
	// 칸별 채움 상태. 이게 이 존의 전부다.
	TArray<bool> CellFilled;

	int32 FilledCellCount = 0;

	// 지우기 모드에서 진행도의 분모로 쓴다. 지금까지 최대로 채워졌던 칸 수.
	int32 PeakFilledCellCount = 0;
};
