// CWToolPaintDisplayTest.cpp

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UI/Widget/CWBaseToolPaintGauge.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCWToolPaintDisplayTest,
	"Crosswalk.UI.ToolPaintDisplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCWToolPaintDisplayTest::RunTest(const FString& Parameters)
{
	const FText One = FText::FromString(TEXT("1/2"));
	const FText Full = FText::FromString(TEXT("2/2"));

	// 도구 종류별 데이터 - 기존 BP switch 의 결과를 데이터로 옮긴 값
	struct FCase { ECWToolType Type; bool bShowsFill; bool bDark; };
	const FCase Cases[] = {
		{ ECWToolType::Broom, false, false },
		{ ECWToolType::Liner, true, false },
		{ ECWToolType::WhiteRoller, true, false },
		{ ECWToolType::BlackRoller, true, true },
		{ ECWToolType::AsphaltPatcher, true, true },
	};

	for (const FCase& Case : Cases)
	{
		FCWToolPaintInput Input;
		Input.bHasTool = true;
		Input.ToolType = Case.Type;
		Input.Ratio = 0.5f;
		Input.bShowsFill = Case.bShowsFill;
		Input.bDarkPaint = Case.bDark;
		const FCWToolPaintDisplay Display = FCWToolPaintDisplayBuilder::Build(Input);
		const FString Name = StaticEnum<ECWToolType>()->GetNameStringByValue(static_cast<int64>(Case.Type));
		TestEqual(*(Name + TEXT(" 활성")), Display.bActive, Case.bShowsFill);
		TestEqual(*(Name + TEXT(" 어두움")), Display.bDark, Case.bShowsFill && Case.bDark);
		TestEqual(*(Name + TEXT(" 채움")), Display.Fill, Case.bShowsFill ? 0.5f : 0.f);
		TestEqual(*(Name + TEXT(" 글자 유무")), Display.PercentText.IsEmpty(), !Case.bShowsFill);
	}

	// 아스팔트처럼 잔량 비율이 0 이어도 활성이면 0% 를 표시함 (기존 표시 보존)
	{
		FCWToolPaintInput Input;
		Input.bHasTool = true;
		Input.ToolType = ECWToolType::AsphaltPatcher;
		Input.Ratio = 0.f;
		Input.bShowsFill = true;
		Input.bDarkPaint = true;
		const FCWToolPaintDisplay Display = FCWToolPaintDisplayBuilder::Build(Input);
		TestTrue(TEXT("아스팔트 활성"), Display.bActive);
		TestEqual(TEXT("아스팔트 글자"), Display.PercentText.ToString(), FCWToolPaintDisplayBuilder::MakePercentText(0.f).ToString());
	}

	// 도구가 없거나 None 이면 데이터가 채워져 있어도 비활성
	{
		FCWToolPaintInput Input;
		Input.bHasTool = false;
		Input.ToolType = ECWToolType::Liner;
		Input.bShowsFill = true;
		TestFalse(TEXT("도구 없음 비활성"), FCWToolPaintDisplayBuilder::Build(Input).bActive);

		Input.bHasTool = true;
		Input.ToolType = ECWToolType::None;
		TestFalse(TEXT("None 비활성"), FCWToolPaintDisplayBuilder::Build(Input).bActive);
	}

	// 통을 들고 있으면 글자와 아이콘이 도구 표시를 이김 (1개·2개 이상)
	{
		FCWToolPaintInput Input;
		Input.bHasTool = true;
		Input.ToolType = ECWToolType::Liner;
		Input.Ratio = 0.8f;
		Input.bShowsFill = true;
		Input.CarriedTextOne = One;
		Input.CarriedTextFull = Full;

		Input.CarriedCount = 1;
		FCWToolPaintDisplay Display = FCWToolPaintDisplayBuilder::Build(Input);
		TestEqual(TEXT("통 1개 글자"), Display.PercentText.ToString(), One.ToString());
		TestFalse(TEXT("통 1개 아이콘 활성 꺼짐"), Display.bIconActive);

		Input.CarriedCount = 3;
		Display = FCWToolPaintDisplayBuilder::Build(Input);
		TestEqual(TEXT("통 3개도 가득 글자"), Display.PercentText.ToString(), Full.ToString());

		Input.CarriedCount = 0;
		Display = FCWToolPaintDisplayBuilder::Build(Input);
		TestTrue(TEXT("통 0개면 도구 표시"), Display.bActive && Display.bIconActive);
	}

	// 잔량 비율은 0~1 로 보정
	{
		FCWToolPaintInput Input;
		Input.bHasTool = true;
		Input.ToolType = ECWToolType::Liner;
		Input.bShowsFill = true;
		Input.Ratio = 1.7f;
		TestEqual(TEXT("상한 보정"), FCWToolPaintDisplayBuilder::Build(Input).Fill, 1.f);
		Input.Ratio = -0.2f;
		TestEqual(TEXT("하한 보정"), FCWToolPaintDisplayBuilder::Build(Input).Fill, 0.f);
	}

	return true;
}

#endif
