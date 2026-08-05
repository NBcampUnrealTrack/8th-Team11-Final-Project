// Copyright Crosswalk Team. All Rights Reserved.
// CWGameInstance.cpp

#include "CWGameInstance.h"

#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"

namespace CrosswalkSession
{
    static const FName RoomCodeKey(TEXT("CW_ROOM_CODE"));
    static const FName GameSessionName(TEXT("GameSession"));
}

UCWGameInstance::UCWGameInstance()
    : ListenMapPath(TEXT("/Game/CrossWalk/Maps/Dev/Server_Test"))
    , MaxPublicConnections(4)
    , RoomCodeLength(6)
    , MaxRoomCodeGenerateAttempts(20)
    , CurrentFindPurpose(ECWSessionFindPurpose::None)
    , RoomCodeGenerateAttemptCount(0)
    , LoginCompleteDelegate(FOnLoginCompleteDelegate::CreateUObject(this, &UCWGameInstance::OnLoginComplete))
    , CreateSessionCompleteDelegate(FOnCreateSessionCompleteDelegate::CreateUObject(this, &UCWGameInstance::OnCreateSessionComplete))
    , FindSessionsCompleteDelegate(FOnFindSessionsCompleteDelegate::CreateUObject(this, &UCWGameInstance::OnFindSessionsComplete))
    , JoinSessionCompleteDelegate(FOnJoinSessionCompleteDelegate::CreateUObject(this, &UCWGameInstance::OnJoinSessionComplete))
    , DestroySessionCompleteDelegate(FOnDestroySessionCompleteDelegate::CreateUObject(this, &UCWGameInstance::OnDestroySessionComplete))
{
}

void UCWGameInstance::Init()
{
    Super::Init();

    CacheOnlineInterfaces();
}

void UCWGameInstance::Shutdown()
{
    ClearLoginCompleteDelegate();
    ClearCreateSessionCompleteDelegate();
    ClearFindSessionsCompleteDelegate();
    ClearJoinSessionCompleteDelegate();
    ClearDestroySessionCompleteDelegate();

    Super::Shutdown();
}

void UCWGameInstance::LoginWithEpic()
{
    CacheOnlineInterfaces();

    if (!IdentityInterface.IsValid())
    {
        SetStatus(TEXT("Epic login failed: IdentityInterface is invalid"));
        return;
    }

    if (IsLoggedIn())
    {
        SetStatus(TEXT("Already logged in"));
        return;
    }

    BindLoginCompleteDelegate();

    FOnlineAccountCredentials Credentials;
    Credentials.Type = TEXT("accountportal");
    Credentials.Id = TEXT("");
    Credentials.Token = TEXT("");

    SetStatus(TEXT("Epic login requested"));
    IdentityInterface->Login(0, Credentials);
}

void UCWGameInstance::LoginWithSteam()
{
    CacheOnlineInterfaces();

    if (!IdentityInterface.IsValid())
    {
        SetStatus(TEXT("Steam login failed: IdentityInterface is invalid"));
        return;
    }

    if (IsLoggedIn())
    {
        SetStatus(TEXT("Already logged in"));
        return;
    }

    BindLoginCompleteDelegate();

    // Steamworks 정식 App ID와 EOS Steam ID Provider가 설정되기 전에는 실패하는 것이 정상
    SetStatus(TEXT("Steam/EOS AutoLogin requested"));
    IdentityInterface->AutoLogin(0);
}

void UCWGameInstance::LoginWithDeveloperTool(const FString& CredentialName)
{
    CacheOnlineInterfaces();

    if (!IdentityInterface.IsValid())
    {
        SetStatus(TEXT("Developer login failed: IdentityInterface is invalid"));
        return;
    }

    if (IsLoggedIn())
    {
        SetStatus(TEXT("Already logged in"));
        return;
    }

    BindLoginCompleteDelegate();

    FOnlineAccountCredentials Credentials;
    Credentials.Type = TEXT("developer");
    Credentials.Id = TEXT("localhost:6300");
    Credentials.Token = CredentialName;

    SetStatus(FString::Printf(TEXT("Developer login requested: %s"), *CredentialName));
    IdentityInterface->Login(0, Credentials);
}

