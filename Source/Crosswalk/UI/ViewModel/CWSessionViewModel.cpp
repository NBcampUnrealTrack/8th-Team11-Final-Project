// CWSessionViewModel.cpp

#include "UI/ViewModel/CWSessionViewModel.h"
#include "CoreMinimal.h"
#include "Core/CWGameInstance.h"

#pragma region ViewModelLifecycle

void UCWSessionViewModel::Initialize(UCWGameInstance* InGameInstance)
{
	// 재초기화 시 델리게이트가 중복 등록되지 않도록 기존 구독 정리
	Uninitialize();

	if (!IsValid(InGameInstance))
	{
		return;
	}

	BoundGameInstance = InGameInstance;

	// 요청 반환이나 완료 이벤트를 기다리지 않고 입력 잠금을 동기화
	InGameInstance->OnSessionBusyChanged.AddUObject(this, &UCWSessionViewModel::SetOperationInFlight);

	// 로그인 상태 변경 이벤트 구독
	InGameInstance->OnLoginStateChanged.AddDynamic(
		this,
		&UCWSessionViewModel::HandleLoginStateChanged
	);

	// 세션 작업 완료 이벤트 구독
	InGameInstance->OnSessionOperationCompleted.AddDynamic(
		this,
		&UCWSessionViewModel::HandleSessionOperationCompleted
	);

	// 방 정보 초기화 이벤트 구독
	// 완료가 실패로 끝나거나 아예 방송되지 않는 경로에서도 방 코드를 맞춤
	InGameInstance->OnRoomIdentityCleared.AddDynamic(
		this,
		&UCWSessionViewModel::HandleRoomIdentityCleared
	);

	// 공개 방 목록 변경 이벤트 구독
	InGameInstance->OnRoomListUpdated.AddDynamic(
		this,
		&UCWSessionViewModel::HandleRoomListUpdated
	);

	// ViewModel 생성 전에 이벤트가 발생한 경우를 위해 현재 값을 즉시 반영
	SetIsLoggedIn(InGameInstance->IsLoggedIn());
	SetLoggedInDisplayName(InGameInstance->GetLoggedInDisplayName());
	SetLastLoginFailure(ECWLoginFailure::None);
	SetCurrentOperation(ECWSessionOperation::None);
	SetLastFailure(ECWSessionFailure::None);
	SetOperationInFlight(
		InGameInstance->IsSessionOperationInFlight()
	);
	SetCurrentRoomCode(InGameInstance->GetCurrentRoomCode());
	SetCurrentRoomName(InGameInstance->GetCurrentRoomName());
	SetCurrentHostName(InGameInstance->GetCurrentHostName());
	SetRoomList(InGameInstance->GetCachedRoomList());
}

void UCWSessionViewModel::Uninitialize()
{
	// 현재 GameInstance에 등록한 동적 델리게이트 구독 해제
	if (UCWGameInstance* GameInstance = BoundGameInstance.Get())
	{
		GameInstance->OnSessionBusyChanged.RemoveAll(this);

		GameInstance->OnLoginStateChanged.RemoveDynamic(
			this,
			&UCWSessionViewModel::HandleLoginStateChanged
		);

		GameInstance->OnSessionOperationCompleted.RemoveDynamic(
			this,
			&UCWSessionViewModel::HandleSessionOperationCompleted
		);

		GameInstance->OnRoomIdentityCleared.RemoveDynamic(
			this,
			&UCWSessionViewModel::HandleRoomIdentityCleared
		);

		GameInstance->OnRoomListUpdated.RemoveDynamic(
			this,
			&UCWSessionViewModel::HandleRoomListUpdated
		);
	}

	BoundGameInstance.Reset();

	// 이전 GameInstance의 상태가 남지 않도록 ViewModel 초기화
	SetIsLoggedIn(false);
	SetLoggedInDisplayName(FString());
	SetLastLoginFailure(ECWLoginFailure::None);
	SetCurrentOperation(ECWSessionOperation::None);
	SetLastFailure(ECWSessionFailure::None);
	SetOperationInFlight(false);
	SetCurrentRoomCode(FString());
	SetCurrentRoomName(FString());
	SetCurrentHostName(FString());
	SetRoomList(TArray<FCWRoomSummary>());
}

#pragma endregion

#pragma region LoginRequests

void UCWSessionViewModel::RequestLoginWithEpic()
{
	UCWGameInstance* GameInstance = BoundGameInstance.Get();

	if (!IsValid(GameInstance))
	{
		return;
	}

	// Epic Account Portal 로그인 요청을 GameInstance에 전달
	GameInstance->LoginWithEpic();
}

