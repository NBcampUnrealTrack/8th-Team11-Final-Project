// CWPlayerNameFormatTest.cpp

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UI/Core/CWPlayerNameLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCWPlayerNameFormatTest,
	"Crosswalk.UI.PlayerNameFormat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCWPlayerNameFormatTest::RunTest(const FString& Parameters)
{
	auto Format = [](const FString& Name, int32 MaxChars, int32 KeepChars)
	{
		return UCWPlayerNameLibrary::FormatPlayerNameForDisplay(FText::FromString(Name), MaxChars, KeepChars).ToString();
	};
	auto Repeat = [](TCHAR Ch, int32 Count)
	{
		return FString::ChrN(Count, Ch);
	};

	// 방 목록·이름표 규칙(20, 17): 경계값 20 은 그대로, 21 부터 줄임
	TestEqual(TEXT("짧은 이름은 그대로"), Format(TEXT("Kim"), 20, 17), FString(TEXT("Kim")));
	TestEqual(TEXT("20자는 그대로"), Format(Repeat('a', 20), 20, 17), Repeat('a', 20));
	TestEqual(TEXT("21자는 17자 + 말줄임"), Format(Repeat('a', 21), 20, 17), Repeat('a', 17) + TEXT("..."));

	// 상태창 규칙(15, 13)
	TestEqual(TEXT("15자는 그대로"), Format(Repeat('b', 15), 15, 13), Repeat('b', 15));
	TestEqual(TEXT("16자는 13자 + 말줄임"), Format(Repeat('b', 16), 15, 13), Repeat('b', 13) + TEXT("..."));

	// 한글도 글자 수 기준으로 같은 규칙
	TestEqual(TEXT("한글 21자"), Format(Repeat(0xAC00, 21), 20, 17), Repeat(0xAC00, 17) + TEXT("..."));

	// 서로게이트 쌍(이모지)의 앞 글자에서 끊기면 한 글자 덜 남김
	FString Emoji = Repeat('c', 16);
	Emoji.AppendChar(static_cast<TCHAR>(0xD83D));
	Emoji.AppendChar(static_cast<TCHAR>(0xDE00));
	Emoji += Repeat('c', 4);
	TestEqual(TEXT("쌍의 앞 글자에서 끊기면 16자 + 말줄임"), Format(Emoji, 20, 17), Repeat('c', 16) + TEXT("..."));

	// 잘못된 인자에도 크래시하지 않음
	TestEqual(TEXT("KeepChars 0 이면 말줄임만"), Format(Repeat('d', 30), 20, 0), FString(TEXT("...")));
	TestEqual(TEXT("KeepChars 가 길이를 넘으면 전체 + 말줄임"), Format(Repeat('e', 21), 20, 99), Repeat('e', 21) + TEXT("..."));
	TestEqual(TEXT("빈 문자열"), Format(FString(), 20, 17), FString());

	return true;
}

#endif
