// Copyright Crosswalk Team. All Rights Reserved.
// CWGameInstance.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Data/Enums/CWSessionTypes.h"
#include "Data/Structs/CWRoomSummary.h"
#include "CWGameInstance.generated.h"

class UNetDriver;

// 완료 이벤트와 별개로 작업 시작·복구 상태를 ViewModel에 전달
DECLARE_MULTICAST_DELEGATE_OneParam(FCWOnSessionBusyChanged, bool);

// 세션 검색을 어떤 목적으로 실행했는지 구분
enum class ECWSessionFindPurpose : uint8
{
	None,
	CheckRoomCodeForCreate,
	JoinByRoomCode,
	ManualRefresh
};

// 로그인 결과를 UI에 전달
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FCWOnLoginStateChangedSignature,
	bool,
	bSuccess,
	const FString&,
	DisplayName,
	ECWLoginFailure,
	Failure
);

// 세션 작업 결과를 UI에 전달
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FCWOnSessionOperationCompletedSignature,
	ECWSessionOperation,
	Operation,
	bool,
	bSuccess,
	ECWSessionFailure,
	Failure
);

// 수동 검색으로 갱신된 방 목록을 UI에 전달
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCWOnRoomListUpdatedSignature,
	const TArray<FCWRoomSummary>&,
	Rooms
);

UCLASS()
class CROSSWALK_API UCWGameInstance : public UGameInstance
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FCWRoomSearchCancellationTest;
#endif

