// Copyright Crosswalk Team. All Rights Reserved.
// CWGameInstance.cpp

#include "CWGameInstance.h"

#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"

namespace CrosswalkSession
{
	static const FName RoomCodeKey(TEXT("CW_ROOM_CODE"));
	static const FName GameSessionName(TEXT("GameSession"));

	// GameNetDriver 정의 자체(호스트 Listen()/참가자 InitNetDriver()가 하드코딩해서 찾는 이름)의
	// 드라이버 클래스를 트래블 직전에 바꿔치기하기 위한 상수. Config/DefaultEngine.ini의
	// NetDriverDefinitions에 등록된 실제 값과 반드시 일치해야 한다.
	static const FName GameNetDriverDefName(TEXT("GameNetDriver"));
	static const FName EosNetDriverClassName(TEXT("/Script/SocketSubsystemEOS.NetDriverEOSBase"));

	// 평범한 IpNetDriver가 아니라 SocketSubsystemSteamIP 플러그인의 USteamNetDriver를 써야 한다 -
	// 참가자 실기 테스트에서 확인: Steam 세션이 GetResolvedConnectString()으로 돌려주는 주소는
	// 리터럴 IP가 아니라 "steam.<SteamID64>:<port>" 형태의 가짜 호스트명인데
	// (OnlineSessionInterfaceSteam.cpp의 STEAM_URL_PREFIX, ConnectionMethod::P2P 경로 - 우리가
	// 쓰는 로비 세션은 이 경로로 고정됨), 평범한 IpNetDriver는 기본 OS 소켓 서브시스템으로 이 문자열을
	// 그대로 DNS 호스트명 취급해서 리졸브를 시도하다 실패한다(로그: "AddressResolution: Result
	// [Host=steam.76561198413568206] [FAILED] [Error=48]" → "Connection failed; returning to
	// Entry"). USteamNetDriver::GetSocketSubsystem()은 STEAMIP_SUBSYSTEMNAME 소켓 서브시스템을
	// 반환하고, InitConnect()가 Host가 STEAM_URL_PREFIX로 시작하면 Steam 소켓을 직접 만들어서 그
	// 문제를 피해간다(SteamNetDriver.cpp 확인 완료). 호스트 쪽도 Listen()이 이 소켓 서브시스템을
	// 써야 참가자의 Steam P2P 연결을 실제로 받을 수 있어서 호스트/참가자 둘 다 이 클래스를 쓴다.
	// Crosswalk.uproject의 Plugins 목록에 SocketSubsystemSteamIP를 활성화해둬야 이 클래스가 존재한다.
	static const FName SteamNetDriverClassName(TEXT("/Script/SocketSubsystemSteamIP.SteamNetDriver"));

	// Name= 옵션 값에 못 쓰는 문자만 제거한다 (percent-encoding 아님 - 아래 이유로 폐기).
	// FURL은 옵션 값에 한글/공백을 있는 그대로 허용한다(URL.cpp ValidNetChar 주석: "플레이어
	// 별명에 공백이 들어갈 수 있어 일부러 허용"). 그런데 AGameModeBase::CanServerTravel은 인코딩
	// 여부와 무관하게 FURL 문자열에 '%'가 하나라도 있으면 통째로 거부한다 - 실기 테스트로 확인
	// ("CanServerTravel: FURL ... Contains illegal character '%'", 에픽 로그인으로 방 생성 시
	// 재현, 방 생성 직후 다시 메뉴로 튕기는 증상의 실제 원인이었음). 그래서 값을 인코딩하는 대신
	// 옵션 파싱을 깨뜨리는 구분자(?, #, &, =)와 CanServerTravel이 통째로 막는 문자(%, :, \)만
	// 제거한다 - 한글/공백/그 외 문자는 그대로 둔다.
	static FString SanitizeNameOption(const FString& Value)
	{
		FString Result;
		Result.Reserve(Value.Len());

		for (int32 Index = 0; Index < Value.Len(); ++Index)
		{
			const TCHAR Character = Value[Index];

			switch (Character)
			{
			case TEXT('%'):
			case TEXT('?'):
			case TEXT('#'):
			case TEXT('&'):
			case TEXT('='):
			case TEXT(':'):
			case TEXT('\\'):
				continue;
			default:
				Result.AppendChar(Character);
			}
		}

		return Result;
	}
}