void UCWGameInstance::LoginAsPlayer1()
{
    LoginWithDeveloperTool(TEXT("Player1"));
}

void UCWGameInstance::LoginAsPlayer2()
{
    LoginWithDeveloperTool(TEXT("Player2"));
}

void UCWGameInstance::CreateRoom()
{
    CacheOnlineInterfaces();

    if (!SessionInterface.IsValid())
    {
        SetStatus(TEXT("CreateRoom failed: SessionInterface is invalid"));
        return;
    }

    if (!IsLoggedIn())
    {
        SetStatus(TEXT("CreateRoom failed: login first"));
        return;
    }

    if (SessionInterface->GetNamedSession(CrosswalkSession::GameSessionName) != nullptr)
    {
        SetStatus(TEXT("CreateRoom failed: session already exists"));
        return;
    }

    RoomCodeGenerateAttemptCount = 1;
    PendingRoomCode = GenerateRoomCode();
    StartFindSessions(ECWSessionFindPurpose::CheckRoomCodeForCreate);
}

void UCWGameInstance::FindRooms()
{
    CacheOnlineInterfaces();

    if (!SessionInterface.IsValid())
    {
        SetStatus(TEXT("FindRooms failed: SessionInterface is invalid"));
        return;
    }

    StartFindSessions(ECWSessionFindPurpose::ManualRefresh);
}

void UCWGameInstance::JoinRoomByCode(const FString& RoomCode)
{
    CacheOnlineInterfaces();

    if (!SessionInterface.IsValid())
    {
        SetStatus(TEXT("JoinRoomByCode failed: SessionInterface is invalid"));
        return;
    }

    if (!IsLoggedIn())
    {
        SetStatus(TEXT("JoinRoomByCode failed: login first"));
        return;
    }

    PendingJoinRoomCode = NormalizeRoomCode(RoomCode);

    if (PendingJoinRoomCode.IsEmpty())
    {
        SetStatus(TEXT("JoinRoomByCode failed: room code is empty"));
        return;
    }

    StartFindSessions(ECWSessionFindPurpose::JoinByRoomCode);
}

void UCWGameInstance::JoinFirstFoundRoom()
{
    CacheOnlineInterfaces();

    if (!SessionInterface.IsValid() || !SessionSearch.IsValid())
    {
        SetStatus(TEXT("JoinFirstFoundRoom failed: find rooms first"));
        return;
    }

    if (SessionSearch->SearchResults.Num() <= 0)
    {
        SetStatus(TEXT("JoinFirstFoundRoom failed: no search results"));
        return;
    }

    BindJoinSessionCompleteDelegate();

    SetStatus(TEXT("JoinFirstFoundRoom requested"));
    SessionInterface->JoinSession(0, CrosswalkSession::GameSessionName, SessionSearch->SearchResults[0]);
}

void UCWGameInstance::DestroyRoom()
{
    CacheOnlineInterfaces();

    if (!SessionInterface.IsValid())
    {
        SetStatus(TEXT("DestroyRoom failed: SessionInterface is invalid"));
        return;
    }

    if (SessionInterface->GetNamedSession(CrosswalkSession::GameSessionName) == nullptr)
    {
        SetStatus(TEXT("DestroyRoom failed: no session"));
        return;
    }

    BindDestroySessionCompleteDelegate();

    SetStatus(TEXT("DestroyRoom requested"));
    SessionInterface->DestroySession(CrosswalkSession::GameSessionName);
}

FString UCWGameInstance::GetLastStatus() const
{
    return LastStatus;
}

FString UCWGameInstance::GetCurrentRoomCode() const
{
    return CurrentRoomCode;
}

void UCWGameInstance::CacheOnlineInterfaces()
{
    IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();

    if (OnlineSubsystem == nullptr)
    {
        SetStatus(TEXT("OnlineSubsystem is null"));
        return;
    }

    IdentityInterface = OnlineSubsystem->GetIdentityInterface();
    SessionInterface = OnlineSubsystem->GetSessionInterface();

    SetStatus(FString::Printf(
        TEXT("Cached OnlineSubsystem: %s"),
        *OnlineSubsystem->GetSubsystemName().ToString()
    ));
}

