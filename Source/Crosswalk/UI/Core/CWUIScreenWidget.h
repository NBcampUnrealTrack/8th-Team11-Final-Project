// CWUIScreenWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/Enums/CWUITypes.h"
#include "CWUIScreenWidget.generated.h"

// 화면이 Subsystem에 페이지 전환을 요청
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCWRequestOpenPageSignature,
	ECWUIPage,
	TargetPage
);

UCLASS(Abstract, Blueprintable)
class CROSSWALK_API UCWUIScreenWidget : public UUserWidget
{
	GENERATED_BODY()

#pragma region Constructor

public:
	// 모든 Screen Page가 UI 포커스를 받을 수 있도록 초기화
	UCWUIScreenWidget(const FObjectInitializer& ObjectInitializer);

#pragma endregion

#pragma region PublicFunctions

public:
	// 화면을 직접 만들지 않고 전환 요청만 전달
	UFUNCTION(BlueprintCallable, Category = "CW|UI")
	void RequestOpenPage(ECWUIPage TargetPage);

#pragma endregion

#pragma region ScreenLifecycle

public:
	// 화면이 Root에 표시된 직후 호출
	UFUNCTION(BlueprintImplementableEvent, Category = "CW|UI")
	void OnScreenOpened();

	// 화면이 Root에서 제거되기 직전에 호출
	// 종료 애니메이션과 시각 상태 정리는 이 이벤트에서 처리
	UFUNCTION(BlueprintImplementableEvent, Category = "CW|UI")
	void OnScreenClosing();

	// 화면이 Root에서 제거된 후 호출
	// 참조 해제와 로그만 처리하고 애니메이션이나 Geometry는 사용하지 않음
	UFUNCTION(BlueprintImplementableEvent, Category = "CW|UI")
	void OnScreenClosed();

#pragma endregion

#pragma region Delegates

public:
	// 페이지 전환 요청을 Subsystem에 전달
	UPROPERTY(BlueprintAssignable, Category = "CW|UI")
	FCWRequestOpenPageSignature OnRequestOpenPage;

#pragma endregion
};
