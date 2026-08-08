// Copyright Crosswalk Team. All Rights Reserved.
// CWGameInstance.cpp

#include "CWGameInstance.h"

#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

namespace CrosswalkSession
{
	static const FName RoomCodeKey(TEXT("CW_ROOM_CODE"));
	static const FName GameSessionName(TEXT("GameSession"));
}

UCWGameInstance::UCWGameInstance()
	: ListenMapPath(TEXT("/Game/CrossWalk/Maps/Dev/Test"))
	, MaxPublicConnections(4)
	, RoomCodeLength(6)
	, MaxRoomCodeGenerateAttempts(20)
	, CurrentFindPurpose(ECWSessionFindPurpose::None)
	, CurrentSessionOperation(ECWSessionOperation::None)
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
		OnLoginStateChanged.Broadcast(false, LoggedInDisplayName, ECWLoginFailure::ServiceUnavailable);
		return;
	}

	if (IsLoggedIn())
	{
		SetStatus(TEXT("Already logged in"));
		OnLoginStateChanged.Broadcast(true, LoggedInDisplayName, ECWLoginFailure::None);
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
		OnLoginStateChanged.Broadcast(false, LoggedInDisplayName, ECWLoginFailure::ServiceUnavailable);
		return;
	}

	if (IsLoggedIn())
	{
		SetStatus(TEXT("Already logged in"));
		OnLoginStateChanged.Broadcast(true, LoggedInDisplayName, ECWLoginFailure::None);
		return;
	}

	BindLoginCompleteDelegate();

	SetStatus(TEXT("Steam/EOS AutoLogin requested"));
	IdentityInterface->AutoLogin(0);
}

void UCWGameInstance::LoginWithDeveloperTool(const FString& CredentialName)
{
	CacheOnlineInterfaces();

	if (!IdentityInterface.IsValid())
	{
		SetStatus(TEXT("Developer login failed: IdentityInterface is invalid"));
		OnLoginStateChanged.Broadcast(false, LoggedInDisplayName, ECWLoginFailure::ServiceUnavailable);
		return;
	}

	if (IsLoggedIn())
	{
		SetStatus(TEXT("Already logged in"));
		OnLoginStateChanged.Broadcast(true, LoggedInDisplayName, ECWLoginFailure::None);
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
		FailSessionOperation(ECWSessionOperation::CreateRoom, ECWSessionFailure::Unknown, TEXT("CreateRoom failed: SessionInterface is invalid"));
		return;
	}

	if (!IsLoggedIn())
	{
		FailSessionOperation(ECWSessionOperation::CreateRoom, ECWSessionFailure::NotLoggedIn, TEXT("CreateRoom failed: login first"));
		return;
	}

	if (SessionInterface->GetNamedSession(CrosswalkSession::GameSessionName) != nullptr)
	{
		FailSessionOperation(ECWSessionOperation::CreateRoom, ECWSessionFailure::AlreadyInSession, TEXT("CreateRoom failed: session already exists"));
		return;
	}

	BeginSessionOperation(ECWSessionOperation::CreateRoom);

	RoomCodeGenerateAttemptCount = 1;
	PendingRoomCode = GenerateRoomCode();
	StartFindSessions(ECWSessionFindPurpose::CheckRoomCodeForCreate);
}

void UCWGameInstance::FindRooms()
{
	CacheOnlineInterfaces();

	if (!SessionInterface.IsValid())
	{
		FailSessionOperation(ECWSessionOperation::FindRooms, ECWSessionFailure::Unknown, TEXT("FindRooms failed: SessionInterface is invalid"));
		return;
	}

	BeginSessionOperation(ECWSessionOperation::FindRooms);
	StartFindSessions(ECWSessionFindPurpose::ManualRefresh);
}

void UCWGameInstance::JoinRoomByCode(const FString& RoomCode)
{
	CacheOnlineInterfaces();

	if (!SessionInterface.IsValid())
	{
		FailSessionOperation(ECWSessionOperation::JoinRoom, ECWSessionFailure::Unknown, TEXT("JoinRoomByCode failed: SessionInterface is invalid"));
		return;
	}

	if (!IsLoggedIn())
	{
		FailSessionOperation(ECWSessionOperation::JoinRoom, ECWSessionFailure::NotLoggedIn, TEXT("JoinRoomByCode failed: login first"));
		return;
	}

	PendingJoinRoomCode = NormalizeRoomCode(RoomCode);

	if (PendingJoinRoomCode.IsEmpty())
	{
		FailSessionOperation(ECWSessionOperation::JoinRoom, ECWSessionFailure::RoomNotFound, TEXT("JoinRoomByCode failed: room code is empty"));
		return;
	}

	BeginSessionOperation(ECWSessionOperation::JoinRoom);
	StartFindSessions(ECWSessionFindPurpose::JoinByRoomCode);
}

