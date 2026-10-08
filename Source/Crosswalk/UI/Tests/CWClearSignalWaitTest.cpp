// CWClearSignalWaitTest.cpp

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UI/Widget/CWBaseInGameHUD.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCWClearSignalWaitTest,
	"Crosswalk.UI.ClearSignalWait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCWClearSignalWaitTest::RunTest(const FString& Parameters)
{
	// 대기 시간 전에는 신호를 확인하지 않음
	TestFalse(TEXT("리드인 전"), FCWClearSignalTiming::IsLeadInElapsed(0.59f, 0.6f));
	TestTrue(TEXT("리드인 경계"), FCWClearSignalTiming::IsLeadInElapsed(0.6f, 0.6f));
	TestTrue(TEXT("리드인 후"), FCWClearSignalTiming::IsLeadInElapsed(5.f, 0.6f));

	// 타임아웃 전에는 계속 기다림
	TestFalse(TEXT("타임아웃 전"), FCWClearSignalTiming::IsTimedOut(9.99f, 10.f));
	TestTrue(TEXT("타임아웃 경계"), FCWClearSignalTiming::IsTimedOut(10.f, 10.f));

	// 설정이 0 이하여도 0.5초 하한으로 즉시 통과하지 않음
	TestFalse(TEXT("0 설정 하한 전"), FCWClearSignalTiming::IsTimedOut(0.49f, 0.f));
	TestTrue(TEXT("0 설정 하한 경계"), FCWClearSignalTiming::IsTimedOut(0.5f, 0.f));
	TestFalse(TEXT("음수 설정 하한 전"), FCWClearSignalTiming::IsTimedOut(0.1f, -3.f));

	// 소요 시간은 스테이지 시작을 뺀 값 - 맵 이동에도 이어지는 월드 시간이 누적되어 보이지 않게 함
	TestEqual(TEXT("소요 시간 = 클리어 - 시작"), FCWClearSignalTiming::StageElapsedSeconds(286.f, 226.f), 60.f);
	TestEqual(TEXT("다음 스테이지도 처음부터"), FCWClearSignalTiming::StageElapsedSeconds(430.f, 290.f), 140.f);
	TestEqual(TEXT("시작이 더 늦으면 0"), FCWClearSignalTiming::StageElapsedSeconds(10.f, 20.f), 0.f);
	TestEqual(TEXT("시작 기록 전에는 이전 값 그대로"), FCWClearSignalTiming::StageElapsedSeconds(280.f, -1.f), 280.f);

	return true;
}

#endif
