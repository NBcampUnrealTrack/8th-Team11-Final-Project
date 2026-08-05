#pragma once

#include "CoreMinimal.h"
#include "Painting/Zones/CWBaseWorkZone.h"
#include "CWWorkZone_Dust.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/**
 * 1단계 도로 청소용 존. 빗자루로 쓸어서 먼지를 없앤다.
 *
 * 진행도는 부모가 들고 있고 서버 권위다. 여러 명이 같이 쓸면 각자 ApplyWork 를 부르므로
 * 자동으로 인원수만큼 빨라진다. 완료되면 HUD 체크리스트와 선행 단계 잠금이 함께 풀린다.
 *
 * 페인트 마스크는 순수한 로컬 연출이다.
 *   각 클라이언트가 리플리케이트된 캐릭터 위치와 도구/작업 상태만 보고 똑같이 다시 그린다.
 *   렌더 타깃 자체는 네트워크로 보내지 않는다. 진행도만 맞으면 그림은 알아서 수렴한다.
 *
 * 머티리얼 요구사항
 *   DustMaterial      : 스칼라 파라미터 ClearedPct (0~1). 먼지 → 깨끗함 전환을 구동한다.
 *                       선택적으로 텍스처 파라미터 CoverageMask 도 받으면 정밀한 붓 자취가 나온다.
 *   BrushStampMaterial: 스칼라 파라미터 CenterU / CenterV / Radius. 비워두면 붓 자취 없이
 *                       ClearedPct 만으로 동작한다. (기능은 정상, 연출만 거칠어진다)
 */
UCLASS()
class CROSSWALK_API ACWWorkZone_Dust : public ACWBaseWorkZone
{
	GENERATED_BODY()

public:
	ACWWorkZone_Dust();

	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void BeginPlay() override;

	virtual void Tick(float DeltaSeconds) override;

	virtual void ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds) override;

protected:
	virtual void OnProgressChanged(float NewProgress) override;

private:
	/** 지정 지점에 붓 자취를 찍는다. 로컬 연출이라 서버에서는 의미가 없다. */
	void PaintAt(const FVector& WorldLocation);

	/**
	 * 월드 지점을 이 존의 0~1 UV 로 바꾼다.
	 * @return 존 영역 안이면 true
	 */
	bool ProjectToLocalUV(const FVector& WorldLocation, FVector2D& OutUV) const;

	void ApplyProgressToMaterial();

	/** 먼지 메쉬를 Area 크기에 맞춘다. */
	void FitPatchMeshToArea();

public:
	// 먼지가 보이는 바닥 메쉬. 판정은 부모의 Area 박스가 한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dust")
	TObjectPtr<UStaticMeshComponent> PatchMesh;

protected:
	// 혼자서 전부 쓸어내는 데 걸리는 시간(초). 두 명이 붙으면 절반으로 줄어든다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust", meta = (ClampMin = "0.1"))
	float SecondsToClear = 4.f;

	// 먼지 → 깨끗함 전환을 구동하는 바닥 머티리얼.
	//
	// 비워두면 PatchMesh 컴포넌트의 Materials 슬롯에 이미 들어 있는 머티리얼을 그대로 쓴다.
	// EditDefaultsOnly 로 두면 레벨에 배치한 인스턴스에서는 편집할 수 없어서
	// "머티리얼은 보이는데 파라미터만 안 바뀌는" 상태가 되므로 EditAnywhere 로 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual")
	TObjectPtr<UMaterialInterface> DustMaterial;

	// 붓 자취를 찍는 소프트 원형 스탬프. 비워두면 자취 없이 ClearedPct 만으로 동작한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual")
	TObjectPtr<UMaterialInterface> BrushStampMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual")
	FIntPoint CoverageMaskSize = FIntPoint(128, 256);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual")
	float BrushRadiusUV = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual")
	FName ClearedParameterName = TEXT("ClearedPct");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual")
	FName CoverageMaskParameterName = TEXT("CoverageMask");

	// 먼지 메쉬를 도로면보다 살짝 띄워 Z-파이팅을 막는다. (cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual")
	float PatchHeightOffset = 1.f;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DustDynMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BrushDynMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> CoverageMask;
};