void UCWSessionViewModel::RequestLoginWithSteam()
{
	UCWGameInstance* GameInstance = BoundGameInstance.Get();

	if (!IsValid(GameInstance))
	{
		return;
	}

	// Steam 인증 정보를 사용하는 로그인 요청 전달
	GameInstance->LoginWithSteam();
}

void UCWSessionViewModel::RequestLoginAsPlayer1()
{
	UCWGameInstance* GameInstance = BoundGameInstance.Get();

	if (!IsValid(GameInstance))
	{
		return;
	}

	// Developer Authentication Tool의 Player1 로그인 요청 전달
	GameInstance->LoginAsPlayer1();
}

void UCWSessionViewModel::RequestLoginAsPlayer2()
{
	UCWGameInstance* GameInstance = BoundGameInstance.Get();

	if (!IsValid(GameInstance))
	{
		return;
	}

	// Developer Authentication Tool의 Player2 로그인 요청 전달
	GameInstance->LoginAsPlayer2();
}

#pragma endregion

#pragma region SessionRequests

FString UCWSessionViewModel::GetMenuMapPath() const
{
	const UCWGameInstance* GameInstance =
		BoundGameInstance.Get();

	if (!IsValid(GameInstance))
	{
		return FString();
	}

	// 위젯이 GameInstance를 직접 참조하지 않도록 설정값을 중계
	return GameInstance->GetMenuMapPath();
}

FText UCWSessionViewModel::GetCurrentRoomCodeText() const
{
	// 세션 내부 문자열을 UI 표시용 텍스트로 변환
	return FText::FromString(CurrentRoomCode);
}

FText UCWSessionViewModel::GetCurrentRoomNameText() const
{
	if (!CurrentRoomName.IsEmpty())
	{
		return FText::FromString(CurrentRoomName);
	}

	// 호스트가 제목을 비워 둔 경우 "<호스트>님의 방" 으로 대체
	if (!CurrentHostName.IsEmpty())
	{
		return FText::Format(
			NSLOCTEXT("CWSession", "RoomNameFallback", "{0}님의 방"),
			FText::FromString(CurrentHostName)
		);
	}

	// 둘 다 없으면 빈 텍스트 - 표시 여부는 위젯이 정함
	return FText::GetEmpty();
}

void UCWSessionViewModel::RequestCreateRoom(const FString& RoomName, bool bIsPrivate)
{
	UCWGameInstance* GameInstance = BoundGameInstance.Get();

	if (!IsValid(GameInstance))
	{
		return;
	}

	// 방 생성 요청을 GameInstance에 전달
	GameInstance->CreateRoom(RoomName, bIsPrivate);

	// 요청이 실제로 시작됐는지 GameInstance의 상태를 다시 읽음
	SetOperationInFlight(
		GameInstance->IsSessionOperationInFlight()
	);
}

void UCWSessionViewModel::RequestFindRooms()
{
	UCWGameInstance* GameInstance = BoundGameInstance.Get();

	if (!IsValid(GameInstance))
	{
		return;
	}

	// 공개 방 검색 요청을 GameInstance에 전달
	GameInstance->FindRooms();

	// 동기 실패 가능성이 있으므로 실제 진행 상태를 다시 읽음
	SetOperationInFlight(
		GameInstance->IsSessionOperationInFlight()
	);
}

void UCWSessionViewModel::RequestCancelRoomSearch()
{
	if (UCWGameInstance* GameInstance = BoundGameInstance.Get())
	{
		GameInstance->CancelRoomSearch();
	}
}

void UCWSessionViewModel::RequestJoinRoomByCode(
	const FString& RoomCode
)
{
	UCWGameInstance* GameInstance = BoundGameInstance.Get();

	if (!IsValid(GameInstance))
	{
		return;
	}

	// 공백만 입력된 코드는 불필요한 전체 검색 전에 차단
	const FString NormalizedRoomCode =
		RoomCode.TrimStartAndEnd();

	if (NormalizedRoomCode.IsEmpty())
	{
		return;
	}

	// 입력한 방 코드로 세션 참가 요청 전달
	GameInstance->JoinRoomByCode(NormalizedRoomCode);

	// 요청이 실제로 시작됐는지 GameInstance의 상태를 다시 읽음
	SetOperationInFlight(
		GameInstance->IsSessionOperationInFlight()
	);
}

void UCWSessionViewModel::RequestDestroyRoom()
{
	UCWGameInstance* GameInstance = BoundGameInstance.Get();

	if (!IsValid(GameInstance))
	{
		return;
	}

	// 현재 생성하거나 참가 중인 세션 제거 요청 전달
	GameInstance->DestroyRoom();

	// 동기 실패 가능성이 있으므로 실제 진행 상태를 다시 읽음
	SetOperationInFlight(
		GameInstance->IsSessionOperationInFlight()
	);
}

