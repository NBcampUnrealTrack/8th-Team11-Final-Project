// CWUIBootstrapComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Enums/CWUITypes.h"
#include "CWUIBootstrapComponent.generated.h"

class UCWUIRootWidget;
class UCWUIScreenWidget;
class UCWUIPageConfigDataAsset;
class UUserWidget;

UCLASS(
	ClassGroup = (CW),
	// 블루프린트 PlayerController - Add Component 목록에서 검색
	meta = (BlueprintSpawnableComponent)
)
class CROSSWALK_API UCWUIBootstrapComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWUIBootstrapComponent();

#pragma region ActorComponentLifecycle

protected:
	// 로컬 PlayerController의 UI 초기화를 시작
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void TryOpenInitialPage();
	FTimerHandle InitialPageTimer;
	ECWUIPage PendingInitialPage = ECWUIPage::None;
	bool bInitialWorldReady = false;

#pragma endregion

#pragma region UIConfiguration

protected:
	// 현재 World에 생성할 UI Root 클래스
	UPROPERTY(EditDefaultsOnly, Category = "CW|UI")
	TSubclassOf<UCWUIRootWidget> UIRootClass;

	// 세션 복귀 안내에 기존 공용 확인 팝업을 사용함
	UPROPERTY(EditDefaultsOnly, Category = "CW|UI")
	TSubclassOf<UUserWidget> SessionNoticeClass;

	// 이 환경에서 열 수 있는 페이지 정책
	UPROPERTY(EditDefaultsOnly, Category = "CW|UI")
	TObjectPtr<UCWUIPageConfigDataAsset> PageConfigData;

	// 최초 진입 시 표시할 페이지
	UPROPERTY(EditDefaultsOnly, Category = "CW|UI")
	ECWUIPage InitialPage = ECWUIPage::MainMenu;

	// Page를 열지 않는 화면에서 사용할 초기 입력 모드
	UPROPERTY(EditDefaultsOnly, Category = "CW|UI")
	ECWUIInputMode InitialInputMode = ECWUIInputMode::GameOnly;

	// 현재 World에서 시작할 HUD 클래스 목록
	UPROPERTY(EditDefaultsOnly, Category = "CW|UI")
	TArray<TSubclassOf<UUserWidget>> InitialHUDClasses;

#pragma endregion
};
