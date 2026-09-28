// CWClipboardScreenWidget.cpp

#include "UI/Clipboard/CWClipboardScreenWidget.h"
#include "UI/Clipboard/CWClipboardPresenterSubsystem.h"
#include "Components/SizeBox.h"
#include "Engine/LocalPlayer.h"

void UCWClipboardScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bClosing = false;
	bReturning = false;
	PendingPage = ECWUIPage::None;
	if (!Panel_ClipboardPaper || !SizeBox_PaperHost || !GetOwningLocalPlayer())
	{
		// 프롭 연결이 빠져도 기존 화면의 세션 조작은 유지함
		return;
	}

	PaperVisibility = Panel_ClipboardPaper->GetVisibility();
	Presenter = GetOwningLocalPlayer()->GetSubsystem<UCWClipboardPresenterSubsystem>();
	if (!Presenter)
	{
		return;
	}
	if (bClipboardPopup && Presenter->GetPropActor())
	{
		PreviousPage = Cast<UCWClipboardScreenWidget>(Presenter->GetPropActor()->GetPaperOwner());
	}
	Presenter->OnRaiseFinished.AddUniqueDynamic(this, &ThisClass::HandleRaiseFinished);
	Presenter->OnLowerFinished.AddUniqueDynamic(this, &ThisClass::HandleLowerFinished);

	if (PreviousPage.IsValid())
	{
		PaperActor = Presenter->GetPropActor();
		PreviousPage->SetPaperInput(false);
		Panel_ClipboardPaper->RemoveFromParent();
		SetPaperInput(false);
		// 전환이 불가능하면 즉시 완료되므로 구독을 먼저 걸어야 입력 잠금이 풀림
		PaperActor->OnPaperCrossfadeFinished.AddUObject(this, &ThisClass::HandlePaperCrossfadeFinished);
		PaperActor->CrossfadePaperContent(this, Panel_ClipboardPaper);
	}
	else
	{
		PaperActor = Presenter->AttachPaperContent(this, Panel_ClipboardPaper);
		if (!PaperActor.IsValid())
		{
			RestoreHostContent();
			return;
		}
		SetPaperInput(false);
		PaperActor->SetPaperOnlyPresentation(true);
		PaperActor->SetPresentationScale(ClipboardScale, SizeBox_PaperHost->GetHeightOverride() / 1040.f);
		Presenter->PlayRaise();
	}
}

void UCWClipboardScreenWidget::NativeDestruct()
{
	if (Presenter)
	{
		Presenter->OnRaiseFinished.RemoveDynamic(this, &ThisClass::HandleRaiseFinished);
		Presenter->OnLowerFinished.RemoveDynamic(this, &ThisClass::HandleLowerFinished);
	}
	if (ACWClipboardPropActor* Actor = PaperActor.Get())
	{
		Actor->OnPaperCrossfadeFinished.RemoveAll(this);
		if (Actor->IsPaperOwner(this) ||
			(bReturning && PreviousPage.IsValid() && Actor->IsPaperOwner(PreviousPage.Get())))
		{
			// 성공·강제 종료에도 원래 종이 소유권을 복구함
			if (PreviousPage.IsValid() && !PreviousPage->bClosing)
			{
				Actor->SetPaperContent(PreviousPage.Get(), PreviousPage->Panel_ClipboardPaper);
				PreviousPage->SetPaperInput(true);
			}
			else
			{
				Actor->ClearPaperContent(this);
				if (Presenter)
				{
					Presenter->PlayLower();
				}
			}
		}
	}
	RestoreHostContent();
	PaperActor.Reset();
	PreviousPage.Reset();
	Presenter = nullptr;
	Super::NativeDestruct();
}

void UCWClipboardScreenWidget::RequestOpenPage(ECWUIPage TargetPage)
{
	if (bClosing || bReturning)
	{
		return;
	}
	if (!Presenter || !PaperActor.IsValid() || !PaperActor->IsPaperOwner(this))
	{
		Super::RequestOpenPage(TargetPage);
		return;
	}
	bClosing = true;
	PendingPage = TargetPage;
	SetPaperInput(false);
	SetFocus();
	Presenter->PlayLower();
}

