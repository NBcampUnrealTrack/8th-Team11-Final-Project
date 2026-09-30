#include "UI/Widget/CWHUDPaintSurface.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

class SCWHUDPaintSurface : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCWHUDPaintSurface) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) { SetCanTick(true); }

	void Configure(const FLinearColor& InColor, float InGrain, float InEdge, int32 InSeed, FVector2D InSize, bool bInAnimate, float InDuration)
	{
		Color = InColor;
		Grain = FMath::Clamp(InGrain, 0.f, 1.f);
		Edge = FMath::Clamp(InEdge, 0.f, 16.f);
		Seed = InSeed;
		Size = InSize;
		bAnimate = bInAnimate;
		Duration = FMath::Max(0.05f, InDuration);
		Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
	}

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return Size; }

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		// 숨겨진 완료 덧칠은 실제로 처음 그려진 뒤부터 시간을 계산함
		if (bAnimate && bHasPainted && Elapsed < Duration)
		{
			Elapsed = FMath::Min(Duration, Elapsed + InDeltaTime);
			Invalidate(EInvalidateWidgetReason::Paint);
		}
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId,
		const FWidgetStyle& Style, bool bParentEnabled) const override
	{
		const FVector2D Extent = Geometry.GetLocalSize();
		if (Extent.X <= 0.f || Extent.Y <= 0.f)
		{
			return LayerId;
		}

		const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
		bHasPainted = true;
		const float Progress = bAnimate ? FMath::Clamp(Elapsed / Duration, 0.f, 1.f) : 1.f;
		const FLinearColor Tint = Color * Style.GetColorAndOpacityTint();
		const float Roughness = FMath::Min(Edge, FMath::Min(Extent.X, Extent.Y) * 0.08f);
		FRandomStream Random(Seed);
		auto DrawRect = [&](FVector2D Position, FVector2D Dimensions, FLinearColor DrawColor, int32 Layer)
		{
			// 세로 획을 위아래로 번갈아 채워 덧칠이 지그재그로 이어지게 함
			constexpr int32 StrokeCount = 8;
			for (int32 Stroke = 0; Stroke < StrokeCount; ++Stroke)
			{
				const float Fill = FMath::Clamp(Progress * StrokeCount - Stroke, 0.f, 1.f);
				if (Fill <= 0.f) continue;
				const float Left = FMath::Max(float(Position.X), float(Extent.X * Stroke / StrokeCount));
				const float Right = FMath::Min(float(Position.X + Dimensions.X), float(Extent.X * (Stroke + 1) / StrokeCount));
				const float Top = FMath::Max(float(Position.Y), Stroke % 2 ? float(Extent.Y * (1.f - Fill)) : 0.f);
				const float Bottom = FMath::Min(float(Position.Y + Dimensions.Y), Stroke % 2 ? float(Extent.Y) : float(Extent.Y * Fill));
				if (Right <= Left || Bottom <= Top) continue;
				FSlateDrawElement::MakeBox(Elements, Layer,
					Geometry.ToPaintGeometry(FVector2f(Right - Left, Bottom - Top), FSlateLayoutTransform(FVector2f(Left, Top))),
					Brush, ESlateDrawEffect::None, DrawColor);
			}
		};

		// 띠를 겹치지 않게 채워 반투명 완료 덧칠에도 이음새가 진해지지 않게 함
		constexpr int32 StripCount = 32;
		for (int32 Index = 0; Index < StripCount; ++Index)
		{
			const float Left = Random.FRandRange(0.f, Roughness);
			const float Right = Random.FRandRange(0.f, Roughness);
			const float Top = Extent.Y * Index / StripCount;
			const float Bottom = Extent.Y * (Index + 1) / StripCount;
			DrawRect(FVector2D(Left, Top), FVector2D(Extent.X - Left - Right, Bottom - Top), Tint, LayerId);
		}

		// 글씨 뒤의 대비를 해치지 않도록 잔붓 자국은 낮은 농도로 제한함
		for (int32 Index = 0; Index < 42; ++Index)
		{
			const float X = Random.FRandRange(Roughness, FMath::Max(Roughness, Extent.X - Roughness));
			const float Y = Random.FRandRange(0.f, FMath::Max(0.f, Extent.Y - 1.f));
			const float Width = FMath::Min(Random.FRandRange(4.f, 24.f), Extent.X - X);
			FLinearColor GrainColor = Tint * Random.FRandRange(0.72f, 0.92f);
			GrainColor.A = Tint.A * Grain;
			DrawRect(FVector2D(X, Y), FVector2D(Width, 0.6f), GrainColor, LayerId + 1);
		}
		return LayerId + 1;
	}

private:
	FLinearColor Color = FLinearColor::White;
	FVector2D Size = FVector2D(320.f, 100.f);
	float Grain = 0.08f;
	float Edge = 4.f;
	int32 Seed = 17;
	bool bAnimate = false;
	mutable bool bHasPainted = false;
	float Elapsed = 0.f;
	float Duration = 0.55f;
};

TSharedRef<SWidget> UCWHUDPaintSurface::RebuildWidget()
{
	PaintSurface = SNew(SCWHUDPaintSurface);
	PaintSurface->Configure(PaintColor, GrainStrength, EdgeRoughness, StrokeSeed, DesiredSize, bAnimateReveal, RevealDuration);
	return PaintSurface.ToSharedRef();
}

void UCWHUDPaintSurface::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (PaintSurface.IsValid())
	{
		PaintSurface->Configure(PaintColor, GrainStrength, EdgeRoughness, StrokeSeed, DesiredSize, bAnimateReveal, RevealDuration);
	}
}

void UCWHUDPaintSurface::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	PaintSurface.Reset();
}
