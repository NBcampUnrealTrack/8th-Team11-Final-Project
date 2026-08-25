// CWUILog.h

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CWUILog.generated.h"

// UI 시스템 전용 로그 카테고리
DECLARE_LOG_CATEGORY_EXTERN(LogCWUI, Log, All);

UCLASS()
class CROSSWALK_API UCWUILog : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

#pragma region DebugLog

public:
	// 필요할 때 콘솔에서 Log LogCWUI Verbose로 활성화하는 UI 진단 로그
	UFUNCTION(
		BlueprintCallable,
		Category = "CW|UI|Debug",
		meta = (DevelopmentOnly)
	)
	static void LogUIVerbose(const FString& Message);

#pragma endregion
};