void UCWClipboardScreenWidget::ReturnToClipboardPage()
{
	if (bClosing || bReturning || !bInputReady)
	{
		return;
	}
	bReturning = true;
	SetPaperInput(false);
	SetFocus();
	if (!Presenter || !PaperActor.IsValid() || !PaperActor->IsPaperOwner(this))
	{
		OnClipboardReturnFinished.Broadcast();
		return;
	}
	if (!PreviousPage.IsValid())
	{
		// 설정 화면은 내리기가 끝난 뒤 기존 복귀 로직을 실행해 종이가 먼저 사라지지 않도록 함
		Presenter->PlayLower();
		return;
	}
	// 팝업은 전환이 끝난 뒤 닫혀야 지워지는 도중에 사라지지 않음
	PaperActor->CrossfadePaperContent(PreviousPage.Get(), PreviousPage->Panel_ClipboardPaper);
}

void UCWClipboardScreenWidget::HandlePaperCrossfadeFinished()
{
	if (bReturning)
	{
		if (PreviousPage.IsValid())
		{
			PreviousPage->SetPaperInput(true);
		}
		OnClipboardReturnFinished.Broadcast();
		return;
	}
	HandleRaiseFinished();
}

void UCWClipboardScreenWidget::HandleRaiseFinished()
{
	if (!bClosing && !bReturning && PaperActor.IsValid() && PaperActor->IsPaperOwner(this))
	{
		SetPaperInput(true);
	}
}

void UCWClipboardScreenWidget::HandleLowerFinished()
{
	if (bReturning && !PreviousPage.IsValid())
	{
		OnClipboardReturnFinished.Broadcast();
		return;
	}
	if (bClosing && PendingPage != ECWUIPage::None)
	{
		const ECWUIPage Target = PendingPage;
		PendingPage = ECWUIPage::None;
		Super::RequestOpenPage(Target);
	}
}

void UCWClipboardScreenWidget::SetPaperInput(bool bEnabled)
{
	bInputReady = bEnabled;
	if (Panel_ClipboardPaper)
	{
		Panel_ClipboardPaper->SetVisibility(bEnabled ? PaperVisibility : ESlateVisibility::HitTestInvisible);
	}
	if (PaperActor.IsValid() && PaperActor->IsPaperOwner(this))
	{
		PaperActor->SetPaperInteractionEnabled(bEnabled);
	}
}

void UCWClipboardScreenWidget::RestoreHostContent()
{
	if (SizeBox_PaperHost && Panel_ClipboardPaper)
	{
		SizeBox_PaperHost->SetContent(Panel_ClipboardPaper);
		Panel_ClipboardPaper->SetVisibility(PaperVisibility);
	}
}

FReply UCWClipboardScreenWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (PaperActor.IsValid() && PaperActor->IsPaperOwner(this))
	{
		if (PaperActor->PressPaperPointer(Event.GetEffectingButton()))
		{
			return FReply::Handled().CaptureMouse(TakeWidget());
		}
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(Geometry, Event);
}

FReply UCWClipboardScreenWidget::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (PaperActor.IsValid() && PaperActor->IsPaperOwner(this))
	{
		PaperActor->ReleasePaperPointer(Event.GetEffectingButton());
		// Release 중 팝업·페이지가 바뀌면 이전 입력칸에 포커스를 돌려주지 않음
		if (PaperActor.IsValid() && PaperActor->IsPaperOwner(this))
		{
			PaperActor->FocusPaperInput();
		}
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(Geometry, Event);
}

FReply UCWClipboardScreenWidget::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (PaperActor.IsValid() && PaperActor->IsPaperOwner(this))
	{
		PaperActor->ScrollPaper(Event.GetWheelDelta());
		return FReply::Handled();
	}
	return Super::NativeOnMouseWheel(Geometry, Event);
}

void UCWClipboardScreenWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& Event)
{
	if (PaperActor.IsValid() && PaperActor->IsPaperOwner(this))
	{
		PaperActor->ReleasePaperPointers();
	}
	Super::NativeOnMouseCaptureLost(Event);
}

FReply UCWClipboardScreenWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	return bInputReady ? Super::NativeOnPreviewKeyDown(Geometry, Event) : FReply::Handled();
}
