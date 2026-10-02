#include "CWStageIntroWidget.h"

#include "Components/Image.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Core/CWUISubsystem.h"

UCWStageIntroWidget::UCWStageIntroWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// UIOnly 입력에서도 스페이스바를 위젯이 받을 수 있도록 포커스를 허용함
	SetIsFocusable(true);
}

bool UCWStageIntroWidget::BeginIntro(bool bReturnToMenu)
{
	// 에디터의 원본 목록을 유지해야 메뉴에서 다시 열 때 이미지가 중복되지 않음
	DisplayPages = GetPagesForContext(bReturnToMenu);
	if (!IsValid(Image_Page) || DisplayPages.IsEmpty())
	{
		return false;
	}
	// 에디터에서 장수를 바꾸더라도 빈 이미지로 입력만 차단되는 상황을 방지함
	for (const TObjectPtr<UTexture2D>& Page : DisplayPages)
	{
		if (!IsValid(Page))
		{
			return false;
		}
	}

	PageIndex = 0;
	bReturnToPauseMenu = bReturnToMenu;
	bIntroActive = true;
	ShowPage();
	if (APlayerController* PC = GetOwningPlayer())
	{
		SetUserFocus(PC);
	}
	return true;
}

TArray<TObjectPtr<UTexture2D>> UCWStageIntroWidget::GetPagesForContext(bool bCombined) const
{
	if (bCombined)
	{
		TArray<TObjectPtr<UTexture2D>> Combined = Pages;
		Combined.Append(Stage02Pages);
		return Combined;
	}
	const FString StageName = UGameplayStatics::GetCurrentLevelName(this, true);
	if (StageName == TEXT("Stage_01"))
	{
		return Pages;
	}
	if (StageName == TEXT("Stage_02"))
	{
		return Stage02Pages;
	}
	return {};
}

bool UCWStageIntroWidget::HasConfiguredIntroPages() const
{
	const TArray<TObjectPtr<UTexture2D>> IntroPages = GetPagesForContext(false);
	return !IntroPages.IsEmpty() && !IntroPages.ContainsByPredicate(
		[](const TObjectPtr<UTexture2D>& Page) { return !IsValid(Page); });
}

void UCWStageIntroWidget::NativeDestruct()
{
	bIntroActive = false;
	Super::NativeDestruct();
}

FReply UCWStageIntroWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIntroActive && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// 이미지 크기와 DPI 배율이 달라도 플레이 화면 중앙을 기준으로 나누기 위함
		const FGeometry ViewportGeometry = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this);
		const float ClickX = ViewportGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()).X;
		if (ClickX < ViewportGeometry.GetLocalSize().X * 0.5f)
		{
			PreviousPage();
		}
		else
		{
			AdvancePage();
		}
		// 마지막 클릭이 게임 작업 입력으로 이어지지 않도록 소비함
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UCWStageIntroWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bIntroActive && bReturnToPauseMenu &&
		(InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::One))
	{
		FinishIntro();
		return FReply::Handled();
	}
	if (bIntroActive && (InKeyEvent.GetKey() == EKeys::SpaceBar || InKeyEvent.GetKey() == EKeys::Enter))
	{
		// 키를 길게 눌러 안내 여러 장이 한꺼번에 넘어가지 않도록 함
		if (!InKeyEvent.IsRepeat())
		{
			AdvancePage();
		}
		// 안내를 닫는 키가 게임 입력으로 이어지지 않도록 소비함
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
	if (PageIndex == DisplayPages.Num())
	{
		FinishIntro();
		return;
	}
	ShowPage();
}

void UCWStageIntroWidget::ShowPage()
{
	// 페이지마다 원본 해상도가 달라도 안내 이미지의 배치 크기를 유지함
	Image_Page->SetBrushFromTexture(DisplayPages[PageIndex], false);
	Image_Page->SetDesiredSizeOverride(FVector2D(1152.0f, 648.0f));
}

void UCWStageIntroWidget::PreviousPage()
{
	if (bIntroActive && PageIndex > 0)
	{
		--PageIndex;
		ShowPage();
	}
}

void UCWStageIntroWidget::FinishIntro()
{
	bIntroActive = false;
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
		{
			if (UCWUISubsystem* UI = LocalPlayer->GetSubsystem<UCWUISubsystem>())
			{
				// 팝업 닫기 경로가 기존 입력 모드와 포커스를 복구하므로 직접 제거하지 않음
				if (UI->ClosePopup(this) && bReturnToPauseMenu)
				{
					UI->RestorePauseMenuFocus();
				}
			}
		}
	}
}