bool UCWGameInstance::IsLoggedIn() const
{
    return IdentityInterface.IsValid() && IdentityInterface->GetLoginStatus(0) == ELoginStatus::LoggedIn;
}

void UCWGameInstance::SetStatus(const FString& NewStatus)
{
    LastStatus = NewStatus;

    UE_LOG(LogTemp, Log, TEXT("[Crosswalk Online] %s"), *LastStatus);

    if (GEngine != nullptr)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, FString::Printf(TEXT("[Online] %s"), *LastStatus));
    }
}

FString UCWGameInstance::GenerateRoomCode() const
{
    static const TCHAR* Characters = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");

    FString Result;
    const int32 CharactersLength = FCString::Strlen(Characters);

    for (int32 Index = 0; Index < RoomCodeLength; ++Index)
    {
        const int32 RandomIndex = FMath::RandRange(0, CharactersLength - 1);
        Result.AppendChar(Characters[RandomIndex]);
    }

    return Result;
}

FString UCWGameInstance::NormalizeRoomCode(const FString& RoomCode) const
{
    FString Result = RoomCode;
    Result.TrimStartAndEndInline();
    Result.ToUpperInline();
    return Result;
}

void UCWGameInstance::StartFindSessions(ECWSessionFindPurpose FindPurpose)
{
    if (!SessionInterface.IsValid())
    {
        SetStatus(TEXT("FindSessions failed: SessionInterface is invalid"));
        return;
    }

    CurrentFindPurpose = FindPurpose;

    SessionSearch = MakeShared<FOnlineSessionSearch>();
    SessionSearch->MaxSearchResults = 100;
    SessionSearch->bIsLanQuery = false;
    SessionSearch->PingBucketSize = 50;

    SessionSearch->QuerySettings.Set(
        SEARCH_LOBBIES,
        true,
        EOnlineComparisonOp::Equals
    );

    BindFindSessionsCompleteDelegate();

    SetStatus(TEXT("FindSessions requested"));
    SessionInterface->FindSessions(0, SessionSearch.ToSharedRef());
}

void UCWGameInstance::CreateSessionWithRoomCode(const FString& RoomCode)
{
    if (!SessionInterface.IsValid())
    {
        SetStatus(TEXT("CreateSession failed: SessionInterface is invalid"));
        return;
    }

    FOnlineSessionSettings SessionSettings;
    SessionSettings.bIsLANMatch = false;
    SessionSettings.NumPublicConnections = MaxPublicConnections;
    SessionSettings.bShouldAdvertise = true;
    SessionSettings.bAllowJoinInProgress = true;
    SessionSettings.bAllowInvites = true;
    SessionSettings.bUsesPresence = true;
    SessionSettings.bAllowJoinViaPresence = true;
    SessionSettings.bUseLobbiesIfAvailable = true;

    SessionSettings.Set(SETTING_MAPNAME, ListenMapPath, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    SessionSettings.Set(CrosswalkSession::RoomCodeKey, RoomCode, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    BindCreateSessionCompleteDelegate();

    CurrentRoomCode = RoomCode;
    SetStatus(FString::Printf(TEXT("CreateSession requested. RoomCode: %s"), *RoomCode));

    SessionInterface->CreateSession(0, CrosswalkSession::GameSessionName, SessionSettings);
}

bool UCWGameInstance::HasRoomCodeInSearchResults(const FString& RoomCode) const
{
    if (!SessionSearch.IsValid())
    {
        return false;
    }

    for (const FOnlineSessionSearchResult& SearchResult : SessionSearch->SearchResults)
    {
        FString FoundRoomCode;

        if (SearchResult.Session.SessionSettings.Get(CrosswalkSession::RoomCodeKey, FoundRoomCode))
        {
            if (FoundRoomCode.Equals(RoomCode, ESearchCase::IgnoreCase))
            {
                return true;
            }
        }
    }

    return false;
}

void UCWGameInstance::BindLoginCompleteDelegate()
{
    ClearLoginCompleteDelegate();

    if (IdentityInterface.IsValid())
    {
        LoginCompleteDelegateHandle = IdentityInterface->AddOnLoginCompleteDelegate_Handle(0, LoginCompleteDelegate);
    }
}

void UCWGameInstance::ClearLoginCompleteDelegate()
{
    if (IdentityInterface.IsValid() && LoginCompleteDelegateHandle.IsValid())
    {
        IdentityInterface->ClearOnLoginCompleteDelegate_Handle(0, LoginCompleteDelegateHandle);
        LoginCompleteDelegateHandle.Reset();
    }
}

void UCWGameInstance::BindCreateSessionCompleteDelegate()
{
    ClearCreateSessionCompleteDelegate();

    if (SessionInterface.IsValid())
    {
        CreateSessionCompleteDelegateHandle = SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegate);
    }
}

void UCWGameInstance::ClearCreateSessionCompleteDelegate()
{
    if (SessionInterface.IsValid() && CreateSessionCompleteDelegateHandle.IsValid())
    {
        SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
        CreateSessionCompleteDelegateHandle.Reset();
    }
}

void UCWGameInstance::BindFindSessionsCompleteDelegate()
{
    ClearFindSessionsCompleteDelegate();

    if (SessionInterface.IsValid())
    {
        FindSessionsCompleteDelegateHandle = SessionInterface->AddOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegate);
    }
}

