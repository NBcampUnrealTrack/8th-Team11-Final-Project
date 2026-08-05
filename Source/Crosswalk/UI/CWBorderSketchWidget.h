#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "CWBorderSketchWidget.generated.h"

class ACWWorkZone_BorderSketch;

/**
 * 테두리 스케치 미니게임 UI.
 *
 * 사각형 밑그림과 지금까지 그려진 선을 NativePaint 에서 직접 그린다.
 * 이미지 위젯을 여러 개 배치해 위치를 계산하는 방식보다 단순하고, 위젯 BP 가 없어도 동작한다.
 *
 * 스타일을 다듬고 싶으면 이 클래스를 부모로 하는 위젯 BP 를 만들어
 * 존의 WidgetClass 에 지정하면 된다. BP 에서 추가한 텍스트나 게이지는 이 그림 위에 얹힌다.
 * (GetZone 으로 존 상태를 바인딩할 수 있다)
 */
UCLASS()
class CROSSWALK_API UCWBorderSketchWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	void SetZone(ACWWorkZone_BorderSketch* InZone) { Zone = InZone; }

	UFUNCTION(BlueprintPure, Category = "Sketch")
	ACWWorkZone_BorderSketch* GetZone() const { return Zone; }

protected:
	// 도형의 가로 폭을 화면 가로의 몇 배로 잡을지.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float BoardWidthRatio = 0.35f;

	// 도형 좌상단을 화면 세로의 몇 지점에 둘지. 0.25 면 위쪽 1/4 지점.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BoardTopRatio = 0.22f;

	// 아직 그리지 않은 밑그림 색.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch")
	FLinearColor GuideColor = FLinearColor(1.f, 1.f, 1.f, 0.22f);

	// 그려진 선의 색.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch")
	FLinearColor DrawnColor = FLinearColor(1.f, 0.93f, 0.35f, 1.f);

	// 꼭짓점에서 방향 입력을 기다리는 동안의 색. 눈에 띄게 다른 색을 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch")
	FLinearColor WaitingColor = FLinearColor(1.f, 0.35f, 0.2f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch", meta = (ClampMin = "1.0"))
	float GuideThickness = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch", meta = (ClampMin = "1.0"))
	float DrawnThickness = 6.f;

	// 선의 머리에 표시할 사각 마커의 한 변 길이(px).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch", meta = (ClampMin = "0.0"))
	float HeadMarkerSize = 16.f;

	// 대기 중일 때 눌러야 하는 방향을 가리키는 화살표 길이(px).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sketch", meta = (ClampMin = "0.0"))
	float DirectionHintLength = 46.f;

private:
	UPROPERTY(Transient)
	TObjectPtr<ACWWorkZone_BorderSketch> Zone;
};
