// CWUIRootWidget.cpp

#include "CWUIRootWidget.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"

#pragma region PublicFunctions

void UCWUIRootWidget::SetScreenContent(UUserWidget* InWidget)
{
    if (!IsValid(Overlay_ScreenLayer))
    {
        return;
    }

    // Screen Page는 한 번에 하나만 표시
    Overlay_ScreenLayer->ClearChildren();

    if (!IsValid(InWidget))
    {
        return;
    }

    // 화면이 전체 Screen Layer를 채우도록 명시
    if (UOverlaySlot* AddedSlot =
        Overlay_ScreenLayer->AddChildToOverlay(InWidget))
    {
        AddedSlot->SetHorizontalAlignment(HAlign_Fill);
        AddedSlot->SetVerticalAlignment(VAlign_Fill);
    }
}

void UCWUIRootWidget::ClearScreenContent()
{
    if (IsValid(Overlay_ScreenLayer))
    {
        // 현재 Screen Page를 Root에서 제거
        Overlay_ScreenLayer->ClearChildren();
    }
}

void UCWUIRootWidget::SetTransitionBlocking(bool bBlocking)
{
    if (!IsValid(Overlay_TransitionLayer))
    {
        return;
    }

    // 전환 중에만 투명 입력 차단막을 표시
    Overlay_TransitionLayer->SetVisibility(
        bBlocking
        ? ESlateVisibility::Visible
        : ESlateVisibility::Collapsed
    );
}

#pragma endregion