#include "UI/Core/CWPlayerNameLibrary.h"

FText UCWPlayerNameLibrary::FormatPlayerNameForDisplay(const FText& Name, int32 MaxChars, int32 KeepChars)
{
	const FString Source = Name.ToString();
	if (MaxChars < 0 || Source.Len() <= MaxChars)
	{
		return Name;
	}

	int32 Keep = FMath::Clamp(KeepChars, 0, Source.Len());

	// 서로게이트 쌍의 앞 글자(0xD800~0xDBFF)에서 끊으면 반쪽짜리 문자가 화면에 남음
	if (Keep > 0 && (Source[Keep - 1] >= 0xD800 && Source[Keep - 1] <= 0xDBFF))
	{
		--Keep;
	}

	return FText::FromString(Source.Left(Keep) + TEXT("..."));
}
