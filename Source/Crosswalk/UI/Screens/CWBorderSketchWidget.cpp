#include "UI/Screens/CWBorderSketchWidget.h"

#include "Rendering/DrawElements.h"
#include "Work/CWWorkZone_BorderSketch.h"

namespace
{
	/** 정규화 경로를 위젯 로컬 좌표로 옮긴다. MakeLines 가 FVector2f 를 받으므로 여기서 변환한다. */
	void BuildLinePoints(const TArray<FVector2D>& NormalizedPoints, const FVector2D& Origin, float Scale,
		TArray<FVector2f>& OutPoints)
	{
		OutPoints.Reset(NormalizedPoints.Num());
		for (const FVector2D& Point : NormalizedPoints)
		{
			const FVector2D Local = Origin + Point * Scale;
			OutPoints.Add(FVector2f(static_cast<float>(Local.X), static_cast<float>(Local.Y)));
		}
	}
}

int32 UCWBorderSketchWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (Zone == nullptr)
	{
		return Layer;
	}

	// 정규화 경로의 가로가 1.0 이므로, 화면에서 쓸 가로 폭이 그대로 배율이 된다.
	const FVector2D ScreenSize = AllottedGeometry.GetLocalSize();
	const float BoardWidth = ScreenSize.X * BoardWidthRatio;
	const FVector2D Origin((ScreenSize.X - BoardWidth) * 0.5f, ScreenSize.Y * BoardTopRatio);

	const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();

	TArray<FVector2D> NormalizedPoints;
	TArray<FVector2f> LinePoints;

	// 1. 아직 그리지 않은 밑그림 (사각형 전체)
	Zone->GetFullPath(NormalizedPoints);
	BuildLinePoints(NormalizedPoints, Origin, BoardWidth, LinePoints);
	FSlateDrawElement::MakeLines(OutDrawElements, Layer, PaintGeometry, LinePoints,
		ESlateDrawEffect::None, GuideColor, true, GuideThickness);

	++Layer;

	// 2. 지금까지 그려진 선
	const bool bWaiting = Zone->IsWaitingAtCorner();
	const FLinearColor LineColor = bWaiting ? WaitingColor : DrawnColor;

	Zone->GetDrawnPath(NormalizedPoints);
	if (NormalizedPoints.Num() >= 2)
	{
		BuildLinePoints(NormalizedPoints, Origin, BoardWidth, LinePoints);
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, PaintGeometry, LinePoints,
			ESlateDrawEffect::None, LineColor, true, DrawnThickness);
	}

	++Layer;

	if (NormalizedPoints.Num() == 0)
	{
		return Layer;
	}

	// 3. 선의 머리 표시. 사각형 외곽선으로 그려서 별도 브러시 애셋이 필요 없게 한다.
	const FVector2D HeadLocal = Origin + NormalizedPoints.Last() * BoardWidth;
	const float HalfMarker = HeadMarkerSize * 0.5f;

	const TArray<FVector2f> MarkerPoints = {
		FVector2f(static_cast<float>(HeadLocal.X - HalfMarker), static_cast<float>(HeadLocal.Y - HalfMarker)),
		FVector2f(static_cast<float>(HeadLocal.X + HalfMarker), static_cast<float>(HeadLocal.Y - HalfMarker)),
		FVector2f(static_cast<float>(HeadLocal.X + HalfMarker), static_cast<float>(HeadLocal.Y + HalfMarker)),
		FVector2f(static_cast<float>(HeadLocal.X - HalfMarker), static_cast<float>(HeadLocal.Y + HalfMarker)),
		FVector2f(static_cast<float>(HeadLocal.X - HalfMarker), static_cast<float>(HeadLocal.Y - HalfMarker))
	};

	FSlateDrawElement::MakeLines(OutDrawElements, Layer, PaintGeometry, MarkerPoints,
		ESlateDrawEffect::None, LineColor, true, DrawnThickness * 0.6f);

	++Layer;

	// 4. 꼭짓점 대기 중이면 눌러야 하는 방향을 화살표로 알려준다.
	if (!bWaiting)
	{
		return Layer;
	}

	// 존은 이동 입력 기준(W = +Y)으로 돌려주므로, 화면 좌표로 되돌리려면 Y 부호를 뒤집는다.
	const FVector2D RequiredInput = Zone->GetRequiredCornerInput();
	const FVector2D HintDirection(RequiredInput.X, -RequiredInput.Y);
	if (HintDirection.IsNearlyZero())
	{
		return Layer;
	}

	const FVector2D HintEnd = HeadLocal + HintDirection * DirectionHintLength;

	const TArray<FVector2f> HintPoints = {
		FVector2f(static_cast<float>(HeadLocal.X), static_cast<float>(HeadLocal.Y)),
		FVector2f(static_cast<float>(HintEnd.X), static_cast<float>(HintEnd.Y))
	};

	FSlateDrawElement::MakeLines(OutDrawElements, Layer, PaintGeometry, HintPoints,
		ESlateDrawEffect::None, WaitingColor, true, DrawnThickness);

	return Layer + 1;
}
