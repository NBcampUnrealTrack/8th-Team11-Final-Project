// CWSessionViewModel.h

#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "Data/Enums/CWSessionTypes.h"
#include "Data/Structs/CWRoomSummary.h"
#include "CWSessionViewModel.generated.h"

class UCWGameInstance;

// 로그인 요청 완료 결과를 UI 화면에 전달
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCWOnUILoginCompletedSignature,
	bool,
	bSuccess
);

// 세션 작업 완료 결과를 UI 화면에 전달
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FCWOnUISessionOperationCompletedSignature,
	ECWSessionOperation,
	Operation,
	bool,
	bSuccess
);

UCLASS(BlueprintType)
class CROSSWALK_API UCWSessionViewModel : public UMVVMViewModelBase
{
	GENERATED_BODY()

#pragma region ViewModelLifecycle

public:
	// GameInstance의 로그인 및 세션 이벤트 구독을 시작
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Session")
	void Initialize(UCWGameInstance* InGameInstance);

	// GameInstance에 등록된 로그인 및 세션 이벤트 구독을 정리
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Session")
	void Uninitialize();

#pragma endregion

#pragma region LoginRequests

public:
	// Epic Account Portal 로그인을 요청
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Session")
	void RequestLoginWithEpic();

	// Steam 인증 정보를 사용하는 로그인을 요청
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Session")
	void RequestLoginWithSteam();

	// Developer Authentication Tool의 Player1 로그인을 요청
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Session|Test")
	void RequestLoginAsPlayer1();

	// Developer Authentication Tool의 Player2 로그인을 요청
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Session|Test")
	void RequestLoginAsPlayer2();

#pragma endregion

#pragma region SessionRequests

public:
	// 새로운 세션 생성을 요청. 제목은 목록 표시용이고 비밀방은 목록에서만 숨김
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Session")
	void RequestCreateRoom(const FString& RoomName, bool bIsPrivate);

	// 현재 참가 가능한 공개 세션 검색을 요청
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Session")
	void RequestFindRooms();

	// 다른 화면·버튼으로 이동할 때 목록 조회만 조용히 취소
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Session")
	void RequestCancelRoomSearch();

	// 입력한 방 코드에 해당하는 세션 참가를 요청
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Session")
	void RequestJoinRoomByCode(const FString& RoomCode);

	// 현재 생성하거나 참가 중인 세션 제거를 요청
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Session")
	void RequestDestroyRoom();

#pragma endregion

#pragma region SessionState

public:
	// 이전 화면에서 발생한 세션 실패 상태를 초기화
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Session")
	void ClearLastFailure();

	// 마지막 세션 실패 원인을 사용자용 문구로 변환
	UFUNCTION(
		BlueprintPure,
		FieldNotify,
		Category = "CW|UI|Session"
	)
	FText GetLastFailureText() const;

	// 현재 로그인 여부를 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Session")
	bool GetIsLoggedIn() const
	{
		return bIsLoggedIn;
	}

	// 현재 로그인된 사용자 이름을 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Session")
	const FString& GetLoggedInDisplayName() const
	{
		return LoggedInDisplayName;
	}

	// 마지막 로그인 실패 원인을 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Session")
	ECWLoginFailure GetLastLoginFailure() const
	{
		return LastLoginFailure;
	}

	// 마지막 로그인 실패 원인을 사용자용 문구로 변환
	UFUNCTION(
		BlueprintPure,
		FieldNotify,
		Category = "CW|UI|Session"
	)
	FText GetLastLoginFailureText() const;

	// 현재 처리 중이거나 마지막으로 완료된 세션 작업을 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Session")
	ECWSessionOperation GetCurrentOperation() const
	{
		return CurrentOperation;
	}

	// 마지막 세션 실패 원인을 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Session")
	ECWSessionFailure GetLastFailure() const
	{
		return LastFailure;
	}

	// 현재 세션 작업이 진행 중인지 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Session")
	bool GetIsOperationInFlight() const
	{
		return bIsOperationInFlight;
	}

	// 새로고침 중복 요청은 막되 다른 버튼은 별도의 잠금 상태를 사용
	UFUNCTION(BlueprintPure, FieldNotify, Category = "CW|UI|Session")
	bool GetCanRefreshRooms() const
	{
		return !bIsOperationInFlight;
	}

	// 현재 방 코드를 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Session")
	const FString& GetCurrentRoomCode() const
	{
		return CurrentRoomCode;
	}

	// 방을 나간 뒤 돌아갈 메뉴 맵 경로를 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Session")
	FString GetMenuMapPath() const;

	// 현재 방 코드를 UI TextBlock에 표시할 텍스트로 반환
	UFUNCTION(
		BlueprintPure,
		FieldNotify,
		Category = "CW|UI|Session"
	)
	FText GetCurrentRoomCodeText() const;

	// 현재 방 제목을 표시용 텍스트로 반환. 비어 있을 수 있음
	UFUNCTION(
		BlueprintPure,
		FieldNotify,
		Category = "CW|UI|Session"
	)
	FText GetCurrentRoomNameText() const;

