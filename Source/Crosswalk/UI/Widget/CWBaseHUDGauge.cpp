#include "UI/Widget/CWBaseHUDGauge.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "UI/Core/CWUISubsystem.h"

void UCWBaseHUDGauge::NativeConstruct()
{
	Super::NativeConstruct();

	// 월드 공간 위젯은 소유 플레이어가 비어 있을 수 있어 첫 로컬 플레이어로 대신함
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	if (!LocalPlayer && GetWorld())
	{
		LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
	}
	UCWUISubsystem* UI = LocalPlayer ? LocalPlayer->GetSubsystem<UCWUISubsystem>() : nullptr;
	if (!IsValid(UI))
	{
		return;
	}

	// 재구성(RemoveFromParent 후 재추가)에도 구독이 겹치지 않게 함
	UI->OnPageChanged.AddUniqueDynamic(this, &UCWBaseHUDGauge::HandlePageChanged);
	SubscribedSubsystem = UI;

	// 위젯이 만들어지기 전에 이미 설정 화면일 수 있어 현재 값으로 한 번 맞춤
	ApplySettingsVisibility(UI->GetCurrentPage());
}

void UCWBaseHUDGauge::NativeDestruct()
{
	if (UCWUISubsystem* UI = SubscribedSubsystem.Get())
	{
		UI->OnPageChanged.RemoveDynamic(this, &UCWBaseHUDGauge::HandlePageChanged);
	}
	SubscribedSubsystem.Reset();

	Super::NativeDestruct();
}

void UCWBaseHUDGauge::SetGaugeAvailable(bool bInAvailable)
{
	bGaugeAvailable = bInAvailable;
	ApplyOpacity();
}

void UCWBaseHUDGauge::HandlePageChanged(ECWUIPage OldPage, ECWUIPage NewPage)
{
	ApplySettingsVisibility(NewPage);
}

void UCWBaseHUDGauge::ApplySettingsVisibility(ECWUIPage Page)
{
	// 설정 화면의 배경 위에 겹쳐 보이지 않도록 투명도만 바꿈(레이아웃·히트 설정은 건드리지 않음)
	bHiddenBySettings = bHideInSettings && Page == ECWUIPage::Settings;
	ApplyOpacity();
}

void UCWBaseHUDGauge::ApplyOpacity()
{
	SetRenderOpacity((bGaugeAvailable && !bHiddenBySettings) ? 1.f : 0.f);
}
