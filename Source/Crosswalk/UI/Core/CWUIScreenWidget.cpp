// CWUIScreenWidget.cpp

#include "CWUIScreenWidget.h"

#pragma region Constructor

UCWUIScreenWidget::UCWUIScreenWidget(
    const FObjectInitializer& ObjectInitializer
)
    : Super(ObjectInitializer)
{
    // Screen Page의 Slate Root가 키보드 포커스를 받을 수 있도록 설정
    SetIsFocusable(true);
}

#pragma endregion

#pragma region PublicFunctions

void UCWUIScreenWidget::RequestOpenPage(ECWUIPage TargetPage)
{
    // 실제 화면 전환은 Subsystem이 처리
    OnRequestOpenPage.Broadcast(TargetPage);
}

#pragma endregion