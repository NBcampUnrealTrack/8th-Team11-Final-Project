// CWUIBootstrapComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Enums/CWUITypes.h"
#include "CWUIBootstrapComponent.generated.h"

class UCWUIRootWidget;
class UCWUIScreenWidget;
class UUserWidget;

UCLASS(
	ClassGroup = (CW),
	// 블루프린트 PlayerController - Add Component 목록에서 검색
	meta = (BlueprintSpawnableComponent)
)
class CROSSWALK_API UCWUIBootstrapComponent : public UActorComponent
{
	GENERATED_BODY()

#pragma region ActorComponentLifecycle

protected:
	// 로컬 PlayerController의 UI 초기화를 시작
	virtual void BeginPlay() override;

#pragma endregion

#pragma region UIConfiguration

protected:
	// 현재 World에 생성할 UI Root 클래스
	UPROPERTY(EditDefaultsOnly, Category = "CW|UI")
	TSubclassOf<UCWUIRootWidget> UIRootClass;

	// 페이지별로 생성할 화면 클래스
	UPROPERTY(EditDefaultsOnly, Category = "CW|UI")
	TMap<ECWUIPage, TSubclassOf<UCWUIScreenWidget>> PageClasses;

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
