// CWStageSelectWidgetBase.cpp

#include "UI/Screens/StageSelect/CWStageSelectWidgetBase.h"
#include "Components/Widget.h"
#include "Engine/LocalPlayer.h"
#include "UI/Clipboard/CWClipboardPresenterSubsystem.h"

#pragma region UserWidgetOverrides

void UCWStageSelectWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	BeginOpenStageSelect();
}

void UCWStageSelectWidgetBase::NativeDestruct()
{
	if (IsValid(ClipboardPresenter))
	{
		// Destruct 중 발생하는 완료 이벤트를 이 위젯이 다시 받지 않도록
		// 먼저 구독을 해제한 뒤 프롭 정리만 요청한다.
		UnbindClipboardDelegates();

		// 강제 제거 또는 맵 전환에서는 완료를 기다리지 않는다.
		ClipboardPresenter->PlayLower();
	}

	ClipboardPresenter = nullptr;

	Super::NativeDestruct();
}

#pragma endregion

#pragma region PublicFunctions

void UCWStageSelectWidgetBase::BeginOpenStageSelect()
{
	bIsClosing = false;
	bCloseCompleted = false;

	if (IsValid(Panel_StageSelectInteraction) &&
		!bHasCachedInteractionVisibility)
	{
		OriginalInteractionVisibility =
			Panel_StageSelectInteraction->GetVisibility();

		bHasCachedInteractionVisibility = true;
	}

	ResolveClipboardPresenter();

	if (!IsValid(ClipboardPresenter))
	{
		// 연출 시스템이 없어도 스테이지 선택은 진행할 수 있어야 한다.
		SetInteractionEnabled(true);
		return;
	}

	// PlayRaise 실패 경로는 함수 안에서 동기적으로 완료 이벤트를
	// 발행할 수 있으므로 반드시 호출 전에 델리게이트를 연결한다.
	BindClipboardDelegates();

	SetInteractionEnabled(false);

	ClipboardPresenter->PlayRaise();
}

void UCWStageSelectWidgetBase::BeginCloseStageSelect()
{
	if (bIsClosing || bCloseCompleted)
	{
		return;
	}

	bIsClosing = true;

	SetInteractionEnabled(false);

	if (!IsValid(ClipboardPresenter))
	{
		// 연출 시스템이 없으면 연출만 생략하고 즉시 닫기 완료 처리
		FinishCloseStageSelect();
		return;
	}

	ClipboardPresenter->PlayLower();
}

#pragma endregion

#pragma region PrivateFunctions

void UCWStageSelectWidgetBase::HandleClipboardRaiseFinished()
{
	// Raise 도중 닫기 상태가 복제된 경우,
	// 늦게 도착한 Raise 완료가 입력을 다시 열지 않도록 차단한다.
	if (bIsClosing)
	{
		return;
	}

	SetInteractionEnabled(true);
}

void UCWStageSelectWidgetBase::HandleClipboardLowerFinished()
{
	// 현재 위젯이 닫히는 중일 때만 Lower 완료를 처리한다.
	if (!bIsClosing)
	{
		return;
	}

	FinishCloseStageSelect();
}

void UCWStageSelectWidgetBase::ResolveClipboardPresenter()
{
	if (IsValid(ClipboardPresenter))
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	if (!IsValid(LocalPlayer))
	{
		return;
	}

	ClipboardPresenter =
		LocalPlayer->GetSubsystem<UCWClipboardPresenterSubsystem>();
}

void UCWStageSelectWidgetBase::BindClipboardDelegates()
{
	if (!IsValid(ClipboardPresenter))
	{
		return;
	}

	ClipboardPresenter->OnRaiseFinished.AddUniqueDynamic(
		this,
		&UCWStageSelectWidgetBase::HandleClipboardRaiseFinished
	);

	ClipboardPresenter->OnLowerFinished.AddUniqueDynamic(
		this,
		&UCWStageSelectWidgetBase::HandleClipboardLowerFinished
	);
}

void UCWStageSelectWidgetBase::UnbindClipboardDelegates()
{
	if (!IsValid(ClipboardPresenter))
	{
		return;
	}

	ClipboardPresenter->OnRaiseFinished.RemoveDynamic(
		this,
		&UCWStageSelectWidgetBase::HandleClipboardRaiseFinished
	);

	ClipboardPresenter->OnLowerFinished.RemoveDynamic(
		this,
		&UCWStageSelectWidgetBase::HandleClipboardLowerFinished
	);
}

void UCWStageSelectWidgetBase::SetInteractionEnabled(bool bEnabled)
{
	if (!IsValid(Panel_StageSelectInteraction))
	{
		return;
	}

	Panel_StageSelectInteraction->SetVisibility(
		bEnabled
		? OriginalInteractionVisibility
		: ESlateVisibility::HitTestInvisible
	);
}

void UCWStageSelectWidgetBase::FinishCloseStageSelect()
{
	if (bCloseCompleted)
	{
		return;
	}

	bCloseCompleted = true;

	OnClosePresentationFinished.Broadcast();
}

#pragma endregion
