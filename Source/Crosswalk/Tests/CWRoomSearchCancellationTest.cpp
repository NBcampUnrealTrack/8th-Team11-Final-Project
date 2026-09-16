// Copyright Crosswalk Team. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/CWGameInstance.h"
#include "Misc/AutomationTest.h"
#include "OnlineSessionSettings.h"
#include "TimerManager.h"
#include "UI/ViewModel/CWSessionViewModel.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCWRoomSearchCancellationTest,
	"Crosswalk.Session.RoomSearchCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCWRoomSearchCancellationTest::RunTest(const FString& Parameters)
{
	// 외부 세션 없이 취소·타임아웃·늦은 응답의 상태 전이만 검증
	UCWGameInstance* GameInstance = NewObject<UCWGameInstance>();
	UCWSessionViewModel* ViewModel = NewObject<UCWSessionViewModel>();
	ViewModel->Initialize(GameInstance);
	const FBoolProperty* ActionsLocked = FindFProperty<FBoolProperty>(
		UCWSessionViewModel::StaticClass(), TEXT("bAreRoomActionsLocked"));
	if (!TestNotNull(TEXT("Room action lock field exists"), ActionsLocked))
	{
		ViewModel->Uninitialize();
		return false;
	}

	TestTrue(TEXT("Refresh starts"), GameInstance->BeginSessionOperation(ECWSessionOperation::FindRooms));
	TestEqual(TEXT("Refresh timeout is five seconds"),
		GameInstance->GetTimerManager().GetTimerRate(GameInstance->SessionOperationTimeoutHandle), 5.f);
	TestFalse(TEXT("Only refresh is disabled"), ViewModel->GetCanRefreshRooms());
	TestFalse(TEXT("Other buttons remain available"), ActionsLocked->GetPropertyValue_InContainer(ViewModel));

	GameInstance->SessionSearch = MakeShared<FOnlineSessionSearch>();
	GameInstance->bAwaitingSearchCompletion = true;
	GameInstance->CurrentFindPurpose = ECWSessionFindPurpose::ManualRefresh;
	TestTrue(TEXT("Join supersedes refresh"), GameInstance->BeginSessionOperation(ECWSessionOperation::JoinRoom));
	TestTrue(TEXT("Cancelled refresh result is discarded"), GameInstance->bDiscardSearchResult);
	TestTrue(TEXT("Join still locks conflicting actions"), ActionsLocked->GetPropertyValue_InContainer(ViewModel));
	TestEqual(TEXT("Join retains its existing timeout"),
		GameInstance->GetTimerManager().GetTimerRate(GameInstance->SessionOperationTimeoutHandle), 30.f);

	GameInstance->DeferredFindPurpose = ECWSessionFindPurpose::JoinByRoomCode;
	GameInstance->HandleSessionOperationTimeout();
	TestTrue(TEXT("Timeout releases refresh"), ViewModel->GetCanRefreshRooms());
	TestTrue(TEXT("Timeout drops deferred join"), GameInstance->DeferredFindPurpose == ECWSessionFindPurpose::None);
	GameInstance->OnFindSessionsComplete(true);
	TestFalse(TEXT("Late response does not restart timed-out join"), GameInstance->IsSessionOperationInFlight());
	TestFalse(TEXT("Late response drains the old search"), GameInstance->bAwaitingSearchCompletion);

	GameInstance->BeginSessionOperation(ECWSessionOperation::FindRooms);
	GameInstance->CancelRoomSearch();
	GameInstance->CancelRoomSearch();
	TestFalse(TEXT("Repeated cancellation is harmless"), GameInstance->IsSessionOperationInFlight());
	TestFalse(TEXT("Cancelled search has no timeout"),
		GameInstance->GetTimerManager().TimerExists(GameInstance->SessionOperationTimeoutHandle));
	TestTrue(TEXT("Refresh can be used after cancel"), ViewModel->GetCanRefreshRooms());
	ViewModel->Uninitialize();
	return true;
}

#endif
