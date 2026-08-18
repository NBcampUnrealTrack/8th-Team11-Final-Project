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

#pragma region HUD

bool UCWUIRootWidget::AddHUDWidget(UUserWidget* InWidget)
{
    if (!IsValid(Overlay_HUDLayer) || !IsValid(InWidget))
    {
        return false;
    }

    // 같은 Widget의 중복 추가를 방지
    if (Overlay_HUDLayer->HasChild(InWidget))
    {
        return true;
    }

    // 다른 Panel에 붙은 Widget을 임의로 이동하지 않음
    if (InWidget->GetParent() != nullptr)
    {
        return false;
    }

    // HUD Widget이 전체 레이어를 채우도록 추가
    if (UOverlaySlot* AddedSlot =
        Overlay_HUDLayer->AddChildToOverlay(InWidget))
    {
        AddedSlot->SetHorizontalAlignment(HAlign_Fill);
        AddedSlot->SetVerticalAlignment(VAlign_Fill);
        return true;
    }

    return false;
}

bool UCWUIRootWidget::RemoveHUDWidget(UUserWidget* InWidget)
{
    if (!IsValid(Overlay_HUDLayer) || !IsValid(InWidget))
    {
        return false;
    }

    // 지정한 Widget만 HUD 레이어에서 제거
    return Overlay_HUDLayer->RemoveChild(InWidget);
}

void UCWUIRootWidget::ClearHUDWidgets()
{
    if (IsValid(Overlay_HUDLayer))
    {
        // 현재 HUD Widget을 모두 제거
        Overlay_HUDLayer->ClearChildren();
    }
}

void UCWUIRootWidget::SetHUDLayerVisible(bool bVisible)
{
	if (!IsValid(Overlay_HUDLayer))
	{
		return;
	}

	// HUD는 클릭을 받지 않아야 아래 Screen 레이어의 버튼이 눌린다
	Overlay_HUDLayer->SetVisibility(
		bVisible
		? ESlateVisibility::SelfHitTestInvisible
		: ESlateVisibility::Collapsed
	);
}

#pragma endregion