void UCWGameInstance::ClearFindSessionsCompleteDelegate()
{
    if (SessionInterface.IsValid() && FindSessionsCompleteDelegateHandle.IsValid())
    {
        SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
        FindSessionsCompleteDelegateHandle.Reset();
    }
}

void UCWGameInstance::BindJoinSessionCompleteDelegate()
{
    ClearJoinSessionCompleteDelegate();

    if (SessionInterface.IsValid())
    {
        JoinSessionCompleteDelegateHandle = SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegate);
    }
}

void UCWGameInstance::ClearJoinSessionCompleteDelegate()
{
    if (SessionInterface.IsValid() && JoinSessionCompleteDelegateHandle.IsValid())
    {
        SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
        JoinSessionCompleteDelegateHandle.Reset();
    }
}

void UCWGameInstance::BindDestroySessionCompleteDelegate()
{
    ClearDestroySessionCompleteDelegate();

    if (SessionInterface.IsValid())
    {
        DestroySessionCompleteDelegateHandle = SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegate);
    }
}

void UCWGameInstance::ClearDestroySessionCompleteDelegate()
{
    if (SessionInterface.IsValid() && DestroySessionCompleteDelegateHandle.IsValid())
    {
        SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
        DestroySessionCompleteDelegateHandle.Reset();
    }
}

void UCWGameInstance::OnLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error)
{
    ClearLoginCompleteDelegate();

    if (!bWasSuccessful)
    {
        SetStatus(FString::Printf(TEXT("Login failed: %s"), *Error));
        return;
    }

    FString DisplayName;

    if (IdentityInterface.IsValid())
    {
        DisplayName = IdentityInterface->GetPlayerNickname(LocalUserNum);

        if (DisplayName.IsEmpty())
        {
            TSharedPtr<FUserOnlineAccount> UserAccount = IdentityInterface->GetUserAccount(UserId);

            if (UserAccount.IsValid())
            {
                DisplayName = UserAccount->GetDisplayName();
            }
        }
    }

    if (DisplayName.IsEmpty())
    {
        DisplayName = UserId.ToString();
    }

    SetStatus(FString::Printf(TEXT("Login success: %s"), *DisplayName));
}

void UCWGameInstance::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
    ClearCreateSessionCompleteDelegate();

    if (!bWasSuccessful)
    {
        CurrentRoomCode.Empty();
        SetStatus(TEXT("CreateSession failed"));
        return;
    }

    SetStatus(FString::Printf(TEXT("CreateSession success. RoomCode: %s"), *CurrentRoomCode));

    if (!ListenMapPath.IsEmpty())
    {
        UWorld* World = GetWorld();

        if (World != nullptr)
        {
            World->ServerTravel(ListenMapPath + TEXT("?listen"));
        }
    }
}

