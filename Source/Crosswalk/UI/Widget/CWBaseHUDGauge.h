#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/Enums/CWUITypes.h"
#include "CWBaseHUDGauge.generated.h"

class UCWUISubsystem;

// HUD 게이지의 공통 부모
// 설정 화면에서 숨겨야 하는 인스턴스만 bHideInSettings 로 지정하며, 기본은 숨기지 않음
// 페이지 변경 통보를 구독해 Tick 폴링 없이 표시를 맞추고, 값 갱신은 자식 BP 가 맡음
// 투명도는 이 클래스가 단독으로 정하므로 자식 BP 는 RenderOpacity 를 직접 쓰지 말고 SetGaugeAvailable 을 사용
UCLASS(Abstract)
class CROSSWALK_API UCWBaseHUDGauge : public UUserWidget
{
	GENERATED_BODY()

public:
	// 표시할 데이터가 없을 때 게이지를 숨김 (설정 화면 숨김과 겹쳐도 서로 덮어쓰지 않음)
	UFUNCTION(BlueprintCallable, Category = "HUD Gauge")
	void SetGaugeAvailable(bool bInAvailable);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandlePageChanged(ECWUIPage OldPage, ECWUIPage NewPage);

	void ApplySettingsVisibility(ECWUIPage Page);
	void ApplyOpacity();

public:
	// HUD 를 설정 화면에서도 일관되게 보이도록 기본은 끔
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD Gauge")
	bool bHideInSettings = false;

private:
	bool bGaugeAvailable = true;
	bool bHiddenBySettings = false;

	// 위젯이 만들어진 월드의 서브시스템 - 해제 때 같은 대상에서 구독을 풀기 위함
	TWeakObjectPtr<UCWUISubsystem> SubscribedSubsystem;
};
