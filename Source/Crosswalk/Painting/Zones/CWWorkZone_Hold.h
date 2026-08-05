#pragma once

#include "CoreMinimal.h"
#include "Painting/Zones/CWBaseWorkZone.h"
#include "CWWorkZone_Hold.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 * 2단계 테두리용 존. 작업 키를 누르고 있으면 게이지가 차오르는 방식.
 *
 * 여러 명이 같이 붙으면 각자 매 프레임 ApplyWork 를 부르기 때문에
 * 자동으로 인원수만큼 빨라진다. 협동 보너스 계산이 따로 필요 없다.
 *
 * 판정은 부모의 Area 박스가, 보이는 건 GuideMesh 가 담당한다.
 * 메쉬가 직접 판정을 받게 하면 단순 콜리전이 없는 메쉬를 넣었을 때
 * "아무리 눌러도 게이지가 안 오르는" 상태가 된다.
 */
UCLASS()
class CROSSWALK_API ACWWorkZone_Hold : public ACWBaseWorkZone
{
	GENERATED_BODY()

public:
	ACWWorkZone_Hold();

	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void BeginPlay() override;

	virtual void ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds) override;

protected:
	virtual void OnProgressChanged(float NewProgress) override;

private:
	/** 가이드 메쉬를 Area 크기에 맞춘다. 레벨에서는 박스 크기만 조절하면 된다. */
	void FitGuideMeshToArea();

public:
	// 눈에 보이는 가이드라인. 판정은 부모의 Area 박스가 하므로 콜리전이 필요 없다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Work")
	TObjectPtr<UStaticMeshComponent> GuideMesh;

protected:
	// 혼자서 0% → 100% 까지 걸리는 시간(초). 2명이면 절반, 4명이면 1/4 로 줄어든다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work", meta = (ClampMin = "0.1"))
	float SecondsToComplete = 6.f;

	// 진행도만큼 선을 드러내기 위해 머티리얼에 넘길 스칼라 파라미터 이름.
	// 이 이름의 파라미터가 없는 머티리얼이어도 그냥 무시된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	FName ProgressParameterName = TEXT("Progress");

	// 가이드 메쉬를 도로면보다 살짝 띄워 Z-파이팅을 막는다. (cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Work")
	float GuideHeightOffset = 1.f;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GuideMaterial;
};
