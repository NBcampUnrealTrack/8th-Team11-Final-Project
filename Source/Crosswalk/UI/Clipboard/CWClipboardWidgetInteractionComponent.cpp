// CWClipboardWidgetInteractionComponent.cpp

#include "UI/Clipboard/CWClipboardWidgetInteractionComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"

void UCWClipboardWidgetInteractionComponent::FocusHardwareUser(int32 UserIndex)
{
	if (!FSlateApplication::IsInitialized() || !VirtualUser.IsValid() || UserIndex < 0)
	{
		return;
	}

	FSlateApplication& Slate = FSlateApplication::Get();
	const TSharedPtr<SWidget> FocusedWidget = Slate.GetUserFocusedWidget(VirtualUser->GetUserIndex());
	// 입력칸 내부 SEditableText는 포인터 경로의 SEditableTextBox 아래에 있어 경로 포함 검사에서 빠짐
	// 일반 컨트롤까지 실제 사용자 포커스를 옮기면 가상 창의 마우스 좌표와 충돌하므로 입력칸만 연결함
	if (FocusedWidget.IsValid() && IsOverFocusableWidget() &&
		FocusedWidget->GetTypeAsString().Contains(TEXT("EditableText")))
	{
		Slate.SetUserFocus(UserIndex, FocusedWidget, EFocusCause::Mouse);
	}
}
