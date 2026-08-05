// CWUIBootstrapComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Enums/CWUITypes.h"
#include "UI/Core/CWUIRootWidget.h"
#include "UI/Core/CWUIScreenWidget.h"
#include "CWUIBootstrapComponent.generated.h"

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

#pragma endregion
};