public:
	UCWGameInstance();

	FCWOnSessionBusyChanged OnSessionBusyChanged;

	// 게임 시작 시 온라인 서브시스템 인터페이스를 캐싱
	virtual void Init() override;

	// 게임 종료 시 등록한 온라인 델리게이트를 정리
	virtual void Shutdown() override;

	// Steam 클라이언트 인증 정보로 로그인
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Online")
	void LoginWithSteam();

	/**
	 * [EOS 제거 브릿지, WBP_Title 정리 전까지만 유지] EOS 전체를 걷어내면서 이 함수들이 실제로
	 * EOS에 로그인하던 기능은 사라졌다(Crosswalk_EOS_아카이브_기록.md 참고) - 그러나
	 * CWSessionViewModel/WBP_Title은 UI 담당(현준님) 영역이라 이 세션에서 건드리지 않기로
	 * 했고, 그 블루프린트가 여전히 이 함수들을 호출하고 있어서 시그니처만 남겨둔다.
	 *
	 * 호출되면 CacheOnlineInterfaces()가 지금 기본 OnlineSubsystem(Steam)을 그대로 가져오고,
	 * 그 인터페이스에 "accountportal"/"developer" 크리덴셜을 넣어 Login()을 시도한다 - Steam
	 * 쪽에서 이해 못 하는 타입이라 정상적인 로그인 실패 흐름(OnLoginStateChanged 등)으로
	 * 자연스럽게 실패한다. 별도 "지원 안 함" 처리를 새로 만들지 않고 기존 실패 경로를 그대로
	 * 태운다. WBP_Title이 정리되면 이 함수들과 LoginWithDeveloperTool을 통째로 지운다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Online")
	void LoginWithEpic();

#pragma region Online Test

	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Online Test")
	void LoginWithDeveloperTool(const FString& CredentialName);

	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Online Test")
	void LoginAsPlayer1();

	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Online Test")
	void LoginAsPlayer2();

#pragma endregion

	// 방 코드를 생성하고, 중복 여부를 확인한 뒤 세션을 생성
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Session")
	void CreateRoom(const FString& RoomName, bool bIsPrivate);

	// 현재 검색 가능한 공개 세션 목록을 가져옴
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Session")
	void FindRooms();

	// 목록 검색 대기만 취소. 늦은 OSS 응답은 화면에 반영하지 않는다
	void CancelRoomSearch();

	bool IsRoomSearchInProgress() const;

	// 입력한 방 코드와 일치하는 세션을 찾아 입장
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Session")
	void JoinRoomByCode(const FString& RoomCode);

	// 마지막 세션 검색 결과 중 첫 번째 방에 입장
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Session")
	void JoinFirstFoundRoom();

	// 현재 생성하거나 참가 중인 세션을 제거
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Session")
	void DestroyRoom();

	// UI에 표시할 마지막 온라인 처리 상태 메시지를 반환
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Online")
	FString GetLastStatus() const;

	// 호스트가 생성한 현재 방 코드를 반환
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Session")
	FString GetCurrentRoomCode() const;

	// 현재 방 제목을 반환. 호스트가 안 넣었으면 빈 값
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Session")
	FString GetCurrentRoomName() const;

	// 현재 방 호스트의 표시 이름. 제목이 비었을 때 UI 대체 문구에 씀
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Session")
	FString GetCurrentHostName() const;

	// 로컬 유저 0번이 온라인 서비스에 로그인되어 있는지 확인
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Online")
	bool IsLoggedIn() const;

	// UI에 표시할 현재 로그인 계정 이름을 반환
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Online")
	FString GetLoggedInDisplayName() const;

	// 방 생성, 검색, 참가, 파기 요청이 진행 중인지 확인
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Session")
	bool IsSessionOperationInFlight() const;

	// 마지막으로 수동 검색한 방 목록을 반환
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Session")
	const TArray<FCWRoomSummary>& GetCachedRoomList() const;

	// 방을 나간 뒤 돌아갈 메뉴 레벨 경로를 반환
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Session")
	const FString& GetMenuMapPath() const
	{
		return MenuMapPath;
	}

	// 지금 Room UI 로 착지해도 되는 상태인지 확인
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Session")
	bool ShouldEnterRoomPage() const;

protected:
	// 세션 생성 성공 시 리슨 서버로 이동할 맵 경로
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosswalk|Session")
	FString ListenMapPath;

	// 방을 나간 뒤 돌아갈 메뉴 레벨 경로
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosswalk|Session")
	FString MenuMapPath;

	// 한 세션에 입장 가능한 최대 플레이어 수
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosswalk|Session")
	int32 MaxPublicConnections;

	// 자동 생성할 방 코드의 문자 길이
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosswalk|Session")
	int32 RoomCodeLength;

	// 중복 방 코드가 나왔을 때 다시 생성할 최대 횟수
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosswalk|Session")
	int32 MaxRoomCodeGenerateAttempts;

	// 세션 작업 하나가 끝나기를 기다리는 최대 시간(초). 넘기면 Timeout 실패로 정리한다.
	//
	// 방 생성은 코드 중복 검사 때문에 검색을 여러 번 돌 수 있는데, 이 시간은 재시도를
	// 포함한 작업 전체의 마감이다. 0 이하면 타임아웃을 걸지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosswalk|Session")
	float SessionOperationTimeout;

private:
	// 기본 OnlineSubsystem(Steam) 인스턴스의 Identity/Session 인터페이스를 가져옴
	void CacheOnlineInterfaces();

	/**
	 * 지금 붙어있는 OnlineSubsystem이 Null(로컬 전용)인가.
	 *
	 * Null이면 방 광고/검색이 UDP 브로드캐스트라 같은 기기(멀티 PIE) 안에서만 보이고,
	 * Steam이면 밸브 로비 서버를 거치므로 인터넷 너머까지 보인다. 같은 스팀 계정으로는
	 * 인스턴스 2개를 띄워도 혼자 검색/참가 테스트가 안 되는데(엔진 소스 확인 -
	 * OnLobbyDataUpdate가 요청자와 로비 소유자의 SteamID가 같으면 콜백을 버림), Null은 이
	 * 제약이 없어서 로컬 세션/UI 로직 검증엔 이쪽을 쓴다. CreateSessionWithRoomCode/
	 * StartFindSessions가 이 값으로 bIsLANMatch/bIsLanQuery를 정한다 - ini의
	 * DefaultPlatformService만 Null로 바꾸면 코드 수정 없이 전환된다.
	 */
	bool IsLanOnlySubsystem() const;

	// 로그인 화면에 표시할 상태 메시지를 갱신
	void SetStatus(const FString& NewStatus);

	// 사람이 읽기 쉬운 랜덤 방 코드를 생성
	FString GenerateRoomCode() const;

	// 사용자가 입력한 방 코드의 공백을 제거하고 대문자로 통일
	FString NormalizeRoomCode(const FString& RoomCode) const;

	// 목적에 따라 세션 검색을 시작
	void StartFindSessions(ECWSessionFindPurpose FindPurpose);

	// UI 대기는 끝내되 실제 검색 응답은 받아 버린다
	void DiscardPendingSearch();

	// 확정된 방 코드로 세션을 생성
	void CreateSessionWithRoomCode(const FString& RoomCode);

	// 검색 결과 안에 같은 방 코드가 존재하는지 확인
	bool HasRoomCodeInSearchResults(const FString& RoomCode) const;

	// 검색 결과가 비밀방인지. 목록 표시 경로에서만 사용
	bool IsPrivateRoom(const FOnlineSessionSearchResult& SearchResult) const;

	// 검색 결과 한 개를 UI에서 사용할 방 요약 정보로 변환
	FCWRoomSummary BuildRoomSummary(const FOnlineSessionSearchResult& SearchResult) const;

	// 현재 세션 검색 결과를 UI 표시용 방 목록 캐시에 반영
	void RefreshCachedRoomList();

	/**
	 * 세션 작업을 시작한다. 이미 다른 작업이 진행 중이면 거절한다.
	 *
	 * @return 시작했으면 true. false 면 호출부는 그대로 돌아가야 한다.
	 *         (거절 결과는 이 안에서 이미 UI 에 알렸다)
	 */
	bool BeginSessionOperation(ECWSessionOperation Operation);

	// 진행 중인 작업이 응답 없이 멈춰 있지 않도록 마감 타이머를 건다
	void StartSessionOperationTimeout();

	// 마감 타이머를 해제한다
	void ClearSessionOperationTimeout();

	// UI 작업은 Timeout으로 끝내고, 아직 실행 중인 검색 결과는 버린다
	void HandleSessionOperationTimeout();

	// 세션 작업 완료 상태를 기록하고 UI에 결과를 전달
	void CompleteSessionOperation(ECWSessionOperation Operation, bool bSuccess, ECWSessionFailure Failure);

	// 세션 작업 실패 처리를 한 곳에서 정리
	void FailSessionOperation(ECWSessionOperation Operation, ECWSessionFailure Failure, const FString& StatusMessage);

	// 로그인 에러 문자열을 UI용 로그인 실패 타입으로 변환
	ECWLoginFailure MapLoginFailure(const FString& Error) const;

	// 세션 참가 결과를 UI용 세션 실패 타입으로 변환
	ECWSessionFailure MapJoinSessionFailure(EOnJoinSessionCompleteResult::Type Result) const;

	// 로그인 완료 콜백을 등록
	void BindLoginCompleteDelegate();

	// 로그인 완료 콜백을 해제
	void ClearLoginCompleteDelegate();

	// IsLoggedIn()이 true라 OnLoginComplete()를 안 거치고 바로 반환하는 경로(Steam은 항상 이 경로)를 위해
	// LoggedInDisplayName을 직접 채운다 - 안 하면 로그인 닉네임이 계속 비어있게 된다.
	void RefreshLoggedInDisplayName(int32 LocalUserNum);

	// 세션 생성 완료 콜백을 등록
	void BindCreateSessionCompleteDelegate();

	// 세션 생성 완료 콜백을 해제
	void ClearCreateSessionCompleteDelegate();

	// 세션 검색 완료 콜백을 등록
	void BindFindSessionsCompleteDelegate();

	// 세션 검색 완료 콜백을 해제
	void ClearFindSessionsCompleteDelegate();

	// 세션 참가 완료 콜백을 등록
	void BindJoinSessionCompleteDelegate();

	// 세션 참가 완료 콜백을 해제
	void ClearJoinSessionCompleteDelegate();

	// 세션 제거 완료 콜백을 등록
	void BindDestroySessionCompleteDelegate();

	// 세션 제거 완료 콜백을 해제
	void ClearDestroySessionCompleteDelegate();

	// 온라인 로그인 요청이 끝났을 때 호출
	void OnLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error);

	// 세션 생성 요청이 끝났을 때 호출
	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);

	// 세션 검색 요청이 끝났을 때 호출
	void OnFindSessionsComplete(bool bWasSuccessful);

	// 세션 참가 요청이 끝났을 때 호출
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

	// 세션 제거 요청이 끝났을 때 호출
	void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	// 맵을 떠날 때 BGM 을 끊음
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	// 호스트 연결이 끊겼을 때 참가자의 세션 멤버십을 정리
	void HandleNetworkFailure(
		UWorld* World,
		UNetDriver* NetDriver,
		ENetworkFailure::Type FailureType,
		const FString& ErrorString
	);

	// 세션 생성 후 실패했을 때 만들어진 세션을 되돌리고 CreateRoom 실패로 끝냄
	void RollbackCreatedSession(const FString& Reason);

	// 생성 롤백을 마무리하고 CreateRoom 실패를 알림
	void FinishRollbackAsCreateFailure();

	// 네트워크 실패에서 메뉴로 복구. 사용자용 DestroyRoom 을 타지 않음
	void RecoverToMenuAfterNetworkFailure();

	// 복구를 시작하기 전에 진행 중이던 사용자 세션 작업을 조용히 끊음
	void CancelCurrentSessionOperationForRecovery();

	// 네트워크 실패 복구를 마무리하고 메뉴 이동을 예약
	void FinishNetworkFailureRecovery();

	// 연결이 끊긴 월드에 아직 남아 있으면 메뉴 맵을 새로 엶
	void TravelToMenuMap();

	// 복구 전용 타임아웃을 시작
	void StartNetworkFailureRecoveryTimeout();

	// 복구 전용 타임아웃을 해제
	void ClearNetworkFailureRecoveryTimeout();

	// 복구 응답이 오지 않았을 때 로컬 상태만 정리하고 복구를 끝냄
	void HandleNetworkFailureRecoveryTimeout();

