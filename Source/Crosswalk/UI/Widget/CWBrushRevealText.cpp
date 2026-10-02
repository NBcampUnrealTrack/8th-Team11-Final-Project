#include "UI/Widget/CWBrushRevealText.h"

#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SCompoundWidget.h"

class SCWBrushRevealText : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCWBrushRevealText) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		ChildSlot[Args._Content.Widget];
	}

	void SetProgress(float Value)
	{
		const float NewProgress = FMath::Clamp(Value, 0.f, 1.f);
		if (Progress == NewProgress) return;
		Progress = NewProgress;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId,
		const FWidgetStyle& Style, bool bParentEnabled) const override
	{
		if (Progress <= 0.f) return LayerId;
		if (Progress >= 1.f)
		{
			return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements, LayerId, Style, bParentEnabled);
		}

		const FVector2D Size = Geometry.GetLocalSize();
		if (Size.X <= 0.f || Size.Y <= 0.f) return LayerId;
		// 폰트의 실제 높이와 바깥 획까지 포함하여 작은 슬롯에서도 글씨 끝이 잘리지 않게 함
		const FVector2D TextDesiredSize = ChildSlot.GetWidget()->GetDesiredSize();
		constexpr float Padding = 4.f;
		const float Left = FMath::Min(0.f, float(Size.X - TextDesiredSize.X)) - Padding;
		const float Width = FMath::Max(float(Size.X), float(TextDesiredSize.X)) + Padding - Left;
		const float Height = FMath::Max(float(Size.Y), float(TextDesiredSize.Y)) + Padding * 2.f;
		const int32 Rows = FMath::Clamp(FMath::CeilToInt(Height * Geometry.Scale), 1, 256);
		constexpr int32 Strokes = 8;
		constexpr float BrushWidth = 0.42f;
		const float Travel = Progress * Strokes;
		const int32 Stroke = FMath::Min(FMath::FloorToInt(Travel), Strokes - 1);
		const float Phase = Travel - Stroke;
		int32 MaxLayer = LayerId;
		// 지나간 영역은 완전히 열어 두고 앞 경계만 왕복시켜 뒤늦은 획 복원을 방지함
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const float Y = (Row + 0.5f) / Rows;
			const float Direction = Stroke % 2 ? Y : 1.f - Y;
			const float Start = Direction * (1.f - BrushWidth);
			const float Fill = FMath::SmoothStep(Start, Start + BrushWidth, Phase);
			const float RevealedWidth = Width * (Stroke + Fill) / Strokes;
			if (RevealedWidth <= 0.f) continue;
			const FVector2D Position(Left, -Padding + Height * Row / Rows);
			const FVector2D Extent(RevealedWidth, Height / Rows);
			Elements.PushClip(FSlateClippingZone(Geometry.ToPaintGeometry(FVector2f(Extent), FSlateLayoutTransform(FVector2f(Position)))));
			MaxLayer = FMath::Max(MaxLayer, SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements, LayerId, Style, bParentEnabled));
			Elements.PopClip();
		}
		return MaxLayer;
	}

private:
	float Progress = 1.f;
};

TSharedRef<SWidget> UCWBrushRevealText::RebuildWidget()
{
	const TSharedRef<SWidget> TextWidget = Super::RebuildWidget();
	RevealWidget = SNew(SCWBrushRevealText)[TextWidget];
	RevealWidget->SetProgress(BrushProgress);
	return RevealWidget.ToSharedRef();
}

void UCWBrushRevealText::SetBrushProgress(float Progress)
{
	BrushProgress = FMath::Clamp(Progress, 0.f, 1.f);
	if (RevealWidget.IsValid()) RevealWidget->SetProgress(BrushProgress);
}

void UCWBrushRevealText::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (RevealWidget.IsValid()) RevealWidget->SetProgress(BrushProgress);
}

void UCWBrushRevealText::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	RevealWidget.Reset();
}