UCWGameInstance::UCWGameInstance()
	: ListenMapPath(TEXT("/Game/CrossWalk/Maps/Dev/Prototype"))
	, MaxPublicConnections(4)
	, RoomCodeLength(6)
	, MaxRoomCodeGenerateAttempts(20)
	, SessionOperationTimeout(30.f)
	, CurrentFindPurpose(ECWSessionFindPurpose::None)
	, CurrentSessionOperation(ECWSessionOperation::None)
	, ActivePlatform(ECWOnlinePlatform::EOS)
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

	// net.IpConnectionDisableResolution은 ECVF_Cheat라 프로젝트 ini([ConsoleVariables] 섹션)로는
	// 못 켠다 - 엔진이 로드 시점에 ensure()를 던지며 무시한다(설정 자체가 적용 안 됨), 로컬 개인
	// 파일(Engine/Config/ConsoleVariables.ini, 팀 깃에 안 들어감)에서만 허용되기 때문이다
	// (ConfigUtilities.cpp의 OnSetCVarFromIniEntry 확인). 팀 전체에 적용되려면 ini가 아니라
	// 코드로 직접 설정해야 한다 - USteamNetDriver가 UE5.8의 새 멀티주소 해석 시스템과 안 맞아서
	// (ApplyNetDriverClassForActivePlatform 주석 참고) 꺼야 하는 CVar다.
	if (IConsoleVariable* DisableResolutionCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("net.IpConnectionDisableResolution")))
	{
		DisableResolutionCVar->Set(1, ECVF_SetByCode);
	}

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
	ActivePlatform = ECWOnlinePlatform::EOS;
	CacheOnlineInterfaces();

	if (!IdentityInterface.IsValid())
	{
		SetStatus(TEXT("Epic login failed: IdentityInterface is invalid"));
		OnLoginStateChanged.Broadcast(false, LoggedInDisplayName, ECWLoginFailure::ServiceUnavailable);
		return;
	}

	if (IsLoggedIn())
	{
		RefreshLoggedInDisplayName(0);
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
	// Steam은 EOS를 거치지 않고 독립된 OnlineSubsystem으로 로그인한다 -
	// EOS Steam Identity Provider 연동은 우리 소유 Steamworks App ID 등록이 끝나기 전까지 보류.
	// 그래서 에픽/DevAuth 로그인 유저와는 서로 다른 방 목록을 보게 된다.
	ActivePlatform = ECWOnlinePlatform::Steam;
	CacheOnlineInterfaces();

	if (!IdentityInterface.IsValid())
	{
		SetStatus(TEXT("Steam login failed: IdentityInterface is invalid"));
		OnLoginStateChanged.Broadcast(false, LoggedInDisplayName, ECWLoginFailure::ServiceUnavailable);
		return;
	}

	if (IsLoggedIn())
	{
		// 네이티브 Steam은 로그인 상태 확인이 즉시 끝나서 항상 이 분기로 들어온다 -
		// OnLoginComplete()를 안 거치므로 여기서 직접 닉네임을 채우지 않으면 LoggedInDisplayName이
		// 계속 비어있어 방 입장 시 PlayerState 이름이 "Player"로 고정된다.
		RefreshLoggedInDisplayName(0);
		SetStatus(TEXT("Already logged in"));
		OnLoginStateChanged.Broadcast(true, LoggedInDisplayName, ECWLoginFailure::None);
		return;
	}

	BindLoginCompleteDelegate();

	SetStatus(TEXT("Steam AutoLogin requested"));
	IdentityInterface->AutoLogin(0);
}

