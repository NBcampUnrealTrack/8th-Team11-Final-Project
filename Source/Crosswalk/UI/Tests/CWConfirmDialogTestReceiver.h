#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CWConfirmDialogTestReceiver.generated.h"

// 확인 팝업 자동 테스트에서 결과 콜백이 몇 번 불렸는지 세는 수신자
// UHT 가 헤더만 처리하므로 테스트 파일 밖에 둠
UCLASS()
class UCWConfirmDialogTestReceiver : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleConfirmed() { ++ConfirmedCount; }

	UFUNCTION()
	void HandleCancelled() { ++CancelledCount; }

	int32 ConfirmedCount = 0;
	int32 CancelledCount = 0;
};
