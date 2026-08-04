// Copyright Crosswalk Team. All Rights Reserved.
// CWGameInstance.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "CWGameInstance.generated.h"

enum class ECWSessionFindPurpose : uint8
{
    None,
    CheckRoomCodeForCreate,
    JoinByRoomCode,
    ManualRefresh
};

UCLASS()
class CROSSWALK_API UCWGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    UCWGameInstance();

    // 게임 시작 시 온라인 서브시스템 인터페이스를 캐싱
    virtual void Init() override;

    // 게임 종료 시 등록된 온라인 델리게이트를 정리
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

    // 새 방 코드를 생성하고, 중복 여부를 확인한 뒤 세션을 생성
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

protected:
    // 세션 생성 성공 후 리슨 서버로 이동할 맵 경로
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosswalk|Session")
    FString ListenMapPath;

    // 한 세션에 입장 가능한 최대 플레이어 수
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosswalk|Session")
    int32 MaxPublicConnections;

    // 자동 생성할 방 코드의 문자열 길이
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosswalk|Session")
    int32 RoomCodeLength;

    // 중복 방 코드가 나왔을 때 다시 생성할 최대 횟수
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosswalk|Session")
    int32 MaxRoomCodeGenerateAttempts;

private:
    // 현재 DefaultPlatformService의 Identity/Session 인터페이스를 가져옴
    void CacheOnlineInterfaces();

    // 로컬 유저 0번이 온라인 서비스에 로그인되어 있는지 확인
    bool IsLoggedIn() const;

    // 로그와 화면에 표시할 상태 메시지를 갱신
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

    // 온라인 로그인 요청이 끝났을 때 호출.
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
    UPROPERTY()
    FString LastStatus;

    UPROPERTY()
    FString CurrentRoomCode;

    UPROPERTY()
    FString PendingRoomCode;

    UPROPERTY()
    FString PendingJoinRoomCode;

    IOnlineIdentityPtr IdentityInterface;
    IOnlineSessionPtr SessionInterface;
    TSharedPtr<FOnlineSessionSearch> SessionSearch;

    ECWSessionFindPurpose CurrentFindPurpose;
    int32 RoomCodeGenerateAttemptCount;

    FDelegateHandle LoginCompleteDelegateHandle;
    FDelegateHandle CreateSessionCompleteDelegateHandle;
    FDelegateHandle FindSessionsCompleteDelegateHandle;
    FDelegateHandle JoinSessionCompleteDelegateHandle;
    FDelegateHandle DestroySessionCompleteDelegateHandle;

    FOnLoginCompleteDelegate LoginCompleteDelegate;
    FOnCreateSessionCompleteDelegate CreateSessionCompleteDelegate;
    FOnFindSessionsCompleteDelegate FindSessionsCompleteDelegate;
    FOnJoinSessionCompleteDelegate JoinSessionCompleteDelegate;
    FOnDestroySessionCompleteDelegate DestroySessionCompleteDelegate;
};