void UCWGameInstance::OnFindSessionsComplete(bool bWasSuccessful)
{
    ClearFindSessionsCompleteDelegate();

    if (!bWasSuccessful || !SessionSearch.IsValid())
    {
        SetStatus(TEXT("FindSessions failed"));
        CurrentFindPurpose = ECWSessionFindPurpose::None;
        return;
    }

    if (CurrentFindPurpose == ECWSessionFindPurpose::CheckRoomCodeForCreate)
    {
        if (HasRoomCodeInSearchResults(PendingRoomCode))
        {
            if (RoomCodeGenerateAttemptCount >= MaxRoomCodeGenerateAttempts)
            {
                SetStatus(TEXT("CreateRoom failed: could not generate unique room code"));
                CurrentFindPurpose = ECWSessionFindPurpose::None;
                return;
            }

            ++RoomCodeGenerateAttemptCount;
            PendingRoomCode = GenerateRoomCode();
            StartFindSessions(ECWSessionFindPurpose::CheckRoomCodeForCreate);
            return;
        }

        CreateSessionWithRoomCode(PendingRoomCode);
        CurrentFindPurpose = ECWSessionFindPurpose::None;
        return;
    }

    if (CurrentFindPurpose == ECWSessionFindPurpose::JoinByRoomCode)
    {
        for (const FOnlineSessionSearchResult& SearchResult : SessionSearch->SearchResults)
        {
            FString FoundRoomCode;

            if (SearchResult.Session.SessionSettings.Get(CrosswalkSession::RoomCodeKey, FoundRoomCode))
            {
                if (FoundRoomCode.Equals(PendingJoinRoomCode, ESearchCase::IgnoreCase))
                {
                    BindJoinSessionCompleteDelegate();

                    SetStatus(FString::Printf(TEXT("JoinSession requested. RoomCode: %s"), *PendingJoinRoomCode));
                    SessionInterface->JoinSession(0, CrosswalkSession::GameSessionName, SearchResult);

                    CurrentFindPurpose = ECWSessionFindPurpose::None;
                    return;
                }
            }
        }

        SetStatus(FString::Printf(TEXT("JoinRoomByCode failed: room not found. RoomCode: %s"), *PendingJoinRoomCode));
        CurrentFindPurpose = ECWSessionFindPurpose::None;
        return;
    }

    SetStatus(FString::Printf(TEXT("FindSessions success. Results: %d"), SessionSearch->SearchResults.Num()));
    CurrentFindPurpose = ECWSessionFindPurpose::None;
}

void UCWGameInstance::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
    ClearJoinSessionCompleteDelegate();

    if (!SessionInterface.IsValid())
    {
        SetStatus(TEXT("JoinSession failed: SessionInterface is invalid"));
        return;
    }

    if (Result != EOnJoinSessionCompleteResult::Success)
    {
        SetStatus(TEXT("JoinSession failed"));
        return;
    }

    FString ConnectString;

    if (!SessionInterface->GetResolvedConnectString(SessionName, ConnectString))
    {
        SetStatus(TEXT("JoinSession failed: could not resolve connect string"));
        return;
    }

    APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);

    if (PlayerController == nullptr)
    {
        SetStatus(TEXT("JoinSession failed: PlayerController is null"));
        return;
    }

    SetStatus(FString::Printf(TEXT("JoinSession success. Travel: %s"), *ConnectString));
    PlayerController->ClientTravel(ConnectString, TRAVEL_Absolute);
}

void UCWGameInstance::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
    ClearDestroySessionCompleteDelegate();

    if (!bWasSuccessful)
    {
        SetStatus(TEXT("DestroySession failed"));
        return;
    }

    CurrentRoomCode.Empty();
    PendingRoomCode.Empty();
    PendingJoinRoomCode.Empty();

    SetStatus(TEXT("DestroySession success"));
}