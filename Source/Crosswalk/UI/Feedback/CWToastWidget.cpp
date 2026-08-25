// CWToastWidget.cpp

#include "UI/Feedback/CWToastWidget.h"
#include "Components/TextBlock.h"

#pragma region Message

void UCWToastWidget::Setup(const FText& InMessage)
{
	if (!IsValid(Text_Message))
	{
		return;
	}

	// 전달받은 안내 문구를 화면에 표시
	Text_Message->SetText(InMessage);
}

#pragma endregion
