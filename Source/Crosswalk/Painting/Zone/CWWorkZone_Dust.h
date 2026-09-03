#pragma once

#include "CoreMinimal.h"
#include "Painting/Zone/CWBaseWorkZone.h"
#include "CWWorkZone_Dust.generated.h"

class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
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
 *   DustMaterial      : 머티리얼 도메인이 Deferred Decal 이어야 한다. (아래 참고)
 *                       스칼라 파라미터 ClearedPct (0~1) 로 먼지 → 깨끗함 전환을 구동한다.
 *                       선택적으로 텍스처 파라미터 CoverageMask 도 받으면 정밀한 붓 자취가 나온다.
 *   BrushStampMaterial: 스칼라 파라미터 CenterU / CenterV / Radius. 비워두면 붓 자취 없이
 *                       ClearedPct 만으로 동작한다. (기능은 정상, 연출만 거칠어진다)
 *
 * 먼지가 메쉬가 아니라 데칼인 이유
 *   먼지는 도로 위에 얹히는 바닥 연출이고, 싱크홀 균열이나 페인트 자국과 같은 자리를 다툰다.
 *   반투명 평면 메쉬로 두면 층 순서를 정할 방법이 없다. 반투명은 데칼 패스보다 뒤에 그려지고
 *   디퍼드 데칼은 GBuffer 를 고치므로, 먼지가 항상 데칼 위로 올라가 균열을 덮어버린다.
 *   Receives Decals 를 꺼도 달라지지 않는다. 반투명 서피스는 애초에 데칼이 닿지 않는다.
 *
 *   데칼끼리는 SortOrder 로 층을 명시적으로 정할 수 있다. 그래서 먼지도 데칼로 두고
 *   DecalSortOrder 를 낮게 잡아 균열 밑으로 깔았다. 도로 요철을 타는 것과 Z-파이팅이
 *   없어지는 것은 덤이다.
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

	/** 먼지 데칼의 투영 범위를 Area 크기에 맞춘다. */
	void FitDecalToArea();

public:
	// 먼지가 보이는 바닥 데칼. 판정은 부모의 Area 박스가 한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dust")
	TObjectPtr<UDecalComponent> DustDecal;

protected:
	// 혼자서 전부 쓸어내는 데 걸리는 시간(초). 두 명이 붙으면 절반으로 줄어든다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust", meta = (ClampMin = "0.1"))
	float SecondsToClear = 4.f;

	// 먼지 → 깨끗함 전환을 구동하는 바닥 머티리얼. 도메인이 Deferred Decal 이어야 한다.
	//
	// 비워두면 DustDecal 컴포넌트에 이미 지정된 데칼 머티리얼을 그대로 쓴다.
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

	/**
	 * 데칼이 위아래로 훑는 깊이의 절반(cm). DecalSize.X 로 들어간다.
	 *
	 * 존 원점 기준으로 위아래 이 값만큼이 투영 범위다. 도로면이 원점과 정확히 맞지 않아도
	 * 이 범위 안에 들어오면 먼지가 찍힌다. 반대로 너무 크게 잡으면 도로 옆 인도나 화단처럼
	 * 높이가 다른 표면까지 같이 더러워진다.
	 *
	 * 캐릭터나 차량에 씌워지는 걱정은 없다. 움직이는 것들은 데칼 수신을 껐다.
	 * (UCWDecalStatics 참고)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual", meta = (ClampMin = "1.0"))
	float DecalProjectionDepth = 32.f;

	/**
	 * 바닥 데칼끼리의 층 순서. 값이 크면 위에 그려진다.
	 *
	 * 먼지는 도로에 얹힌 것 중 가장 아래다. 페인트 자국(기본 0)과 싱크홀 균열(ACWSinkhole
	 * 에서 지정)이 먼지 위로 올라와야 한다. 먼지가 균열을 덮으면 이미 갈라진 바닥이
	 * 멀쩡해 보인다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual")
	int32 DecalSortOrder = -10;

	/**
	 * 붓 자취 마스크의 UV 축을 데칼 UV 규약에 맞춘다.
	 *
	 * 평면 메쉬였을 때는 메쉬 UV 가 존의 로컬 X/Y 와 그대로 맞았다. 데칼은 투영 방향이
	 * 로컬 X 라서 텍스처 축이 로컬 Y/Z 에 걸리고, 그래서 축이 한 번 돌아간다.
	 * 계산으로 단언하기보다 한 번 쓸어보고 맞추는 쪽이 확실해서 토글로 뺐다.
	 *
	 * 증상별 조치: 자취가 90도 돌아가면 bSwapCoverageUV, 좌우가 뒤집히면 bFlipCoverageU,
	 * 위아래가 뒤집히면 bFlipCoverageV.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Coverage UV")
	bool bSwapCoverageUV = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Coverage UV")
	bool bFlipCoverageU = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Coverage UV")
	bool bFlipCoverageV = false;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DustDynMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BrushDynMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> CoverageMask;
};
