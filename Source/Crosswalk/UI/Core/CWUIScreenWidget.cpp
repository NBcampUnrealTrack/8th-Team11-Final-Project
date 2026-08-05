// CWUIScreenWidget.cpp

#include "CWUIScreenWidget.h"

#pragma region PublicFunctions

void UCWUIScreenWidget::RequestOpenPage(ECWUIPage TargetPage)
{
    // 실제 화면 전환은 Subsystem이 처리
    OnRequestOpenPage.Broadcast(TargetPage);
}

#pragma endregion