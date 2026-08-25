// CWUILog.cpp

#include "UI/CWUILog.h"

// UI 시스템 전용 로그 카테고리 정의
DEFINE_LOG_CATEGORY(LogCWUI);

#pragma region DebugLog

void UCWUILog::LogUIVerbose(const FString& Message)
{
	// Verbose는 평소 숨겨두고 UI 문제를 진단할 때만 콘솔에서 활성화
	UE_LOG(
		LogCWUI,
		Verbose,
		TEXT("%s"),
		*Message
	);
}

#pragma endregion
