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

public:
	UCWGameInstance();

	// 게임 시작 시 온라인 서브시스템 인터페이스를 캐싱
	virtual void Init() override;

	// 게임 종료 시 등록한 온라인 델리게이트를 정리
	virtual void Shutdown() override;

	// Epic Account Portal을 통해 현재 로컬 유저를 EOS에 로그인
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Online")
	void LoginWithEpic();

	// Steam 클라이언트 인증 정보를 사용해 EOS 로그인을 요청
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Online")
	void LoginWithSteam();

#pragma region Online Test

	// EOS Developer Authentication Tool 테스트 계정으로 로그인
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Online Test")
	void LoginWithDeveloperTool(const FString& CredentialName);

	// Developer Authentication Tool의 Player1 계정으로 로그인
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Online Test")
	void LoginAsPlayer1();

	// Developer Authentication Tool의 Player2 계정으로 로그인
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Online Test")
	void LoginAsPlayer2();

#pragma endregion

	// 방 코드를 생성하고, 중복 여부를 확인한 뒤 세션을 생성
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Session")
	void CreateRoom();

	// 현재 검색 가능한 공개 세션 목록을 가져옴
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Session")
	void FindRooms();

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

protected:
	// 세션 생성 성공 시 리슨 서버로 이동할 맵 경로
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosswalk|Session")
	FString ListenMapPath;

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
	// 현재 DefaultPlatformService의 Identity/Session 인터페이스를 가져옴
	void CacheOnlineInterfaces();

	// 로그인 화면에 표시할 상태 메시지를 갱신
	void SetStatus(const FString& NewStatus);

	// 사람이 읽기 쉬운 랜덤 방 코드를 생성
	FString GenerateRoomCode() const;

	// 사용자가 입력한 방 코드의 공백을 제거하고 대문자로 통일
	FString NormalizeRoomCode(const FString& RoomCode) const;

	// 목적에 따라 EOS 세션 검색을 시작
	void StartFindSessions(ECWSessionFindPurpose FindPurpose);

	// 확정된 방 코드로 EOS 세션을 생성
	void CreateSessionWithRoomCode(const FString& RoomCode);

	// 검색 결과 안에 같은 방 코드가 존재하는지 확인
	bool HasRoomCodeInSearchResults(const FString& RoomCode) const;

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

	// 마감 시간이 지났다. 남은 콜백을 걷어내고 Timeout 실패로 정리한다
	void HandleSessionOperationTimeout();

	// 세션 작업 완료 상태를 기록하고 UI에 결과를 전달
	void CompleteSessionOperation(ECWSessionOperation Operation, bool bSuccess, ECWSessionFailure Failure);

	// 세션 작업 실패 처리를 한 곳에서 정리
	void FailSessionOperation(ECWSessionOperation Operation, ECWSessionFailure Failure, const FString& StatusMessage);

	// EOS 로그인 에러 문자열을 UI용 로그인 실패 타입으로 변환
	ECWLoginFailure MapLoginFailure(const FString& Error) const;

	// 세션 참가 결과를 UI용 세션 실패 타입으로 변환
	ECWSessionFailure MapJoinSessionFailure(EOnJoinSessionCompleteResult::Type Result) const;

	// 로그인 완료 콜백을 등록
	void BindLoginCompleteDelegate();

	// 로그인 완료 콜백을 해제
	void ClearLoginCompleteDelegate();

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

private:
	// UI에 표시할 마지막 온라인 처리 상태 메시지
	UPROPERTY()
	FString LastStatus;

	// 호스트가 생성한 현재 방 코드
	UPROPERTY()
	FString CurrentRoomCode;

	// 방 생성 전 중복 검사를 기다리는 임시 방 코드
	UPROPERTY()
	FString PendingRoomCode;

	// 방 코드 입장 시 검색 완료 후 사용할 임시 방 코드
	UPROPERTY()
	FString PendingJoinRoomCode;

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