#pragma endregion

#pragma region SessionState

void UCWSessionViewModel::ClearLastFailure()
{
	// 다른 화면에서 발생한 이전 세션 결과가 남지 않도록 초기화
	SetCurrentOperation(ECWSessionOperation::None);
	SetLastFailure(ECWSessionFailure::None);
}

FText UCWSessionViewModel::GetLastLoginFailureText() const
{
	// 내부 로그인 실패 타입을 사용자가 이해할 수 있는 문구로 변환
	switch (LastLoginFailure)
	{
	case ECWLoginFailure::None:
		return FText::GetEmpty();

	case ECWLoginFailure::Cancelled:
		return NSLOCTEXT(
			"CWSessionUI",
			"LoginCancelled",
			"로그인이 취소되었습니다."
		);

	case ECWLoginFailure::NoConnection:
		return NSLOCTEXT(
			"CWSessionUI",
			"LoginNoConnection",
			"네트워크 연결을 확인해 주세요."
		);

	case ECWLoginFailure::ServiceUnavailable:
		return NSLOCTEXT(
			"CWSessionUI",
			"LoginServiceUnavailable",
			"로그인 서비스를 사용할 수 없습니다."
		);

	case ECWLoginFailure::NotConfigured:
		return NSLOCTEXT(
			"CWSessionUI",
			"LoginNotConfigured",
			"로그인 설정을 확인해 주세요."
		);

	case ECWLoginFailure::Unknown:
	default:
		return NSLOCTEXT(
			"CWSessionUI",
			"LoginUnknownFailure",
			"로그인에 실패했습니다. 잠시 후 다시 시도해 주세요."
		);
	}
}

FText UCWSessionViewModel::GetLastFailureText() const
{
	// 내부 실패 타입을 사용자가 이해할 수 있는 문구로 변환
	switch (LastFailure)
	{
	case ECWSessionFailure::None:
		return FText::GetEmpty();

	case ECWSessionFailure::NotLoggedIn:
		return NSLOCTEXT(
			"CWSessionUI",
			"NotLoggedIn",
			"로그인이 필요합니다."
		);

	case ECWSessionFailure::RoomNotFound:
		return NSLOCTEXT(
			"CWSessionUI",
			"RoomNotFound",
			"방을 찾을 수 없습니다."
		);

	case ECWSessionFailure::RoomFull:
		return NSLOCTEXT(
			"CWSessionUI",
			"RoomFull",
			"방의 인원이 가득 찼습니다."
		);

	case ECWSessionFailure::AlreadyInSession:
		return NSLOCTEXT(
			"CWSessionUI",
			"AlreadyInSession",
			"이미 다른 방에 참가 중입니다."
		);

	case ECWSessionFailure::Timeout:
		return NSLOCTEXT(
			"CWSessionUI",
			"Timeout",
			"요청 시간이 초과되었습니다."
		);

	case ECWSessionFailure::RoomCodeCollision:
	case ECWSessionFailure::Unknown:
	default:
		return NSLOCTEXT(
			"CWSessionUI",
			"UnknownFailure",
			"요청을 처리하지 못했습니다. 잠시 후 다시 시도해 주세요."
		);
	}
}

#pragma endregion

#pragma region GameInstanceBinding

void UCWSessionViewModel::HandleLoginStateChanged(
	bool bSuccess,
	const FString& DisplayName,
	ECWLoginFailure Failure
)
{
	// GameInstance가 전달한 최신 로그인 상태 저장
	SetIsLoggedIn(bSuccess);
	SetLoggedInDisplayName(DisplayName);
	SetLastLoginFailure(Failure);

	// 상태값 변경 여부와 관계없이 로그인 요청 완료를 화면에 전달
	OnLoginCompleted.Broadcast(bSuccess);
}

void UCWSessionViewModel::HandleSessionOperationCompleted(
	ECWSessionOperation Operation,
	bool bSuccess,
	ECWSessionFailure Failure
)
{
	// 완료된 작업 종류와 실패 원인 저장
	SetCurrentOperation(Operation);
	SetLastFailure(Failure);

	UCWGameInstance* GameInstance = BoundGameInstance.Get();

	if (IsValid(GameInstance))
	{
		// 작업 완료 후 실제 진행 상태와 방 코드를 다시 읽음
		SetOperationInFlight(
			GameInstance->IsSessionOperationInFlight()
		);

		if (
			bSuccess &&
			(
				Operation == ECWSessionOperation::CreateRoom ||
				Operation == ECWSessionOperation::JoinRoom ||
				Operation == ECWSessionOperation::DestroyRoom
				)
			)
		{
			SetCurrentRoomCode(
				GameInstance->GetCurrentRoomCode()
			);

			SetCurrentRoomName(
				GameInstance->GetCurrentRoomName()
			);

			SetCurrentHostName(
				GameInstance->GetCurrentHostName()
			);
		}
	}
	else
	{
		SetOperationInFlight(false);
	}

	// 상태 갱신이 모두 끝난 뒤 세션 작업 완료를 화면에 전달
	OnSessionOperationCompleted.Broadcast(
		Operation,
		bSuccess
	);
}

