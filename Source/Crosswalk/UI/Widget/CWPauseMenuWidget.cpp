#include "CWPauseMenuWidget.h"

#include "Engine/LocalPlayer.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "UI/CWUILog.h"
#include "UI/Core/CWUISubsystem.h"
#include "UI/ViewModel/CWSessionViewModel.h"

UCWPauseMenuWidget::UCWPauseMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

UCWUISubsystem* UCWPauseMenuWidget::GetUISubsystem() const
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	return IsValid(LocalPlayer) ? LocalPlayer->GetSubsystem<UCWUISubsystem>() : nullptr;
}

bool UCWPauseMenuWidget::OpenMenu()
{
	UCWUISubsystem* UI = GetUISubsystem();
	if (!IsValid(UI) || UI->GetCurrentPage() != ECWUIPage::None || bLeavingRoom)
	{
		return false;
	}

	bMenuOpen = true;
	SetVisibility(ESlateVisibility::Visible);
	UI->ApplyInputMode(ECWUIInputMode::UIOnly);
	RestoreFocus();

	return true;
}

void UCWPauseMenuWidget::ResumeGame()
{
	UCWUISubsystem* UI = GetUISubsystem();

	// 설정 같은 페이지가 위에 열려 있으면 그 화면이 먼저 닫혀야 함
	// 여기서 메뉴만 닫으면 페이지가 복원할 입력 모드가 UIOnly 로 굳어 게임 입력이 죽음
	if (!bMenuOpen || bLeavingRoom || !IsValid(UI) || UI->GetCurrentPage() != ECWUIPage::None)
	{
		return;
	}

	DismissForClear();
	UI->ApplyInputMode(ECWUIInputMode::GameAndUI);
}

void UCWPauseMenuWidget::OpenSettings()
{
	if (UCWUISubsystem* UI = GetUISubsystem(); IsValid(UI) && bMenuOpen && !bLeavingRoom)
	{
		// 설정 화면이 실제로 열렸을 때만 숨김
		// 먼저 숨기면 OpenPage 가 실패했을 때 메뉴도 설정도 없는 UIOnly 상태로 잠김
		// 열린 상태(bMenuOpen)는 유지해 설정을 닫으면 다시 돌아오게 함
		if (UI->OpenPage(ECWUIPage::Settings))
		{
			SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UCWPauseMenuWidget::ConfirmLeaveRoom()
{
	UCWUISubsystem* UI = GetUISubsystem();
	UCWSessionViewModel* ViewModel = IsValid(UI) ? UI->GetSessionViewModel() : nullptr;
	if (!bMenuOpen || bLeavingRoom || !IsValid(ViewModel))
	{
		return;
	}
	if (ViewModel->GetIsOperationInFlight())
	{
		UI->ShowToast(FText::FromString(TEXT("처리 중인 요청이 끝난 뒤 다시 시도해 주세요.")));
		return;
	}

	// 동기 완료도 놓치지 않도록 요청 전에 구독하고 잠금을 설정함
	SessionViewModel = ViewModel;
	ViewModel->OnSessionOperationCompleted.AddUniqueDynamic(this, &UCWPauseMenuWidget::HandleSessionCompleted);
	bLeavingRoom = true;
	SetIsEnabled(false);
	ViewModel->RequestDestroyRoom();
}

void UCWPauseMenuWidget::HandleSessionCompleted(ECWSessionOperation Operation, bool bSuccess)
{
	if (!bLeavingRoom || Operation != ECWSessionOperation::DestroyRoom)
	{
		return;
	}

	UCWSessionViewModel* ViewModel = SessionViewModel.Get();
	const FString MenuMap = IsValid(ViewModel) ? ViewModel->GetMenuMapPath() : FString();
	bLeavingRoom = false;
	SetIsEnabled(true);
	UnbindSession();
	if (bSuccess && !MenuMap.IsEmpty())
	{
		// 기존 설정 화면과 같은 종료 경로로 복귀 페이지 정책을 유지함
		DismissForClear();
		UGameplayStatics::OpenLevel(this, FName(*MenuMap));
	}
	else if (UCWUISubsystem* UI = GetUISubsystem(); IsValid(UI))
	{
		UI->ShowToast(IsValid(ViewModel) && !bSuccess ? ViewModel->GetLastFailureText()
			: FText::FromString(TEXT("메뉴 맵 설정을 확인해 주세요.")));
		RestoreFocus();
	}
}

void UCWPauseMenuWidget::DismissForClear()
{
	bMenuOpen = false;
	SetVisibility(ESlateVisibility::Collapsed);
	if (UCWUISubsystem* UI = GetUISubsystem(); IsValid(UI))
	{
		// 메뉴 위 확인창도 함께 닫아 클리어 화면을 가리지 않도록 함
		if (LeaveConfirmation.IsValid())
		{
			UI->ClosePopup(LeaveConfirmation.Get());
		}
	}
	LeaveConfirmation.Reset();
}

void UCWPauseMenuWidget::SetLeaveConfirmation(UUserWidget* InDialog)
{
	LeaveConfirmation = InDialog;
}

void UCWPauseMenuWidget::RestoreFocus()
{
	if (bMenuOpen && !bLeavingRoom)
	{
		// 설정 때문에 숨겨 둔 경우가 있어 포커스와 함께 표시도 되돌림
		SetVisibility(ESlateVisibility::Visible);
		SetUserFocus(GetOwningPlayer());
	}
}

void UCWPauseMenuWidget::UnbindSession()
{
	if (UCWSessionViewModel* ViewModel = SessionViewModel.Get(); IsValid(ViewModel))
	{
		ViewModel->OnSessionOperationCompleted.RemoveDynamic(this, &UCWPauseMenuWidget::HandleSessionCompleted);
	}
	SessionViewModel.Reset();
}

void UCWPauseMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 등록은 위젯 수명에 맞추고, 여는 것과는 분리함
	if (UCWUISubsystem* UI = GetUISubsystem(); IsValid(UI))
	{
		UI->SetPauseMenu(this);
	}
}

void UCWPauseMenuWidget::NativeDestruct()
{
	UnbindSession();
	bLeavingRoom = false;
	DismissForClear();

	// 닫기(DismissForClear)와 달리 등록 해제는 위젯이 사라질 때만 함
	if (UCWUISubsystem* UI = GetUISubsystem(); IsValid(UI))
	{
		UI->ClearPauseMenu(this);
	}

	Super::NativeDestruct();
}

FReply UCWPauseMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// 닫힌 뒤에도 포커스가 남아 있어 열려 있을 때만 키를 가져감
	// 조건 없이 Handled 를 돌려주면 다시 열 키까지 삼켜 한 번만 열림
	if (bMenuOpen && (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::One))
	{
		// 메뉴가 키를 가져간 경우를 남김 - 컨트롤러까지 안 내려간 이유를 가리기 위함
		UE_LOG(
			LogCWUI,
			Log,
			TEXT("[PauseMenu] key handled by menu. Key=%s"),
			*InKeyEvent.GetKey().ToString()
		);

		ResumeGame();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
