#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CWParryStatusWidget.generated.h"

class UTexture2D;
class APawn;

UCLASS()
class CROSSWALK_API UCWParryStatusWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UCWParryStatusWidget(const FObjectInitializer& ObjectInitializer);
	void ConfigureDisplay(bool bShow, FVector2D Offset);
protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
private:
	UPROPERTY()
	TObjectPtr<UTexture2D> BatTexture;
	UPROPERTY()
	TSubclassOf<UUserWidget> LegacyIndicatorClass;
	TWeakObjectPtr<APawn> LastPawn;
	FSlateBrush BatBrush;
	FVector2D IconPosition = FVector2D::ZeroVector;
	bool bCanDisplay = false;
	bool bShowIcon = true;
	FVector2D ScreenOffset = FVector2D(42.f, -2.f);
	bool bReady = false;
	bool bHadCooldown = false;
	float ReadyPopRemaining = 0.f;
	float LegacyRefreshRemaining = 0.f;
};
