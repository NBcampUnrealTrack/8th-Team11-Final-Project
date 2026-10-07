// CWConfirmDialogTest.cpp

#if WITH_DEV_AUTOMATION_TESTS

#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "UI/Feedback/CWBaseConfirmDialog.h"
#include "UI/Tests/CWConfirmDialogTestReceiver.h"

// 보호된 소멸·입력 경로를 테스트에서 부르기 위한 접근자
struct FCWConfirmDialogTestAccess
{
	static void Destroy(UCWBaseConfirmDialog* Dialog) { Dialog->NativeDestruct(); }

	static bool PressKey(UCWBaseConfirmDialog* Dialog, const FKey& Key)
	{
		const FKeyEvent KeyEvent(Key, FModifierKeysState(), 0, false, 0, 0);
		return Dialog->NativeOnKeyDown(FGeometry(), KeyEvent).IsEventHandled();
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCWConfirmDialogTest,
	"Crosswalk.UI.ConfirmDialog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCWConfirmDialogTest::RunTest(const FString& Parameters)
{
	// 새 팝업과 수신자를 만들어 콜백을 연결
	auto MakeDialog = [](UCWConfirmDialogTestReceiver* Receiver, bool bShowCancel = true)
	{
		UCWBaseConfirmDialog* Dialog = NewObject<UCWBaseConfirmDialog>(GetTransientPackage());
		FCWConfirmResultSignature OnConfirmed;
		OnConfirmed.BindUFunction(Receiver, TEXT("HandleConfirmed"));
		FCWConfirmResultSignature OnCancelled;
		OnCancelled.BindUFunction(Receiver, TEXT("HandleCancelled"));
		Dialog->Setup(FText::GetEmpty(), bShowCancel, OnConfirmed, OnCancelled);
		return Dialog;
	};

	// 확인: 확인 콜백만 1회, 이후 어떤 입력도 결과를 바꾸지 못함
	{
		UCWConfirmDialogTestReceiver* Receiver = NewObject<UCWConfirmDialogTestReceiver>();
		UCWBaseConfirmDialog* Dialog = MakeDialog(Receiver);
		Dialog->Confirm();
		Dialog->Confirm();
		Dialog->Cancel();
		TestEqual(TEXT("확인 콜백 1회"), Receiver->ConfirmedCount, 1);
		TestEqual(TEXT("확인 뒤 취소 콜백 없음"), Receiver->CancelledCount, 0);
	}

	// 취소: 취소 콜백만 1회
	{
		UCWConfirmDialogTestReceiver* Receiver = NewObject<UCWConfirmDialogTestReceiver>();
		UCWBaseConfirmDialog* Dialog = MakeDialog(Receiver);
		Dialog->Cancel();
		Dialog->Confirm();
		TestEqual(TEXT("취소 콜백 1회"), Receiver->CancelledCount, 1);
		TestEqual(TEXT("취소 뒤 확인 콜백 없음"), Receiver->ConfirmedCount, 0);
	}

	// ESC 는 취소 버튼과 같은 결과(확인 동작 없음, 취소 콜백 1회)
	{
		UCWConfirmDialogTestReceiver* Receiver = NewObject<UCWConfirmDialogTestReceiver>();
		UCWBaseConfirmDialog* Dialog = MakeDialog(Receiver);
		TestTrue(TEXT("ESC 는 처리됨"), FCWConfirmDialogTestAccess::PressKey(Dialog, EKeys::Escape));
		FCWConfirmDialogTestAccess::PressKey(Dialog, EKeys::Escape);
		TestEqual(TEXT("ESC 로 취소 콜백 1회"), Receiver->CancelledCount, 1);
		TestEqual(TEXT("ESC 로 확인 콜백 없음"), Receiver->ConfirmedCount, 0);
	}

	// 다른 키는 가져가지 않음
	{
		UCWConfirmDialogTestReceiver* Receiver = NewObject<UCWConfirmDialogTestReceiver>();
		UCWBaseConfirmDialog* Dialog = MakeDialog(Receiver);
		TestFalse(TEXT("ESC 가 아니면 처리하지 않음"), FCWConfirmDialogTestAccess::PressKey(Dialog, EKeys::A));
		TestEqual(TEXT("다른 키는 콜백 없음"), Receiver->CancelledCount + Receiver->ConfirmedCount, 0);
	}

	// 결과 없이 사라지면(맵 이동·소유자 파기) 어느 콜백도 부르지 않고, 그 뒤 입력도 무시함
	{
		UCWConfirmDialogTestReceiver* Receiver = NewObject<UCWConfirmDialogTestReceiver>();
		UCWBaseConfirmDialog* Dialog = MakeDialog(Receiver);
		FCWConfirmDialogTestAccess::Destroy(Dialog);
		Dialog->Cancel();
		Dialog->Confirm();
		FCWConfirmDialogTestAccess::PressKey(Dialog, EKeys::Escape);
		TestEqual(TEXT("파기 뒤 확인 콜백 없음"), Receiver->ConfirmedCount, 0);
		TestEqual(TEXT("파기 뒤 취소 콜백 없음"), Receiver->CancelledCount, 0);
	}

	// 콜백이 비어 있어도 크래시하지 않음(안내용)
	{
		UCWBaseConfirmDialog* Dialog = NewObject<UCWBaseConfirmDialog>(GetTransientPackage());
		Dialog->Setup(FText::GetEmpty(), false, FCWConfirmResultSignature(), FCWConfirmResultSignature());
		Dialog->Confirm();
		TestTrue(TEXT("콜백 없는 안내 팝업 확인"), true);
	}

	return true;
}

#endif
