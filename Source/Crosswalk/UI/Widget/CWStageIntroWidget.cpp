#include "CWStageIntroWidget.h"

#include "Components/Image.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"
#include "UI/Core/CWUISubsystem.h"

UCWStageIntroWidget::UCWStageIntroWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// UIOnly 입력에서도 스페이스바를 위젯이 받을 수 있도록 포커스를 허용함
	SetIsFocusable(true);
}

bool UCWStageIntroWidget::BeginIntro()
{
	if (!IsValid(Image_Page) || Pages.Num() != 3 ||
		!IsValid(Pages[0]) || !IsValid(Pages[1]) || !IsValid(Pages[2]))
	{
		return false;
	}

	PageIndex = 0;
	bIntroActive = true;
	ShowPage();
	return true;
}

void UCWStageIntroWidget::NativeDestruct()
{
	bIntroActive = false;
	// 맵 이탈 뒤 제거된 위젯으로 타이머가 다시 호출되지 않도록 정리함
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PageTimer);
	}
	Super::NativeDestruct();
}

FReply UCWStageIntroWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIntroActive && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		AdvancePage();
		// 마지막 클릭이 게임 작업 입력으로 이어지지 않도록 소비함
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UCWStageIntroWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bIntroActive && InKeyEvent.GetKey() == EKeys::SpaceBar)
	{
		AdvancePage();
		// 마지막 스페이스바가 게임 입력으로 이어지지 않도록 소비함
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UCWStageIntroWidget::AdvancePage()
{
	if (!bIntroActive)
	{
		return;
	}

	++PageIndex;
	if (PageIndex == Pages.Num())
	{
		FinishIntro();
		return;
	}
	ShowPage();
}

void UCWStageIntroWidget::ShowPage()
{
	Image_Page->SetBrushFromTexture(Pages[PageIndex], true);
	if (UWorld* World = GetWorld())
	{
		// 수동 넘김 뒤에도 새 페이지를 설정된 시간 동안 볼 수 있도록 같은 타이머를 재설정함
		World->GetTimerManager().SetTimer(PageTimer, this,
			&UCWStageIntroWidget::AdvancePage, PageDuration, false);
	}
}

void UCWStageIntroWidget::FinishIntro()
{
	bIntroActive = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PageTimer);
	}
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
		{
			if (UCWUISubsystem* UI = LocalPlayer->GetSubsystem<UCWUISubsystem>())
			{
				// 팝업 닫기 경로가 기존 입력 모드와 포커스를 복구하므로 직접 제거하지 않음
				UI->ClosePopup(this);
			}
		}
	}
}
