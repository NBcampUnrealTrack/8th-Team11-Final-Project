#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CWPlayerNameLibrary.generated.h"

// 플레이어 이름을 UI 에 맞는 길이로 줄이는 공용 입구
// 화면마다 허용 글자 수가 달라 규칙(MaxChars, KeepChars)은 호출하는 쪽이 정함
UCLASS()
class CROSSWALK_API UCWPlayerNameLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// 이름이 MaxChars 를 넘으면 앞 KeepChars 글자 뒤에 "..." 를 붙여 반환하고, 넘지 않으면 원본을 그대로 반환
	// 자르는 지점이 이모지 같은 서로게이트 쌍의 앞 글자이면 깨진 문자가 남지 않도록 한 글자 덜 남김
	UFUNCTION(BlueprintPure, Category = "CW|UI|Player")
	static FText FormatPlayerNameForDisplay(const FText& Name, int32 MaxChars, int32 KeepChars);
};
