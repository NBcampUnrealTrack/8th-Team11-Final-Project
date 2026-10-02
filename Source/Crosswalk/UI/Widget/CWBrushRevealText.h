#pragma once

#include "CoreMinimal.h"
#include "Components/TextBlock.h"
#include "CWBrushRevealText.generated.h"

class SCWBrushRevealText;

// 중간 렌더 타깃 없이 원본 글씨를 유지하여 연출 완료 전후의 선명도 차이를 방지함
UCLASS(meta = (DisplayName = "CW Brush Reveal Text"))
class CROSSWALK_API UCWBrushRevealText : public UTextBlock
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CW|Brush", meta = (ClampMin = "0", ClampMax = "1"))
	float BrushProgress = 1.f;

	UFUNCTION(BlueprintCallable, Category = "CW|Brush")
	void SetBrushProgress(float Progress);

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SCWBrushRevealText> RevealWidget;
};