private:
	// UI에 표시할 마지막 온라인 처리 상태 메시지
	UPROPERTY()
	FString LastStatus;

	// 호스트가 생성한 현재 방 코드
	UPROPERTY()
	FString CurrentRoomCode;

	// 현재 방 제목. CurrentRoomCode 와 반드시 같이 갱신·정리할 것
	FString CurrentRoomName;

	// 현재 방 호스트 이름. CurrentRoomName 과 같이 갱신·정리할 것
	FString CurrentHostName;

	// 방 생성 전 중복 검사를 기다리는 임시 방 코드
	UPROPERTY()
	FString PendingRoomCode;

	// 방 코드 입장 시 검색 완료 후 사용할 임시 방 코드
	UPROPERTY()
	FString PendingJoinRoomCode;

	// 세션 생성까지 들고 있을 방 제목
	// CreateRoom 은 세션을 바로 안 만들고 방 코드 중복 확인을 한 번 돈 뒤에 만든다
	UPROPERTY()
	FString PendingRoomName;

	// 세션 생성까지 들고 있을 비밀방 여부
	UPROPERTY()
	bool bPendingIsPrivate = false;

	// UI에 표시할 현재 로그인 계정 이름
	UPROPERTY()
	FString LoggedInDisplayName;

	// 마지막 수동 검색으로 가져온 방 목록 캐시
	UPROPERTY()
	TArray<FCWRoomSummary> CachedRoomList;

	// 현재 온라인 서비스의 로그인 인터페이스
	IOnlineIdentityPtr IdentityInterface;

	// 현재 온라인 서비스의 세션 인터페이스
	IOnlineSessionPtr SessionInterface;

	// 마지막 세션 검색 요청 결과
	TSharedPtr<FOnlineSessionSearch> SessionSearch;

	// OSS 검색은 UI 타임아웃 뒤에도 실행 중일 수 있다
	bool bAwaitingSearchCompletion = false;
	bool bDiscardSearchResult = false;

	// 취소한 검색이 끝나면 실행할 새 요청. 입력값은 기존 Pending 필드에 보관
	ECWSessionFindPurpose DeferredFindPurpose = ECWSessionFindPurpose::None;

	// 현재 세션 검색이 어떤 목적으로 실행 중인지 구분
	ECWSessionFindPurpose CurrentFindPurpose;

	// 현재 진행 중인 세션 작업 종류
	ECWSessionOperation CurrentSessionOperation;

	// 방 코드 중복 발생 시 재생성한 횟수
	int32 RoomCodeGenerateAttemptCount;

	// 세션 작업 중 UI 중복 요청을 막기 위한 상태
	bool bSessionOperationInFlight;

	// 진행 중인 세션 작업의 마감 타이머
	FTimerHandle SessionOperationTimeoutHandle;

	// 로그인 완료 델리게이트 해제를 위한 핸들
	FDelegateHandle LoginCompleteDelegateHandle;

	// 세션 생성 완료 델리게이트 해제를 위한 핸들
	FDelegateHandle CreateSessionCompleteDelegateHandle;

	// 세션 검색 완료 델리게이트 해제를 위한 핸들
	FDelegateHandle FindSessionsCompleteDelegateHandle;

	// 세션 참가 완료 델리게이트 해제를 위한 핸들
	FDelegateHandle JoinSessionCompleteDelegateHandle;

	// 세션 제거 완료 델리게이트 해제를 위한 핸들
	FDelegateHandle DestroySessionCompleteDelegateHandle;

	// 네트워크 실패 델리게이트 해제에 사용하는 핸들
	FDelegateHandle NetworkFailureHandle;

	// 월드 정리 델리게이트 해제에 사용하는 핸들
	FDelegateHandle WorldCleanupHandle;

	// 로그인 완료 콜백 객체
	FOnLoginCompleteDelegate LoginCompleteDelegate;

	// 세션 생성 완료 콜백 객체
	FOnCreateSessionCompleteDelegate CreateSessionCompleteDelegate;

	// 세션 검색 완료 콜백 객체
	FOnFindSessionsCompleteDelegate FindSessionsCompleteDelegate;

	// 세션 참가 완료 콜백 객체
	FOnJoinSessionCompleteDelegate JoinSessionCompleteDelegate;

	// 세션 제거 완료 콜백 객체
	FOnDestroySessionCompleteDelegate DestroySessionCompleteDelegate;

	// 세션 생성 후 실패해서 되돌리는 중인지 구분
	// 사용자용 DestroyRoom 과 구분해야 CreateRoom 의 in-flight 를 유지할 수 있음
	bool bRollingBackFailedCreate = false;

	// 네트워크 실패에서 메뉴로 복구하는 중인지 구분
	bool bRecoveringFromNetworkFailure = false;

	// 연결이 끊긴 월드
	// 통합 후에는 Room UI 도 Title 맵 위에 뜨므로 맵 이름으로는 구분되지 않음
	TWeakObjectPtr<UWorld> NetworkFailureWorld;

	// 종료 중에 예약된 메뉴 이동이 실행되지 않도록 보관하는 핸들
	FTimerHandle TravelToMenuNextTickHandle;

	// 네트워크 실패 복구의 마감 타이머
	// 이 경로는 BeginSessionOperation 을 타지 않아 세션 작업 타이머가 돌지 않음
	FTimerHandle NetworkFailureRecoveryTimeoutHandle;

public:
	// 로그인 상태가 바뀌었을 때 UI에 결과를 전달
	UPROPERTY(BlueprintAssignable, Category = "Crosswalk|Online")
	FCWOnLoginStateChangedSignature OnLoginStateChanged;

	// 방 생성, 검색, 참가, 파기 작업이 끝났을 때 UI에 결과를 전달
	UPROPERTY(BlueprintAssignable, Category = "Crosswalk|Session")
	FCWOnSessionOperationCompletedSignature OnSessionOperationCompleted;

	// 수동 방 목록 검색이 끝났을 때 UI에 최신 방 목록을 전달
	UPROPERTY(BlueprintAssignable, Category = "Crosswalk|Session")
	FCWOnRoomListUpdatedSignature OnRoomListUpdated;

};