	// 마지막으로 검색된 공개 방 목록을 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Session")
	const TArray<FCWRoomSummary>& GetRoomList() const
	{
		return RoomList;
	}

#pragma endregion

#pragma region GameInstanceBinding

private:
	// 로그인 작업이 끝나면 ViewModel 로그인 상태를 갱신
	UFUNCTION()
	void HandleLoginStateChanged(
		bool bSuccess,
		const FString& DisplayName,
		ECWLoginFailure Failure
	);

	// 세션 작업이 끝나면 작업 상태와 방 코드를 갱신
	UFUNCTION()
	void HandleSessionOperationCompleted(
		ECWSessionOperation Operation,
		bool bSuccess,
		ECWSessionFailure Failure
	);

	// 공개 방 검색이 끝나면 방 목록을 갱신
	UFUNCTION()
	void HandleRoomListUpdated(
		const TArray<FCWRoomSummary>& Rooms
	);

#pragma endregion

#pragma region StateSetters

private:
	// 로그인 여부를 변경하고 FieldNotify를 발생
	void SetIsLoggedIn(bool bInIsLoggedIn);

	// 로그인 사용자 이름을 변경하고 FieldNotify를 발생
	void SetLoggedInDisplayName(const FString& InDisplayName);

	// 마지막 로그인 실패 원인을 변경하고 FieldNotify를 발생
	void SetLastLoginFailure(ECWLoginFailure InFailure);

	// 현재 세션 작업 종류를 변경하고 FieldNotify를 발생
	void SetCurrentOperation(ECWSessionOperation InOperation);

	// 마지막 세션 실패 원인을 변경하고 FieldNotify를 발생
	void SetLastFailure(ECWSessionFailure InFailure);

	// 세션 작업 진행 여부를 변경하고 FieldNotify를 발생
	void SetOperationInFlight(bool bInFlight);

	// 현재 방 코드를 변경하고 FieldNotify를 발생
	void SetCurrentRoomCode(const FString& InRoomCode);

	// 현재 방 제목을 변경하고 FieldNotify를 발생
	void SetCurrentRoomName(const FString& InRoomName);

	// 현재 방 호스트 이름을 변경하고 FieldNotify를 발생
	void SetCurrentHostName(const FString& InHostName);

	// 공개 방 목록을 변경하고 FieldNotify를 발생
	void SetRoomList(const TArray<FCWRoomSummary>& InRooms);

#pragma endregion

#pragma region ViewModelState

private:
	// 목록 검색은 제외하고 생성·참가·복구 중에만 다른 버튼을 잠금
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "CW|UI|Session", meta = (AllowPrivateAccess = "true"))
	bool bAreRoomActionsLocked = false;

	// 현재 로컬 사용자의 로그인 여부
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Session",
		meta = (AllowPrivateAccess = "true")
	)
	bool bIsLoggedIn = false;

	// UI에 표시할 현재 로그인 사용자 이름
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Session",
		meta = (AllowPrivateAccess = "true")
	)
	FString LoggedInDisplayName;

	// 마지막 로그인 실패 원인
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Session",
		meta = (AllowPrivateAccess = "true")
	)
	ECWLoginFailure LastLoginFailure = ECWLoginFailure::None;

	// 현재 처리 중이거나 마지막으로 완료된 세션 작업
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Session",
		meta = (AllowPrivateAccess = "true")
	)
	ECWSessionOperation CurrentOperation = ECWSessionOperation::None;

	// 마지막 세션 작업 실패 원인
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Session",
		meta = (AllowPrivateAccess = "true")
	)
	ECWSessionFailure LastFailure = ECWSessionFailure::None;

	// 세션 작업 중 중복 입력을 막기 위한 상태
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Session",
		meta = (AllowPrivateAccess = "true")
	)
	bool bIsOperationInFlight = false;

	// 현재 생성하거나 참가한 세션의 방 코드
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Session",
		meta = (AllowPrivateAccess = "true")
	)
	FString CurrentRoomCode;

	// 현재 생성하거나 참가한 세션의 방 제목
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Session",
		meta = (AllowPrivateAccess = "true")
	)
	FString CurrentRoomName;

	// 현재 방 호스트 이름. 제목이 비었을 때 대체 문구에 씀
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Session",
		meta = (AllowPrivateAccess = "true")
	)
	FString CurrentHostName;

	// 공개 방 목록 화면에 표시할 검색 결과
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Session",
		meta = (AllowPrivateAccess = "true")
	)
	TArray<FCWRoomSummary> RoomList;

	// 현재 이벤트를 구독 중인 GameInstance
	UPROPERTY()
	TWeakObjectPtr<UCWGameInstance> BoundGameInstance;

#pragma endregion

#pragma region ViewModelEvents

public:
	// 로그인 요청이 완료되면 성공 여부를 화면에 전달
	UPROPERTY(BlueprintAssignable, Category = "CW|UI|Session")
	FCWOnUILoginCompletedSignature OnLoginCompleted;

	// 세션 작업이 완료되면 작업 종류와 성공 여부를 화면에 전달
	UPROPERTY(BlueprintAssignable, Category = "CW|UI|Session")
	FCWOnUISessionOperationCompletedSignature OnSessionOperationCompleted;

#pragma endregion

};