void UCWGameInstance::JoinFirstFoundRoom()
{
	CacheOnlineInterfaces();

	if (!SessionInterface.IsValid() || !SessionSearch.IsValid())
	{
		FailSessionOperation(ECWSessionOperation::JoinRoom, ECWSessionFailure::RoomNotFound, TEXT("JoinFirstFoundRoom failed: find rooms first"));
		return;
	}

	if (!IsLoggedIn())
	{
		FailSessionOperation(ECWSessionOperation::JoinRoom, ECWSessionFailure::NotLoggedIn, TEXT("JoinFirstFoundRoom failed: login first"));
		return;
	}

	if (SessionSearch->SearchResults.Num() <= 0)
	{
		FailSessionOperation(ECWSessionOperation::JoinRoom, ECWSessionFailure::RoomNotFound, TEXT("JoinFirstFoundRoom failed: no search results"));
		return;
	}

	BeginSessionOperation(ECWSessionOperation::JoinRoom);
	BindJoinSessionCompleteDelegate();

	SetStatus(TEXT("JoinFirstFoundRoom requested"));
	SessionInterface->JoinSession(0, CrosswalkSession::GameSessionName, SessionSearch->SearchResults[0]);
}

void UCWGameInstance::DestroyRoom()
{
	CacheOnlineInterfaces();

	if (!SessionInterface.IsValid())
	{
		FailSessionOperation(ECWSessionOperation::DestroyRoom, ECWSessionFailure::Unknown, TEXT("DestroyRoom failed: SessionInterface is invalid"));
		return;
	}

	if (SessionInterface->GetNamedSession(CrosswalkSession::GameSessionName) == nullptr)
	{
		FailSessionOperation(ECWSessionOperation::DestroyRoom, ECWSessionFailure::Unknown, TEXT("DestroyRoom failed: no session"));
		return;
	}

	BeginSessionOperation(ECWSessionOperation::DestroyRoom);
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

bool UCWGameInstance::IsLoggedIn() const
{
	return IdentityInterface.IsValid() && IdentityInterface->GetLoginStatus(0) == ELoginStatus::LoggedIn;
}

FString UCWGameInstance::GetLoggedInDisplayName() const
{
	return LoggedInDisplayName;
}

bool UCWGameInstance::IsSessionOperationInFlight() const
{
	return bSessionOperationInFlight;
}

const TArray<FCWRoomSummary>& UCWGameInstance::GetCachedRoomList() const
{
	return CachedRoomList;
}

void UCWGameInstance::CacheOnlineInterfaces()
{
	UWorld* World = GetWorld();

	// 멀티 PIE에서는 World마다 별도의 OnlineSubsystem 인스턴스를 사용할 수 있음
	IOnlineSubsystem* OnlineSubsystem = IsValid(World)
		? Online::GetSubsystem(World)
		: IOnlineSubsystem::Get();

	if (OnlineSubsystem == nullptr)
	{
		SetStatus(TEXT("OnlineSubsystem is null"));
		return;
	}

	// 로그인과 세션 작업이 같은 World별 OnlineSubsystem 인스턴스를 사용하도록 갱신
	IdentityInterface = OnlineSubsystem->GetIdentityInterface();
	SessionInterface = OnlineSubsystem->GetSessionInterface();

	SetStatus(FString::Printf(
		TEXT("Cached OnlineSubsystem: %s (Instance: %s)"),
		*OnlineSubsystem->GetSubsystemName().ToString(),
		*OnlineSubsystem->GetInstanceName().ToString()
	));
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
		CompleteSessionOperation(ECWSessionOperation::CreateRoom, false, ECWSessionFailure::Unknown);
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

FCWRoomSummary UCWGameInstance::BuildRoomSummary(const FOnlineSessionSearchResult& SearchResult) const
{
	FCWRoomSummary Summary;

	SearchResult.Session.SessionSettings.Get(CrosswalkSession::RoomCodeKey, Summary.RoomCode);

	Summary.HostName = SearchResult.Session.OwningUserName;
	Summary.MaxPlayers = SearchResult.Session.SessionSettings.NumPublicConnections;
	Summary.CurrentPlayers = FMath::Max(0, Summary.MaxPlayers - SearchResult.Session.NumOpenPublicConnections);

	return Summary;
}

void UCWGameInstance::RefreshCachedRoomList()
{
	CachedRoomList.Reset();

	if (!SessionSearch.IsValid())
	{
		return;
	}

	for (const FOnlineSessionSearchResult& SearchResult : SessionSearch->SearchResults)
	{
		CachedRoomList.Add(BuildRoomSummary(SearchResult));
	}
}

void UCWGameInstance::BeginSessionOperation(ECWSessionOperation Operation)
{
	CurrentSessionOperation = Operation;
	bSessionOperationInFlight = true;
}

void UCWGameInstance::CompleteSessionOperation(ECWSessionOperation Operation, bool bSuccess, ECWSessionFailure Failure)
{
	bSessionOperationInFlight = false;
	CurrentSessionOperation = ECWSessionOperation::None;

	OnSessionOperationCompleted.Broadcast(Operation, bSuccess, Failure);
}

void UCWGameInstance::FailSessionOperation(ECWSessionOperation Operation, ECWSessionFailure Failure, const FString& StatusMessage)
{
	SetStatus(StatusMessage);
	CompleteSessionOperation(Operation, false, Failure);
}

ECWLoginFailure UCWGameInstance::MapLoginFailure(const FString& Error) const
{
	if (Error.Contains(TEXT("cancel"), ESearchCase::IgnoreCase))
	{
		return ECWLoginFailure::Cancelled;
	}

	if (Error.Contains(TEXT("network"), ESearchCase::IgnoreCase) || Error.Contains(TEXT("connection"), ESearchCase::IgnoreCase))
	{
		return ECWLoginFailure::NoConnection;
	}

	if (Error.Contains(TEXT("unavailable"), ESearchCase::IgnoreCase) || Error.Contains(TEXT("timeout"), ESearchCase::IgnoreCase))
	{
		return ECWLoginFailure::ServiceUnavailable;
	}

	if (Error.Contains(TEXT("not configured"), ESearchCase::IgnoreCase) || Error.Contains(TEXT("invalidrequest"), ESearchCase::IgnoreCase))
	{
		return ECWLoginFailure::NotConfigured;
	}

	return ECWLoginFailure::Unknown;
}

ECWSessionFailure UCWGameInstance::MapJoinSessionFailure(EOnJoinSessionCompleteResult::Type Result) const
{
	switch (Result)
	{
	case EOnJoinSessionCompleteResult::SessionIsFull:
		return ECWSessionFailure::RoomFull;

	case EOnJoinSessionCompleteResult::SessionDoesNotExist:
		return ECWSessionFailure::RoomNotFound;

	case EOnJoinSessionCompleteResult::AlreadyInSession:
		return ECWSessionFailure::AlreadyInSession;

	case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:
	case EOnJoinSessionCompleteResult::UnknownError:
	default:
		return ECWSessionFailure::Unknown;
	}
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
		LoggedInDisplayName.Empty();

		const ECWLoginFailure Failure = MapLoginFailure(Error);
		SetStatus(FString::Printf(TEXT("Login failed: %s"), *Error));
		OnLoginStateChanged.Broadcast(false, LoggedInDisplayName, Failure);
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

	LoggedInDisplayName = DisplayName;

	SetStatus(FString::Printf(TEXT("Login success: %s"), *LoggedInDisplayName));
	OnLoginStateChanged.Broadcast(true, LoggedInDisplayName, ECWLoginFailure::None);
}

void UCWGameInstance::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	ClearCreateSessionCompleteDelegate();

	if (!bWasSuccessful)
	{
		CurrentRoomCode.Empty();
		SetStatus(TEXT("CreateSession failed"));
		CompleteSessionOperation(ECWSessionOperation::CreateRoom, false, ECWSessionFailure::Unknown);
		return;
	}

	SetStatus(FString::Printf(TEXT("CreateSession success. RoomCode: %s"), *CurrentRoomCode));
	CompleteSessionOperation(ECWSessionOperation::CreateRoom, true, ECWSessionFailure::None);

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

	const ECWSessionFindPurpose FinishedPurpose = CurrentFindPurpose;

	if (!bWasSuccessful || !SessionSearch.IsValid())
	{
		SetStatus(TEXT("FindSessions failed"));
		CurrentFindPurpose = ECWSessionFindPurpose::None;

		if (FinishedPurpose == ECWSessionFindPurpose::ManualRefresh)
		{
			CachedRoomList.Reset();
			OnRoomListUpdated.Broadcast(CachedRoomList);
			CompleteSessionOperation(ECWSessionOperation::FindRooms, false, ECWSessionFailure::Unknown);
		}
		else if (FinishedPurpose == ECWSessionFindPurpose::CheckRoomCodeForCreate)
		{
			CompleteSessionOperation(ECWSessionOperation::CreateRoom, false, ECWSessionFailure::Unknown);
		}
		else if (FinishedPurpose == ECWSessionFindPurpose::JoinByRoomCode)
		{
			CompleteSessionOperation(ECWSessionOperation::JoinRoom, false, ECWSessionFailure::Unknown);
		}

		return;
	}

	if (FinishedPurpose == ECWSessionFindPurpose::CheckRoomCodeForCreate)
	{
		if (HasRoomCodeInSearchResults(PendingRoomCode))
		{
			if (RoomCodeGenerateAttemptCount >= MaxRoomCodeGenerateAttempts)
			{
				SetStatus(TEXT("CreateRoom failed: could not generate unique room code"));
				CurrentFindPurpose = ECWSessionFindPurpose::None;
				CompleteSessionOperation(ECWSessionOperation::CreateRoom, false, ECWSessionFailure::RoomCodeCollision);
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

	if (FinishedPurpose == ECWSessionFindPurpose::JoinByRoomCode)
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
		CompleteSessionOperation(ECWSessionOperation::JoinRoom, false, ECWSessionFailure::RoomNotFound);
		return;
	}

	if (FinishedPurpose == ECWSessionFindPurpose::ManualRefresh)
	{
		RefreshCachedRoomList();
		OnRoomListUpdated.Broadcast(CachedRoomList);
		CompleteSessionOperation(ECWSessionOperation::FindRooms, true, ECWSessionFailure::None);
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
		CompleteSessionOperation(ECWSessionOperation::JoinRoom, false, ECWSessionFailure::Unknown);
		return;
	}

	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		SetStatus(TEXT("JoinSession failed"));
		CompleteSessionOperation(ECWSessionOperation::JoinRoom, false, MapJoinSessionFailure(Result));
		return;
	}

	FString ConnectString;

	if (!SessionInterface->GetResolvedConnectString(SessionName, ConnectString))
	{
		SetStatus(TEXT("JoinSession failed: could not resolve connect string"));
		CompleteSessionOperation(ECWSessionOperation::JoinRoom, false, ECWSessionFailure::Unknown);
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);

	if (PlayerController == nullptr)
	{
		SetStatus(TEXT("JoinSession failed: PlayerController is null"));
		CompleteSessionOperation(ECWSessionOperation::JoinRoom, false, ECWSessionFailure::Unknown);
		return;
	}

	SetStatus(FString::Printf(TEXT("JoinSession success. Travel: %s"), *ConnectString));
	CompleteSessionOperation(ECWSessionOperation::JoinRoom, true, ECWSessionFailure::None);

	PlayerController->ClientTravel(ConnectString, TRAVEL_Absolute);
}

void UCWGameInstance::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	ClearDestroySessionCompleteDelegate();

	if (!bWasSuccessful)
	{
		SetStatus(TEXT("DestroySession failed"));
		CompleteSessionOperation(ECWSessionOperation::DestroyRoom, false, ECWSessionFailure::Unknown);
		return;
	}

	CurrentRoomCode.Empty();
	PendingRoomCode.Empty();
	PendingJoinRoomCode.Empty();
	CachedRoomList.Reset();

	SetStatus(TEXT("DestroySession success"));
	OnRoomListUpdated.Broadcast(CachedRoomList);
	CompleteSessionOperation(ECWSessionOperation::DestroyRoom, true, ECWSessionFailure::None);
}