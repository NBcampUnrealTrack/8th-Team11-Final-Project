#include "UI/Widget/CWTemperatureStatusIcon.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

UCWTemperatureStatusIcon::UCWTemperatureStatusIcon(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UTexture2D> MD(TEXT("/Game/CrossWalk/UI/HUD/Textures/StatusIcons/T_UI_MoveDown"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> MU(TEXT("/Game/CrossWalk/UI/HUD/Textures/StatusIcons/T_UI_MoveUp"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> WD(TEXT("/Game/CrossWalk/UI/HUD/Textures/StatusIcons/T_UI_WorkDown"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> WU(TEXT("/Game/CrossWalk/UI/HUD/Textures/StatusIcons/T_UI_WorkUp"));
	MoveDown = MD.Object;
	MoveUp = MU.Object;
	WorkDown = WD.Object;
	WorkUp = WU.Object;
}

TSharedRef<SWidget> UCWTemperatureStatusIcon::RebuildWidget()
{
	if (!WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		WorkerImage = WidgetTree->ConstructWidget<UImage>();
		UCanvasPanelSlot* WorkerSlot = Canvas->AddChildToCanvas(WorkerImage);
		WorkerSlot->SetPosition(FVector2D(20.f, 4.f));
		WorkerSlot->SetSize(FVector2D(48.f, 48.f));
		WorkerImage->SetBrushFromTexture(bWorkStatus ? WorkDown : MoveDown, false);
		LastIncrease.Reset();
	}
	ApplyIconSpacing();
	return Super::RebuildWidget();
}

void UCWTemperatureStatusIcon::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	ApplyIconSpacing();
}

void UCWTemperatureStatusIcon::ApplyIconSpacing()
{
	// 기존 체온계 추종 위치를 유지하면서 위아래 행이 추가 간격을 절반씩 나누도록 함
	const float Offset = (bWorkStatus ? 1.f : -1.f) * FMath::Max(0.f, AdditionalVerticalGap) * 0.5f;
	if (WorkerImage)
	{
		if (UCanvasPanelSlot* ImageSlot = Cast<UCanvasPanelSlot>(WorkerImage->Slot))
		{
			ImageSlot->SetPosition(FVector2D(20.f, 4.f + Offset));
		}
	}
}

void UCWTemperatureStatusIcon::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UPanelWidget* Panel = GetParent();
	if (!Panel || !WorkerImage)
	{
		return;
	}
	// 기존 텍스트 결과만 읽어 체온 효과 판정이나 네트워크 로직이 갈라지는 것을 방지함
	for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
	{
		const UTextBlock* Label = Cast<UTextBlock>(Panel->GetChildAt(Index));
		if (!Label)
		{
			continue;
		}
		const bool bIncrease = Label->GetText().ToString().Contains(TEXT("↑"));
		if (!LastIncrease.IsSet() || LastIncrease.GetValue() != bIncrease)
		{
			WorkerImage->SetBrushFromTexture(bWorkStatus
				? (bIncrease ? WorkUp.Get() : WorkDown.Get())
				: (bIncrease ? MoveUp.Get() : MoveDown.Get()), false);
			LastIncrease = bIncrease;
		}
		break;
	}
}
