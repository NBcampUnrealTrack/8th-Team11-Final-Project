#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "CWHUDPaintSurface.generated.h"

class SCWHUDPaintSurface;

// 화면 크기가 달라도 붓 가장자리가 흐려지지 않도록 텍스처 없이 그리는 배경임
UCLASS(meta = (DisplayName = "CW HUD Paint Surface"))
class CROSSWALK_API UCWHUDPaintSurface : public UWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CW|Paint")
	FLinearColor PaintColor = FLinearColor(0.94f, 0.90f, 0.81f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CW|Paint", meta = (ClampMin = "0", ClampMax = "1"))
	float GrainStrength = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CW|Paint", meta = (ClampMin = "0", ClampMax = "16"))
	float EdgeRoughness = 4.f;

	// 카드마다 같은 무늬가 반복되는 것을 피하면서 프레임 간 무늬는 고정함
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CW|Paint")
	int32 StrokeSeed = 17;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CW|Paint")
	FVector2D DesiredSize = FVector2D(320.f, 100.f);

	// 완료 덧칠만 최초 표시 때 재생하여 일반 배경의 표시 지연을 방지함
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CW|Paint")
	bool bAnimateReveal = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CW|Paint", meta = (ClampMin = "0.05"))
	float RevealDuration = 0.55f;

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SCWHUDPaintSurface> PaintSurface;
};
