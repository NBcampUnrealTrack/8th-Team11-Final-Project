#include "UI/Feedback/CWBaseConfirmDialog.h"

#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "InputCoreTypes.h"
#include "Input/Reply.h"
#include "UI/Core/CWUISubsystem.h"

UCWBaseConfirmDialog::UCWBaseConfirmDialog(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// ESC 가 이 팝업까지 오려면 팝업이 포커스를 받을 수 있어야 함
	SetIsFocusable(true);
}

void UCWBaseConfirmDialog::Setup(const FText& Message, bool bShowCancel, FCWConfirmResultSignature InOnConfirmed, FCWConfirmResultSignature InOnCancelled)
{
	OnConfirmed = InOnConfirmed;
	OnCancelled = InOnCancelled;
	bResolved = false;

	SetMessage(Message);
	if (WBP_ButtonCommon_Cancel)
	{
		WBP_ButtonCommon_Cancel->SetVisibility(bShowCancel ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UCWBaseConfirmDialog::SetMessage(FText Message)
{
	if (Text_Message)
	{
		Text_Message->SetText(Message);
	}
}

void UCWBaseConfirmDialog::Confirm()
{
	Resolve(true);
}

void UCWBaseConfirmDialog::Cancel()
{
	Resolve(false);
}

void UCWBaseConfirmDialog::Resolve(bool bConfirmed)
{
	if (bResolved)
	{
		return;
	}
	bResolved = true;

	// 콜백 안에서 다시 이 팝업이 결과를 내지 않도록 먼저 떼어 둠
	const FCWConfirmResultSignature Callback = bConfirmed ? OnConfirmed : OnCancelled;
	OnConfirmed.Unbind();
	OnCancelled.Unbind();
	Callback.ExecuteIfBound();

	// 콜백이 일시정지 메뉴 정리 등으로 이미 닫았으면 다시 닫지 않음
	if (GetParent() == nullptr)
	{
		return;
	}
	if (const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		if (UCWUISubsystem* UI = LocalPlayer->GetSubsystem<UCWUISubsystem>())
		{
			UI->ClosePopup(this);
		}
	}
}

void UCWBaseConfirmDialog::NativeDestruct()
{
	// 결과 없이 사라지는 경우(맵 이동·소유자 파기·밖에서 닫힘)는 업무 콜백 없이 정리만 함
	// 취소 콜백이 이전 화면을 갱신하거나 새 UI 를 여는 일이 파기 중에 일어나면 안 되기 때문임
	bResolved = true;
	OnConfirmed.Unbind();
	OnCancelled.Unbind();

	Super::NativeDestruct();
}

FReply UCWBaseConfirmDialog::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// ESC 는 취소 버튼과 같은 결과로 처리하며, 포커스를 가진 동안만 가져감
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		Cancel();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