void UCWGameInstance::LoginWithDeveloperTool(const FString& CredentialName)
{
	ActivePlatform = ECWOnlinePlatform::EOS;
	CacheOnlineInterfaces();

	if (!IdentityInterface.IsValid())
	{
		SetStatus(TEXT("Developer login failed: IdentityInterface is invalid"));
		OnLoginStateChanged.Broadcast(false, LoggedInDisplayName, ECWLoginFailure::ServiceUnavailable);
		return;
	}

	if (IsLoggedIn())
	{
		RefreshLoggedInDisplayName(0);
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

	// ActivePlatform이 Steam이면 EOS를 거치지 않고 네이티브 Steam 서브시스템을 직접 가져온다.
	// NAME_None을 넘기면 기존과 동일하게 DefaultPlatformService(EOS)를 가져온다.
	const FName SubsystemName = (ActivePlatform == ECWOnlinePlatform::Steam) ? STEAM_SUBSYSTEM : NAME_None;

	// 멀티 PIE에서는 World마다 별도의 OnlineSubsystem 인스턴스를 사용할 수 있음
	IOnlineSubsystem* OnlineSubsystem = IsValid(World)
		? Online::GetSubsystem(World, SubsystemName)
		: IOnlineSubsystem::Get(SubsystemName);

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

void UCWGameInstance::ApplyNetDriverClassForActivePlatform()
{
	if (GEngine == nullptr)
	{
		return;
	}

	FNetDriverDefinition* GameNetDriverDef = GEngine->NetDriverDefinitions.FindByPredicate(
		[](const FNetDriverDefinition& Def)
		{
			return Def.DefName == CrosswalkSession::GameNetDriverDefName;
		});

	if (GameNetDriverDef == nullptr)
	{
		return;
	}

	// 호스트의 UWorld::Listen()과 참가자의 UPendingNetGame::InitNetDriver()는 둘 다 "GameNetDriver"라는
	// 정의 이름을 하드코딩해서 찾는다 - 참가자 쪽은 URL 옵션(NetDriverDef=)으로 바꿀 방법이 아예 없어서
	// (엔진 소스 확인 완료, virtual도 아님) 정의 이름을 유지한 채 그 정의가 가리키는 드라이버 클래스
	// 자체를 트래블 직전마다 바꿔치기하는 방식으로 호스트/참가자 양쪽을 동시에 처리한다.
	// Steam은 EOS P2P 소켓(NetDriverEOSBase)을 못 쓴다 - 로그인된 EOS 유저가 없어서 바인드할 로컬
	// 주소를 못 만들어 "No local user set in LocalBindAddr"로 실패한다.
	GameNetDriverDef->DriverClassName = (ActivePlatform == ECWOnlinePlatform::Steam)
		? CrosswalkSession::SteamNetDriverClassName
		: CrosswalkSession::EosNetDriverClassName;
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

void UCWGameInstance::RefreshLoggedInDisplayName(int32 LocalUserNum)
{
	if (!IdentityInterface.IsValid())
	{
		return;
	}

	FString DisplayName = IdentityInterface->GetPlayerNickname(LocalUserNum);

	if (DisplayName.IsEmpty())
	{
		TSharedPtr<const FUniqueNetId> UserId = IdentityInterface->GetUniquePlayerId(LocalUserNum);

		if (UserId.IsValid())
		{
			TSharedPtr<FUserOnlineAccount> UserAccount = IdentityInterface->GetUserAccount(*UserId);

			if (UserAccount.IsValid())
			{
				DisplayName = UserAccount->GetDisplayName();
			}

			if (DisplayName.IsEmpty())
			{
				DisplayName = UserId->ToString();
			}
		}
	}

	if (!DisplayName.IsEmpty())
	{
		LoggedInDisplayName = DisplayName;
	}
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
			// GameNetDriver 정의가 가리키는 드라이버 클래스를 ActivePlatform에 맞게 바꿔치기한다
			// (CreateNamedNetDriver로 같은 이름을 미리 등록해두는 이전 방식은 Listen()이 내부적으로
			// 호출하는 CreateNamedNetDriver와 이름이 충돌해 NetDriverCreateFailure로 실패했음 - 폐기).
			ApplyNetDriverClassForActivePlatform();

			// URL에 Name= 옵션이 없으면 PlayerState 이름이 GameMode::DefaultPlayerName("Player")로
			// 고정된다 - 플랫폼 상관없이 항상 그랬던 문제라 여기서 로그인 닉네임을 실어 보낸다.
			// 옵션 구분자는 반드시 '?'다 - FURL은 '&'를 옵션 구분자로 인식하지 않는다(엔진 소스
			// URL.cpp의 FURL 파서, GameplayStatics.cpp의 GrabOption 둘 다 확인 완료). '&'로 이으면
			// "listen&Name=Daae"가 통째로 옵션 하나로 묶여서 Name= 옵션 자체가 안 읽힌다 - 실기
			// 로그로 재현/확인함(Server switch level에 Name=이 다른 옵션에 붙어서 나타났음).
			FString TravelURL = ListenMapPath + TEXT("?listen");

			if (!LoggedInDisplayName.IsEmpty())
			{
				TravelURL += TEXT("?Name=") + CrosswalkSession::SanitizeNameOption(LoggedInDisplayName);
			}

			World->ServerTravel(TravelURL);
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

	// 호스트 쪽 ServerTravel 직전과 같은 이유 - UPendingNetGame::InitNetDriver()도 내부적으로
	// "GameNetDriver" 정의를 찾아서 쓰기 때문에(엔진 소스 확인 완료, URL 옵션으로 바꿀 방법 없음),
	// ClientTravel 직전에 그 정의가 가리키는 드라이버 클래스를 여기서도 바꿔치기해야 한다.
	ApplyNetDriverClassForActivePlatform();

	// 호스트 쪽과 같은 이유로 Name= 옵션을 실어 보낸다 - 없으면 서버에서 PlayerState 이름이
	// GameMode::DefaultPlayerName("Player")로 고정된다. FURL은 옵션 구분자로 '&'를 인식하지 않고
	// 옵션마다 '?'로 시작해야 하므로(호스트 쪽과 같은 이유, 위 주석 참고) ConnectString에 이미
	// 옵션이 있든 없든 항상 '?'로 이어붙인다.
	FString FinalConnectString = ConnectString;

	if (!LoggedInDisplayName.IsEmpty())
	{
		FinalConnectString += TEXT("?Name=") + CrosswalkSession::SanitizeNameOption(LoggedInDisplayName);
	}

	PlayerController->ClientTravel(FinalConnectString, TRAVEL_Absolute);
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
