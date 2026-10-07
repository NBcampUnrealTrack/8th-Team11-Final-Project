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
class UCWInteractionPromptWidget;
class UCWStageIntroWidget;

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
	TSubclassOf<UCWStageIntroWidget> GetStageIntroClass() const { return StageIntroClass; }

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

	UPROPERTY(EditDefaultsOnly, Category = "CW|UI|Parry", meta = (DisplayName = "Show Parry Icon"))
	bool bShowParryIcon = true;

	UPROPERTY(EditDefaultsOnly, Category = "CW|UI|Parry", meta = (DisplayName = "Parry Icon Offset"))
	FVector2D ParryIconOffset = FVector2D(42.f, -2.f);

	// 기본 구현을 즉시 사용하고 문구나 배치는 UI 전용 파생 클래스에서 조절함
	UPROPERTY(EditDefaultsOnly, Category = "CW|UI")
	TSubclassOf<UCWInteractionPromptWidget> InteractionPromptClass;

	// 안내 이미지를 BP에서 지정해 화면 조정에 C++ 재빌드가 필요하지 않게 함
	UPROPERTY(EditDefaultsOnly, Category = "CW|UI")
	TSubclassOf<UCWStageIntroWidget> StageIntroClass;

#pragma endregion
};
