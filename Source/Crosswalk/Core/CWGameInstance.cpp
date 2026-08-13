// Copyright Crosswalk Team. All Rights Reserved.
// CWGameInstance.cpp

#include "CWGameInstance.h"

#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
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
	: ListenMapPath(TEXT("/Game/CrossWalk/Maps/Dev/Prototype"))
	, MaxPublicConnections(4)
	, RoomCodeLength(6)
	, MaxRoomCodeGenerateAttempts(20)
	, SessionOperationTimeout(30.f)
	, CurrentFindPurpose(ECWSessionFindPurpose::None)
	, CurrentSessionOperation(ECWSessionOperation::None)
	, RoomCodeGenerateAttemptCount(0)
	, bSessionOperationInFlight(false)
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

	// 호스트 연결 종료와 네트워크 단절을 한 경로에서 처리
	if (GEngine != nullptr && !NetworkFailureHandle.IsValid())
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(
			this,
			&UCWGameInstance::HandleNetworkFailure
		);
	}
}

void UCWGameInstance::Shutdown()
{
	// GameInstance가 제거되기 전에 전역 엔진 델리게이트 구독 해제
	if (GEngine != nullptr && NetworkFailureHandle.IsValid())
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
	}

	NetworkFailureHandle.Reset();

	ClearSessionOperationTimeout();

	ClearLoginCompleteDelegate();
	ClearCreateSessionCompleteDelegate();
	ClearFindSessionsCompleteDelegate();
	ClearJoinSessionCompleteDelegate();
	ClearDestroySessionCompleteDelegate();

	Super::Shutdown();
}

void UCWGameInstance::HandleNetworkFailure(
	UWorld* World,
	UNetDriver* NetDriver,
	ENetworkFailure::Type FailureType,
	const FString& ErrorString
)
{
	// PIE에서는 모든 GameInstance가 엔진의 전역 델리게이트를 구독한다.
	// 다른 PIE 창에서 발생한 네트워크 실패는 처리하지 않는다.
	if (!IsValid(World) || World->GetGameInstance() != this)
	{
		return;
	}

	// ServerConnection이 있는 인스턴스만 참가자다.
	// 호스트는 자기 버튼 흐름에서 직접 방을 정리한다.
	const bool bIsClient =
		IsValid(NetDriver) && NetDriver->ServerConnection != nullptr;

	if (!bIsClient)
	{
		return;
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("GameInstance: network failure. Type=%d Error=%s"),
		static_cast<int32>(FailureType),
		*ErrorString
	);

	// UE 연결이 끊겨도 EOS 로비 멤버십은 남을 수 있다.
	// 남은 멤버십을 정리해 서버 없는 방이 목록에 노출되는 것을 막는다.
	DestroyRoom();
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

	if (!BeginSessionOperation(ECWSessionOperation::CreateRoom))
	{
		return;
	}

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

	RoomCodeGenerateAttemptCount = 1;
	PendingRoomCode = GenerateRoomCode();
	StartFindSessions(ECWSessionFindPurpose::CheckRoomCodeForCreate);
}

void UCWGameInstance::FindRooms()
{
	CacheOnlineInterfaces();

	if (!BeginSessionOperation(ECWSessionOperation::FindRooms))
	{
		return;
	}

	if (!SessionInterface.IsValid())
	{
		FailSessionOperation(ECWSessionOperation::FindRooms, ECWSessionFailure::Unknown, TEXT("FindRooms failed: SessionInterface is invalid"));
		return;
	}

	StartFindSessions(ECWSessionFindPurpose::ManualRefresh);
}

void UCWGameInstance::JoinRoomByCode(const FString& RoomCode)
{
	CacheOnlineInterfaces();

	if (!BeginSessionOperation(ECWSessionOperation::JoinRoom))
	{
		return;
	}

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

	const FString NormalizedRoomCode = NormalizeRoomCode(RoomCode);

	if (NormalizedRoomCode.IsEmpty())
	{
		FailSessionOperation(ECWSessionOperation::JoinRoom, ECWSessionFailure::RoomNotFound, TEXT("JoinRoomByCode failed: room code is empty"));
		return;
	}

	PendingJoinRoomCode = NormalizedRoomCode;
	StartFindSessions(ECWSessionFindPurpose::JoinByRoomCode);
}

void UCWGameInstance::JoinFirstFoundRoom()
{
	CacheOnlineInterfaces();

	if (!BeginSessionOperation(ECWSessionOperation::JoinRoom))
	{
		return;
	}

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

	BindJoinSessionCompleteDelegate();

	SetStatus(TEXT("JoinFirstFoundRoom requested"));
	SessionInterface->JoinSession(0, CrosswalkSession::GameSessionName, SessionSearch->SearchResults[0]);
}

