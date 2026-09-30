// CWClipboardScreenWidget.cpp

#include "UI/Clipboard/CWClipboardScreenWidget.h"
#include "UI/Clipboard/CWClipboardPresenterSubsystem.h"
#include "Components/SizeBox.h"
#include "Engine/LocalPlayer.h"
#include "Components/EditableTextBox.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Brushes/SlateNoResource.h"

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
	RemoveScreenTitleInput();
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
	if (!bEnabled) RemoveScreenTitleInput();
	if (Panel_ClipboardPaper)
	{
		Panel_ClipboardPaper->SetVisibility(bEnabled ? PaperVisibility : ESlateVisibility::HitTestInvisible);
	}
	if (PaperActor.IsValid() && PaperActor->IsPaperOwner(this))
	{
		PaperActor->SetPaperInteractionEnabled(bEnabled);
	}
}

void UCWClipboardScreenWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	UpdateScreenTitleInput();
}

void UCWClipboardScreenWidget::UpdateScreenTitleInput()
{
	UEditableTextBox* Source = nullptr;
	if (Panel_ClipboardPaper)
	{
		// 종이 패널은 팝업이 열릴 때 부모에서 떼어져 GetWidgetFromName 이 뿌리부터 내려가면 닿지 않음
		UWidgetTree::ForWidgetAndChildren(Panel_ClipboardPaper, [&Source](UWidget* Widget)
		{
			if (!Source && Widget->GetFName() == TEXT("EditableTextBox_RoomName"))
			{
				Source = Cast<UEditableTextBox>(Widget);
			}
		});
	}
	if (!bInputReady || bClosing || bReturning || !PaperActor.IsValid() || !PaperActor->IsPaperOwner(this))
	{
		RemoveScreenTitleInput();
		return;
	}
	ULocalPlayer* Player = GetOwningLocalPlayer();
	UGameViewportClient* Viewport = Player ? Player->ViewportClient : nullptr;
	if (!Source || !Viewport) return;
	FVector2D Position, Size;
	if (!PaperActor->ProjectPaperWidget(Source->GetCachedGeometry(), Position, Size))
	{
		RemoveScreenTitleInput();
		return;
	}
	const FVector2D OriginalSize = Source->GetCachedGeometry().GetLocalSize();
	ScreenTitleRect = FMargin(Position.X, Position.Y, OriginalSize.X, OriginalSize.Y);
	if (!ScreenTitleLayer.IsValid())
	{
		TitleInputSource = Source;
		TitleSourceVisibility = Source->GetVisibility();
		TitleSourceStyle = Source->GetWidgetStyle();
		TitleHiddenStyle = TitleSourceStyle;
		// 지정 글자색은 전경색을 상속하지 않을 수 있어 글자와 그림자 및 외곽선 색도 함께 숨김
		TitleHiddenStyle.ForegroundColor = FLinearColor::Transparent;
		TitleHiddenStyle.FocusedForegroundColor = FLinearColor::Transparent;
		TitleHiddenStyle.ReadOnlyForegroundColor = FLinearColor::Transparent;
		TitleHiddenStyle.TextStyle.ColorAndOpacity = FLinearColor::Transparent;
		TitleHiddenStyle.TextStyle.ShadowColorAndOpacity = FLinearColor::Transparent;
		TitleHiddenStyle.TextStyle.Font.OutlineSettings.OutlineColor = FLinearColor::Transparent;
		TitleSourceHint = Source->GetHintText();
		// 종이 쪽 입력칸의 배경과 테두리를 살려 종이 조명과 같은 색으로 보이게 하고 화면 입력칸은 글자와 캐럿만 그림
		// 배경까지 그리면 조명을 안 받는 원래 색이 나와 전환이 끝나는 순간 밝게 튐
		ScreenTitleStyle = Source->GetWidgetStyle();
		ScreenTitleStyle.SetBackgroundImageNormal(FSlateNoResource());
		ScreenTitleStyle.SetBackgroundImageHovered(FSlateNoResource());
		ScreenTitleStyle.SetBackgroundImageFocused(FSlateNoResource());
		ScreenTitleStyle.SetBackgroundImageReadOnly(FSlateNoResource());
		// 실제 화면 창에서 IME를 처리하고 원본 텍스트를 동기화하여 기존 방 생성 BP를 유지함
		const TWeakObjectPtr<UCWClipboardScreenWidget> WeakThis(this);
		SAssignNew(ScreenTitleInput, SEditableTextBox)
			.Style(&ScreenTitleStyle)
			.Text(Source->GetText())
			// 안내 문구는 색을 정한 뒤 알파를 전용 투명도로 덮어써서 전경색을 투명으로 해도 남으므로 포커스 없을 때는 문구를 비움
			.HintText(FText::GetEmpty())
			.Justification(Source->GetJustification())
			.OnTextChanged_Lambda([WeakThis](const FText& Text)
			{
				if (WeakThis.IsValid() && WeakThis->TitleInputSource)
				{
					WeakThis->TitleInputSource->SetText(Text);
				}
			});
		ScreenTitleInput->SetRenderOpacity(0.f);
		ScreenTitleInput->SetRenderTransformPivot(FVector2D::ZeroVector);
		SAssignNew(ScreenTitleLayer, SConstraintCanvas)
			.Visibility(EVisibility::SelfHitTestInvisible)
			+ SConstraintCanvas::Slot()
			.Anchors(FAnchors(0.f, 0.f))
			.Alignment(FVector2D::ZeroVector)
			.Offset_Lambda([WeakThis]() { return WeakThis.IsValid() ? WeakThis->ScreenTitleRect : FMargin(); })
			[ScreenTitleInput.ToSharedRef()];
		Viewport->AddViewportWidgetForPlayer(Player, ScreenTitleLayer.ToSharedRef(), 100);
		// 원본의 레이아웃은 남겨 위치를 추적하되 가상 창이 입력 포커스를 가져가지 않게 함
		Source->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	// 화면 입력칸이 글자를 그리는 동안만 종이 쪽 글자와 안내 문구를 숨겨 두 번 겹쳐 보이지 않게 함
	// 안내 문구는 전경색으로 숨겨지지 않아 양쪽 모두 문구 자체를 바꿈
	const bool bInputFocused = ScreenTitleInput->HasKeyboardFocus();
	// 지정 글자색이나 읽기 전용 색이 투명 전경색을 무시해도 원본과 겹치지 않도록 화면 입력칸 전체를 숨김
	// 가시성은 유지하여 투명 상태에서도 클릭으로 다시 입력 포커스를 받을 수 있게 함
	ScreenTitleInput->SetRenderOpacity(bInputFocused ? 1.f : 0.f);
	if (bInputFocused != bTitleSourceTextHidden)
	{
		bTitleSourceTextHidden = bInputFocused;
		Source->SetWidgetStyle(bInputFocused ? TitleHiddenStyle : TitleSourceStyle);
		Source->SetHintText(bInputFocused ? FText::GetEmpty() : TitleSourceHint);
		ScreenTitleInput->SetHintText(bInputFocused ? TitleSourceHint : FText::GetEmpty());
	}

	// 벡터 하나만 넘기면 크기가 아니라 이동으로 해석되어 입력칸이 줄어들지 않고 원본 크기로 그려짐
	ScreenTitleInput->SetRenderTransform(FSlateRenderTransform(FScale2f(FVector2f(Size / OriginalSize))));
	ScreenTitleInput->SetEnabled(Source->GetIsEnabled());
	ScreenTitleInput->SetIsReadOnly(Source->GetIsReadOnly());
	if (!ScreenTitleInput->HasKeyboardFocus() && !ScreenTitleInput->GetText().EqualTo(Source->GetText()))
	{
		ScreenTitleInput->SetText(Source->GetText());
	}
}

void UCWClipboardScreenWidget::RemoveScreenTitleInput()
{
	if (ScreenTitleLayer.IsValid())
	{
		if (ULocalPlayer* Player = GetOwningLocalPlayer())
		{
			if (Player->ViewportClient)
			{
				Player->ViewportClient->RemoveViewportWidgetForPlayer(Player, ScreenTitleLayer.ToSharedRef());
			}
		}
	}
	ScreenTitleInput.Reset();
	ScreenTitleLayer.Reset();
	bTitleSourceTextHidden = false;
	if (TitleInputSource)
	{
		TitleInputSource->SetWidgetStyle(TitleSourceStyle);
		TitleInputSource->SetHintText(TitleSourceHint);
		TitleInputSource->SetVisibility(TitleSourceVisibility);
		TitleInputSource = nullptr;
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
