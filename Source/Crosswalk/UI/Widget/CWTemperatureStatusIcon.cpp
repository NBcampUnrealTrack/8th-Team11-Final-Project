#include "UI/Widget/CWTemperatureStatusIcon.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "UI/CWUILog.h"

void UCWTemperatureStatusIcon::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// 디자이너에서 값을 바꾸면 위젯을 다시 만들지 않아도 바로 보이게 함
	ApplyIconLayout();
	ApplyIconImage();
}

void UCWTemperatureStatusIcon::SetIncreasing(bool bInIncreasing)
{
	if (bStateReceived && bIncreasing == bInIncreasing)
	{
		return;
	}

	bStateReceived = true;
	bIncreasing = bInIncreasing;
	ApplyIconImage();
}

TSharedRef<SWidget> UCWTemperatureStatusIcon::RebuildWidget()
{
	if (!WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		WorkerImage = WidgetTree->ConstructWidget<UImage>();
		Canvas->AddChildToCanvas(WorkerImage);
	}
	else if (!WorkerImage)
	{
		// 컴파일 뒤처럼 트리는 남고 포인터만 비는 경우에 이미지를 다시 찾음
		if (const UCanvasPanel* Canvas = Cast<UCanvasPanel>(WidgetTree->RootWidget))
		{
			WorkerImage = Cast<UImage>(Canvas->GetChildAt(0));
		}
	}

	ApplyIconLayout();
	ApplyIconImage();
	return Super::RebuildWidget();
}

void UCWTemperatureStatusIcon::ApplyIconLayout()
{
	UCanvasPanelSlot* ImageSlot = WorkerImage ? Cast<UCanvasPanelSlot>(WorkerImage->Slot) : nullptr;
	if (!ImageSlot)
	{
		return;
	}

	// 이동 행은 위로, 작업 행은 아래로 벌려 기존 체온계 추종 위치를 유지함
	const float GapOffset = (bWorkStatus ? 1.f : -1.f) * FMath::Max(0.f, AdditionalVerticalGap) * 0.5f;
	ImageSlot->SetPosition(IconPosition + FVector2D(0.f, GapOffset));
	ImageSlot->SetSize(IconSize);
}

void UCWTemperatureStatusIcon::ApplyIconImage()
{
	if (!WorkerImage)
	{
		return;
	}

	const bool bShowIncrease = bStateReceived ? bIncreasing : bPreviewIncreasing;
	UTexture2D* Texture = bShowIncrease ? IncreaseTexture : DecreaseTexture;
	if (!Texture)
	{
		// 이미지가 비면 조용히 사라지는 아이콘을 로그로 찾을 수 있게 상태마다 한 번만 알림
		bool& bWarned = bShowIncrease ? bWarnedMissingIncrease : bWarnedMissingDecrease;
		if (!bWarned && !IsDesignTime())
		{
			bWarned = true;
			UE_LOG(LogCWUI, Warning, TEXT("StatusIcon %s: %s image is not set."), *GetName(), bShowIncrease ? TEXT("Increase") : TEXT("Decrease"));
		}
		WorkerImage->SetVisibility(ESlateVisibility::Hidden);
		return;
	}

	WorkerImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WorkerImage->SetBrushFromTexture(Texture, false);
}
