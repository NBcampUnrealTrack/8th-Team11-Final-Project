#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "UI/Widget/CWBrushRevealText.h"
#include "Engine/Font.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCWBrushRevealTextRenderTest,
	"Crosswalk.UI.BrushRevealText.RenderContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCWBrushRevealTextRenderTest::RunTest(const FString& Parameters)
{
	UFont* Font = LoadObject<UFont>(nullptr, TEXT("/Game/CrossWalk/UI/Fonts/F_UI_KyoboHandwriting.F_UI_KyoboHandwriting"));
	if (!TestNotNull(TEXT("Result font is available"), Font)) return false;
	const FString Output = FPaths::ProjectSavedDir() / TEXT("Automation/BrushReveal");
	IFileManager::Get().MakeDirectory(*Output, true);
	FWidgetRenderer Renderer(false);
	const float Scales[] = {0.5f, 0.75f, 1.f};
	const float ProgressValues[] = {0.f, 0.25f, 0.5f, 0.75f, 0.85f, 0.9f, 0.95f, 0.99f, 0.999f, 1.f};
	const TCHAR* Lines[] = {TEXT("작업 완료!"), TEXT("스테이지 1-1"), TEXT("소요 시간 01분 56초")};
	const float Widths[] = {600.f, 350.f, 500.f};
	const float Heights[] = {82.f, 60.f, 40.f};

	for (int32 Line = 0; Line < 3; ++Line)
	{
		UCWBrushRevealText* Text = NewObject<UCWBrushRevealText>();
		Text->SetText(FText::FromString(Lines[Line]));
		Text->SetFont(FSlateFontInfo(Font, Line == 0 ? 52 : 28, FName(TEXT("Regular"))));
		Text->SetJustification(ETextJustify::Center);
		Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Text->SetShadowColorAndOpacity(FLinearColor::Transparent);
		const TSharedRef<SWidget> Root = SNew(SOverlay)
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[SNew(SBox).WidthOverride(Widths[Line]).HeightOverride(Heights[Line])[Text->TakeWidget()]];
		for (const float Scale : Scales)
		{
			const FIntPoint Size(FMath::RoundToInt(680.f * Scale), FMath::RoundToInt(180.f * Scale));
			UTextureRenderTarget2D* Target = FWidgetRenderer::CreateTargetFor(FVector2D(Size), TF_Nearest, false);
			TArray<TArray<FColor>> Frames;
			for (int32 Frame = 0; Frame < UE_ARRAY_COUNT(ProgressValues); ++Frame)
			{
				Text->SetBrushProgress(ProgressValues[Frame]);
				// 폰트 아틀라스 최초 업로드와 실제 연출 차이를 분리하기 위한 워밍업임
				Renderer.DrawWidget(Target, Root, Scale, FVector2D(Size), 0.f);
				Renderer.DrawWidget(Target, Root, Scale, FVector2D(Size), 0.f);
				TArray<FColor>& Pixels = Frames.AddDefaulted_GetRef();
				FReadSurfaceDataFlags Flags(RCM_UNorm);
				Flags.SetLinearToGamma(false);
				if (!TestTrue(TEXT("Read rendered text"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, Flags))) return false;
				TArray64<uint8> PNG;
				FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), PNG);
				FFileHelper::SaveArrayToFile(PNG, *(Output / FString::Printf(TEXT("line%d_scale%03d_frame%02d.png"), Line, FMath::RoundToInt(Scale * 100.f), Frame)));
			}
			const TArray<FColor>& Final = Frames.Last();
			int32 InkPixels = 0;
			int32 EndChanges = 0;
			int32 RepaintedPixels = 0;
			for (int32 Pixel = 0; Pixel < Final.Num(); ++Pixel)
			{
				if (Final[Pixel].A > 8) ++InkPixels;
				if (FMath::Abs(int32(Final[Pixel].A) - int32(Frames[8][Pixel].A)) > 2) ++EndChanges;
				// 먼저 완성된 왼쪽 글씨가 마지막 프레임에서 다시 채워지는 회귀를 검출함
				if (Pixel % Size.X < Size.X * 0.4f && FMath::Abs(int32(Final[Pixel].A) - int32(Frames[3][Pixel].A)) > 2) ++RepaintedPixels;
			}
			const FString Label = FString::Printf(TEXT("line%d scale %.2f"), Line, Scale);
			TestTrue(Label + TEXT(" contains visible glyphs"), InkPixels > 20);
			TestEqual(Label + TEXT(" has no completion pop"), EndChanges, 0);
			TestEqual(Label + TEXT(" preserves already revealed glyphs"), RepaintedPixels, 0);
			AddInfo(FString::Printf(TEXT("%s: ink=%d end_changes=%d repainted=%d"), *Label, InkPixels, EndChanges, RepaintedPixels));
		}
	}
	return true;
}

#endif