void UCWGameInstance::DestroyRoom()
{
	CacheOnlineInterfaces();

	if (!BeginSessionOperation(ECWSessionOperation::DestroyRoom))
	{
		return;
	}

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
		// 진행 중이던 작업을 여기서 끝내주지 않으면 in-flight 가 그대로 남는다.
		// 진입점들이 미리 검사하므로 보통은 안 오지만, 방 코드 재시도 경로는 그렇지 않다.
		FailSessionOperation(
			CurrentSessionOperation,
			ECWSessionFailure::Unknown,
			TEXT("FindSessions failed: SessionInterface is invalid"));

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

bool UCWGameInstance::BeginSessionOperation(ECWSessionOperation Operation)
{
	if (bSessionOperationInFlight)
	{
		/**
		 * 거절은 CompleteSessionOperation 을 타면 안 된다.
		 *
		 * 같은 종류의 작업을 두 번 누른 경우(방 목록 새로고침 연타 등) Operation 이
		 * CurrentSessionOperation 과 같아서, 정리 조건에 걸려 진행 중이던 요청의
		 * in-flight 를 대신 꺼버린다. 여기서는 결과만 알리고 상태는 건드리지 않는다.
		 */
		SetStatus(TEXT("Session operation rejected: another operation is in flight"));
		OnSessionOperationCompleted.Broadcast(Operation, false, ECWSessionFailure::Busy);

		return false;
	}

	CurrentSessionOperation = Operation;
	bSessionOperationInFlight = true;

	StartSessionOperationTimeout();

	return true;
}

void UCWGameInstance::CompleteSessionOperation(ECWSessionOperation Operation, bool bSuccess, ECWSessionFailure Failure)
{
	/**
	 * 진행 중이던 작업이 끝났을 때만 상태를 내린다.
	 *
	 * 초기 검증에서 실패한 요청은 BeginSessionOperation 을 지난 적이 없다. 그런데도
	 * 무조건 플래그를 내리면, 다른 작업이 EOS 콜백을 기다리는 중에 엉뚱한 요청 하나가
	 * 조기 실패하는 것만으로 그 작업의 in-flight 가 꺼진다. UI 는 한가한 줄 알고
	 * 다음 요청을 받아버리고, 뒤늦게 도착한 콜백이 그 위에서 다시 돈다.
	 */
	if (CurrentSessionOperation == Operation)
	{
		ClearSessionOperationTimeout();

		bSessionOperationInFlight = false;
		CurrentSessionOperation = ECWSessionOperation::None;
	}

	OnSessionOperationCompleted.Broadcast(Operation, bSuccess, Failure);
}

void UCWGameInstance::StartSessionOperationTimeout()
{
	ClearSessionOperationTimeout();

	if (SessionOperationTimeout <= 0.f)
	{
		return;
	}

	// 월드가 아니라 게임 인스턴스의 타이머 매니저를 쓴다.
	// 방 생성은 성공하면 곧바로 ServerTravel 을 타는데, 월드 타이머는 그때 같이 날아간다.
	GetTimerManager().SetTimer(
		SessionOperationTimeoutHandle,
		this,
		&UCWGameInstance::HandleSessionOperationTimeout,
		SessionOperationTimeout,
		false
	);
}

void UCWGameInstance::ClearSessionOperationTimeout()
{
	GetTimerManager().ClearTimer(SessionOperationTimeoutHandle);
}

void UCWGameInstance::HandleSessionOperationTimeout()
{
	const ECWSessionOperation TimedOutOperation = CurrentSessionOperation;

	if (TimedOutOperation == ECWSessionOperation::None)
	{
		return;
	}

	/**
	 * EOS 콜백이 끝내 오지 않았다. 등록해 둔 델리게이트를 전부 걷어낸다.
	 *
	 * 안 걷어내면 한참 뒤에 도착한 응답이 이미 정리된 상태 위에서 다시 돌면서,
	 * 그 사이에 시작한 다른 작업의 결과를 덮어쓴다.
	 */
	ClearCreateSessionCompleteDelegate();
	ClearFindSessionsCompleteDelegate();
	ClearJoinSessionCompleteDelegate();
	ClearDestroySessionCompleteDelegate();

	CurrentFindPurpose = ECWSessionFindPurpose::None;

	SetStatus(FString::Printf(TEXT("Session operation timed out after %.0fs"), SessionOperationTimeout));
	CompleteSessionOperation(TimedOutOperation, false, ECWSessionFailure::Timeout);
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

	// 참가자는 방 코드를 직접 만들지 않으므로 호스트가 세션에 실어 보낸 값을 읽음
	// 방 목록으로 입장하면 PendingJoinRoomCode가 비어 있어 이 경로가 유일
	FString JoinedRoomCode;

	if (FNamedOnlineSession* NamedSession = SessionInterface->GetNamedSession(SessionName))
	{
		NamedSession->SessionSettings.Get(CrosswalkSession::RoomCodeKey, JoinedRoomCode);
	}

	// 세션 설정에서 못 읽으면 방 코드로 입장한 경우의 값으로 대체
	if (JoinedRoomCode.IsEmpty())
	{
		JoinedRoomCode = PendingJoinRoomCode;
	}

	CurrentRoomCode = JoinedRoomCode;

	SetStatus(FString::Printf(TEXT("JoinSession success. RoomCode: %s Travel: %s"), *CurrentRoomCode, *ConnectString));

	// CurrentRoomCode를 채운 뒤에 알려야 ViewModel이 빈 값을 읽지 않음
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