void UCWSessionViewModel::HandleRoomIdentityCleared()
{
	UCWGameInstance* GameInstance = BoundGameInstance.Get();

	if (!IsValid(GameInstance))
	{
		return;
	}

	// 세 값은 항상 같이 움직이므로 한 번에 맞춤
	SetCurrentRoomCode(GameInstance->GetCurrentRoomCode());
	SetCurrentRoomName(GameInstance->GetCurrentRoomName());
	SetCurrentHostName(GameInstance->GetCurrentHostName());
}

void UCWSessionViewModel::HandleRoomListUpdated(
	const TArray<FCWRoomSummary>& Rooms
)
{
	// GameInstance가 검색한 최신 공개 방 목록 저장
	SetRoomList(Rooms);
}

#pragma endregion

#pragma region StateSetters

void UCWSessionViewModel::SetIsLoggedIn(bool bInIsLoggedIn)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		bIsLoggedIn,
		bInIsLoggedIn
	);
}

void UCWSessionViewModel::SetLoggedInDisplayName(
	const FString& InDisplayName
)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		LoggedInDisplayName,
		InDisplayName
	);
}

void UCWSessionViewModel::SetLastLoginFailure(
	ECWLoginFailure InFailure
)
{
	// 실패 원인이 변경되면 파생된 사용자 문구도 함께 갱신
	if (
		UE_MVVM_SET_PROPERTY_VALUE(
			LastLoginFailure,
			InFailure
		)
		)
	{
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(
			GetLastLoginFailureText
		);
	}
}

void UCWSessionViewModel::SetCurrentOperation(
	ECWSessionOperation InOperation
)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		CurrentOperation,
		InOperation
	);
}

void UCWSessionViewModel::SetLastFailure(
	ECWSessionFailure InFailure
)
{
	// 실패 원인이 변경되면 파생된 사용자 문구도 함께 갱신
	if (
		UE_MVVM_SET_PROPERTY_VALUE(
			LastFailure,
			InFailure
		)
		)
	{
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(
			GetLastFailureText
		);
	}
}

void UCWSessionViewModel::SetOperationInFlight(bool bInFlight)
{
	const UCWGameInstance* GameInstance = BoundGameInstance.Get();
	const bool bSearchingRooms = IsValid(GameInstance) && GameInstance->IsRoomSearchInProgress();
	UE_MVVM_SET_PROPERTY_VALUE(bAreRoomActionsLocked, bInFlight && !bSearchingRooms);

	if (UE_MVVM_SET_PROPERTY_VALUE(
		bIsOperationInFlight,
		bInFlight
	))
	{
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetCanRefreshRooms);
		UE_LOG(LogTemp, Verbose, TEXT("[SessionDiag] ViewModel Busy=%d"), bInFlight);
	}
}

void UCWSessionViewModel::SetCurrentRoomCode(
	const FString& InRoomCode
)
{
	// 방 코드가 실제로 변경된 경우 문자열과 파생 텍스트 모두 갱신
	if (UE_MVVM_SET_PROPERTY_VALUE(
		CurrentRoomCode,
		InRoomCode
	))
	{
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(
			GetCurrentRoomCodeText
		);
	}
}

void UCWSessionViewModel::SetCurrentRoomName(
	const FString& InRoomName
)
{
	// 방 제목이 실제로 변경된 경우 문자열과 파생 텍스트 모두 갱신
	if (UE_MVVM_SET_PROPERTY_VALUE(
		CurrentRoomName,
		InRoomName
	))
	{
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(
			GetCurrentRoomNameText
		);
	}
}

void UCWSessionViewModel::SetCurrentHostName(
	const FString& InHostName
)
{
	// 호스트 이름이 바뀌면 제목 텍스트의 대체 문구도 같이 갱신
	if (UE_MVVM_SET_PROPERTY_VALUE(
		CurrentHostName,
		InHostName
	))
	{
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(
			GetCurrentRoomNameText
		);
	}
}

void UCWSessionViewModel::SetRoomList(
	const TArray<FCWRoomSummary>& InRooms
)
{
	// 방 목록 내용이 실제로 변경된 경우에만 값을 갱신하고 FieldNotify 발생
	UE_MVVM_SET_PROPERTY_VALUE(
		RoomList,
		InRooms
	);
}

#pragma endregion
