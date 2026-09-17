// Copyright Crosswalk Team. All Rights Reserved.
// CWGameInstance.cpp

#include "CWGameInstance.h"

#include "Core/CWSessionLog.h"
#include "CWMusicSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"

namespace CrosswalkSession
{
	// 목록 조회는 짧게 기다리고, 생성·참가·복구 제한 시간은 별도로 유지
	static constexpr float RoomSearchTimeout = 5.f;

	static const FName RoomCodeKey(TEXT("CW_ROOM_CODE"));
	static const FName GameSessionName(TEXT("GameSession"));

	/**
	 * 우리 게임의 방임을 식별하는 표식.
	 *
	 * 개발용 AppID 480(Spacewar)은 전 세계 개발자가 공유해서, 스팀 로비 검색에는 전혀 다른
	 * 게임의 방들이 같이 잡힌다(실측: 한 번 검색에 11~12개, 대부분 남의 팀 로비 - "code
	 * P8VJJU", "name OHO's Lobby" 같은 값이 섞여 나옴). 엔진이 파싱 못 하는 키를 가진 방은
	 * 알아서 걸러지지만 그것만으론 부족해서, 이 표식이 있는 방만 목록/방 코드 매칭에 남긴다.
	 */
	static const FName GameIdKey(TEXT("CW_GAME_ID"));
	static const FString GameIdValue(TEXT("CROSSWALK"));

	// 방 목록에 표시할 제목. 비어 있으면 UI가 호스트 이름으로 대체함
	static const FName RoomNameKey(TEXT("CW_ROOM_NAME"));

	// 비밀방 여부. 목록 표시에서만 걸러내고 방 코드 입장은 그대로 허용함.
	// bool 대신 int32로 둔다 - 세션 플래그가 로비 데이터 왕복에서 유실된 전례가 있어(#182)
	// 커스텀 설정도 명시적인 값으로 주고받는다
	static const FName IsPrivateKey(TEXT("CW_IS_PRIVATE"));

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
	// 꺼야 하는 CVar다.
	if (IConsoleVariable* DisableResolutionCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("net.IpConnectionDisableResolution")))
	{
		DisableResolutionCVar->Set(1, ECVF_SetByCode);
	}

	// t.MaxFPS는 프로젝트 ini([ConsoleVariables] 섹션)에 넣어놔도 패키징 빌드에 반영이 안 되는
	// 문제가 있어(원인 미확인, 2026-09-14) 팀원 전체 빌드에 확실히 적용되도록 코드에서 직접 ini
	// 값을 읽어 강제로 설정한다. 60/144/무제한 A/B 테스트 값은 여전히 ini의 t.MaxFPS 줄만 바꾸면
	// 되고, 이 코드는 그 값이 실제로 반영되게 만드는 역할만 한다.
	if (IConsoleVariable* MaxFPSCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
	{
		float ConfiguredMaxFPS = MaxFPSCVar->GetFloat();
		GConfig->GetFloat(TEXT("ConsoleVariables"), TEXT("t.MaxFPS"), ConfiguredMaxFPS, GEngineIni);
		MaxFPSCVar->Set(ConfiguredMaxFPS, ECVF_SetByCode);
	}

	CacheOnlineInterfaces();

	// 맵을 떠날 때 BGM 정지. bPersistAcrossLevelTransition 이라 안 끄면 다음 맵까지 따라옴
	// 레벨 BP 마다 끄는 방식은 하나만 빠져도 무너짐 - Stage_03 에서 실제로 빠졌음
	// PreLoadMap 은 seamless travel 에서 안 옴. PostLoadMapWithWorld 는 BeginPlay 뒤라 새 곡을 꺼버림
	// CleanupWorld 만 두 경로 다 목적지 BeginPlay 보다 앞섬
	if (!WorldCleanupHandle.IsValid())
	{
		WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(
			this,
			&UCWGameInstance::HandleWorldCleanup
		);
	}

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

	if (WorldCleanupHandle.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	}

	WorldCleanupHandle.Reset();

	ClearSessionOperationTimeout();
	ClearNetworkFailureRecoveryTimeout();

	// 종료 중에 예약된 OpenLevel 이 튀어나오지 않게 취소함
	GetTimerManager().ClearTimer(TravelToMenuNextTickHandle);

	NetworkFailureWorld.Reset();
	bRecoveringFromNetworkFailure = false;
	bRollingBackFailedCreate = false;

	ClearLoginCompleteDelegate();
	ClearCreateSessionCompleteDelegate();
	ClearFindSessionsCompleteDelegate();
	ClearJoinSessionCompleteDelegate();
	ClearDestroySessionCompleteDelegate();
	bAwaitingSearchCompletion = false;
	bDiscardSearchResult = false;

	Super::Shutdown();
}

void UCWGameInstance::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	// 전역 델리게이트라 다른 PIE 창 월드도 들어옴. 내 인스턴스 것만 처리함
	if (!IsValid(World) || World->GetGameInstance() != this)
	{
		return;
	}

	// 도착한 맵이 필요하면 BeginPlay 에서 자기 곡을 다시 켬
	if (UCWMusicSubsystem* MusicSubsystem = GetSubsystem<UCWMusicSubsystem>())
	{
		MusicSubsystem->StopMusic(0.0f);
	}
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
		LogCWSession,
		Warning,
		TEXT("GameInstance: network failure. Type=%d Error=%s"),
		static_cast<int32>(FailureType),
		*ErrorString
	);

	// UE 연결이 끊겨도 세션 멤버십은 남을 수 있다.
	// 남은 멤버십을 정리하고 메뉴까지 돌려보낸다.
	// 사용자용 DestroyRoom 은 조기 반환 경로가 있어 완료 델리게이트가 오지 않을 수 있으므로 쓰지 않는다.
	RecoverToMenuAfterNetworkFailure();
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

void UCWGameInstance::CreateRoom(const FString& RoomName, bool bIsPrivate)
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

	// 세션은 방 코드 중복 확인을 한 번 돈 뒤 CreateSessionWithRoomCode 가 만든다.
	// 그때까지 입력값을 들고 있어야 한다
	PendingRoomName = RoomName;
	bPendingIsPrivate = bIsPrivate;

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

bool UCWGameInstance::IsRoomSearchInProgress() const
{
	return bSessionOperationInFlight && CurrentSessionOperation == ECWSessionOperation::FindRooms;
}

void UCWGameInstance::CancelRoomSearch()
{
	if (!IsRoomSearchInProgress())
	{
		return;
	}

	ClearSessionOperationTimeout();
	DiscardPendingSearch();
	CurrentSessionOperation = ECWSessionOperation::None;
	bSessionOperationInFlight = false;
	OnSessionBusyChanged.Broadcast(IsSessionOperationInFlight());
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

	// 비밀방은 목록에 안 뜨므로 "첫 방 참가" 대상에서도 뺀다
	int32 TargetIndex = INDEX_NONE;

	for (int32 Index = 0; Index < SessionSearch->SearchResults.Num(); ++Index)
	{
		if (!IsPrivateRoom(SessionSearch->SearchResults[Index]))
		{
			TargetIndex = Index;
			break;
		}
	}

	if (TargetIndex == INDEX_NONE)
	{
		FailSessionOperation(ECWSessionOperation::JoinRoom, ECWSessionFailure::RoomNotFound, TEXT("JoinFirstFoundRoom failed: no public room"));
		return;
	}

	BindJoinSessionCompleteDelegate();

	// [Steam 엔진 버그 우회] FOnlineSessionSteam::JoinSession은 검색 결과의 bUsesPresence와
	// bUseLobbiesIfAvailable이 다르면 곧바로 UnknownError로 실패시킨다. 그런데 로비 데이터로
	// 실제 전송되는 세션 플래그 비트필드에는 bUseLobbiesIfAvailable이 빠져 있어서, 검색으로
	// 복원된 결과는 항상 bUsesPresence=true / bUseLobbiesIfAvailable=false가 되어 "방은
	// 목록에 보이는데 입장은 항상 실패"한다. 호스트는 CreateSessionWithRoomCode에서 두 값을
	// 같게 광고하므로, 여기서도 같게 맞춰준다.
	FOnlineSessionSearchResult& DesiredSession = SessionSearch->SearchResults[TargetIndex];
	DesiredSession.Session.SessionSettings.bUseLobbiesIfAvailable =
		DesiredSession.Session.SessionSettings.bUsesPresence;

	SetStatus(TEXT("JoinFirstFoundRoom requested"));
	SessionInterface->JoinSession(0, CrosswalkSession::GameSessionName, DesiredSession);
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

	// 즉시 false 면 완료 델리게이트가 오지 않아 타임아웃까지 Busy 로 잠긴다
	if (!SessionInterface->DestroySession(CrosswalkSession::GameSessionName))
	{
		ClearDestroySessionCompleteDelegate();

		FailSessionOperation(
			ECWSessionOperation::DestroyRoom,
			ECWSessionFailure::Unknown,
			TEXT("DestroyRoom failed to start")
		);
	}
}

FString UCWGameInstance::GetLastStatus() const
{
	return LastStatus;
}

FString UCWGameInstance::GetCurrentRoomCode() const
{
	return CurrentRoomCode;
}

FString UCWGameInstance::GetCurrentRoomName() const
{
	return CurrentRoomName;
}

FString UCWGameInstance::GetCurrentHostName() const
{
	return CurrentHostName;
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
	return bSessionOperationInFlight || bRecoveringFromNetworkFailure;
}

const TArray<FCWRoomSummary>& UCWGameInstance::GetCachedRoomList() const
{
	return CachedRoomList;
}

bool UCWGameInstance::ShouldEnterRoomPage() const
{
	const UWorld* World = GetWorld();

	if (!IsValid(World))
	{
		return false;
	}

	// 세션이 있느냐가 아니라 실제로 연결돼 있느냐를 본다.
	// 정리에 실패해 OSS에만 남은 세션은 넷드라이버를 만들지 않으므로
	// 새 월드가 NM_Standalone이 되어 여기서 자동으로 걸러진다.
	// 넷 모드만으로는 부족하다 - 멀티 PIE를 Listen Server로 띄우면
	// 세션을 만들기 전에도 NM_ListenServer라 오판한다.
	const ENetMode NetMode = World->GetNetMode();

	return (NetMode == NM_Client || NetMode == NM_ListenServer)
		&& SessionInterface.IsValid()
		&& SessionInterface->GetNamedSession(CrosswalkSession::GameSessionName) != nullptr;
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
		IdentityInterface.Reset();
		SessionInterface.Reset();
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

bool UCWGameInstance::IsLanOnlySubsystem() const
{
	const UWorld* World = GetWorld();
	const IOnlineSubsystem* OnlineSubsystem = IsValid(World)
		? Online::GetSubsystem(World)
		: IOnlineSubsystem::Get();

	if (OnlineSubsystem == nullptr)
	{
		// 서브시스템조차 못 잡은 상황. LAN으로 취급하는 게 안전하다
		// (로비를 못 여는 것보다, 최소한 같은 망/기기에서라도 붙는 편이 낫다)
		return true;
	}

	return OnlineSubsystem->GetSubsystemName() == NULL_SUBSYSTEM;
}

void UCWGameInstance::SetStatus(const FString& NewStatus)
{
	LastStatus = NewStatus;
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

	// Steam의 이전 검색은 완료 방송까지 살아 있다. 새 요청을 겹쳐 보내지 않는다.
	if (bAwaitingSearchCompletion)
	{
		DeferredFindPurpose = FindPurpose;
		SetStatus(TEXT("FindSessions queued: waiting for the cancelled search to finish"));
		return;
	}

	DeferredFindPurpose = ECWSessionFindPurpose::None;
	CurrentFindPurpose = FindPurpose;

	SessionSearch = MakeShared<FOnlineSessionSearch>();
	SessionSearch->MaxSearchResults = 20;
	SessionSearch->bIsLanQuery = IsLanOnlySubsystem();   // 광고 방식(CreateSessionWithRoomCode)과 반드시 짝이 맞아야 한다
	SessionSearch->PingBucketSize = 50;

	SessionSearch->QuerySettings.Set(
		SEARCH_LOBBIES,
		true,
		EOnlineComparisonOp::Equals
	);

	/**
	 * [480(Spacewar) 공용 풀 대응, 핵심 수정] QuerySettings에 넣은 키는 클라에서 결과를 받은
	 * 뒤 걸러내는 게 아니라, FOnlineAsyncTaskSteamFindLobbiesBase::CreateQuery()가
	 * SteamMatchmakingPtr->AddRequestLobbyListStringFilter()로 그대로 넘겨서 Steam 서버
	 * 쪽에서 미리 걸러진 채로 응답한다(OnlineSessionAsyncLobbySteam.cpp 확인).
	 *
	 * 이게 필요한 이유: 480은 전 세계 개발자가 공유하는 테스트 AppID라 검색에 남의 팀 로비가
	 * 같이 잡힌다. 그런데 FOnlineAsyncTaskSteamFindLobbiesBase는 잡힌 로비 "전부"의 상세
	 * 데이터(RequestLobbyData)가 와야 성공 처리하고, 그중 하나라도(죽은 로비 등) 응답이 안
	 * 오면 타임아웃을 아무리 늘려도(OnlineAsyncTaskSteamFindLobbiesBaseTimeout) 영원히
	 * 실패한다 - 실측으로 확인(30초로 늘려도 그대로 실패). 검색 시점에 우리 게임 로비만
	 * 요청하면 애초에 죽은 로비를 기다릴 일이 없어진다.
	 */
	SessionSearch->QuerySettings.Set(
		CrosswalkSession::GameIdKey,
		CrosswalkSession::GameIdValue,
		EOnlineComparisonOp::Equals
	);

	BindFindSessionsCompleteDelegate();
	const TSharedRef<FOnlineSessionSearch> RequestedSearch = SessionSearch.ToSharedRef();
	bAwaitingSearchCompletion = true;
	bDiscardSearchResult = false;

	SetStatus(TEXT("FindSessions requested"));
	const bool bAccepted = SessionInterface->FindSessions(0, RequestedSearch);

	// 동기 완료가 이미 처리됐으면 새 요청의 상태를 건드리지 않는다.
	if (bAwaitingSearchCompletion && SessionSearch == RequestedSearch &&
		(!bAccepted || RequestedSearch->SearchState == EOnlineAsyncTaskState::NotStarted))
	{
		OnFindSessionsComplete(false);
	}
}

void UCWGameInstance::DiscardPendingSearch()
{
	DeferredFindPurpose = ECWSessionFindPurpose::None;
	// Steam 검색 취소는 실행 중인 비동기 작업의 완료 방송까지 없애지 못한다.
	// 이전 응답을 받기 전 새 검색을 열면 그 응답이 새 요청의 콜백으로 들어온다.
	if (bAwaitingSearchCompletion)
	{
		bDiscardSearchResult = true;
	}
	else
	{
		ClearFindSessionsCompleteDelegate();
	}
	CurrentFindPurpose = ECWSessionFindPurpose::None;
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
	SessionSettings.bIsLANMatch = IsLanOnlySubsystem();   // 검색(StartFindSessions)의 bIsLanQuery와 반드시 짝이 맞아야 한다
	SessionSettings.NumPublicConnections = MaxPublicConnections;
	SessionSettings.bShouldAdvertise = true;
	SessionSettings.bAllowJoinInProgress = true;
	SessionSettings.bAllowInvites = true;
	SessionSettings.bUsesPresence = true;
	SessionSettings.bAllowJoinViaPresence = true;
	SessionSettings.bUseLobbiesIfAvailable = true;

	SessionSettings.Set(SETTING_MAPNAME, ListenMapPath, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	SessionSettings.Set(CrosswalkSession::RoomCodeKey, RoomCode, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	SessionSettings.Set(CrosswalkSession::GameIdKey, CrosswalkSession::GameIdValue, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	SessionSettings.Set(CrosswalkSession::RoomNameKey, PendingRoomName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	SessionSettings.Set(CrosswalkSession::IsPrivateKey, bPendingIsPrivate ? 1 : 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	BindCreateSessionCompleteDelegate();

	CurrentRoomCode = RoomCode;
	CurrentRoomName = PendingRoomName;
	CurrentHostName = LoggedInDisplayName;
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

bool UCWGameInstance::IsPrivateRoom(const FOnlineSessionSearchResult& SearchResult) const
{
	int32 IsPrivate = 0;
	SearchResult.Session.SessionSettings.Get(CrosswalkSession::IsPrivateKey, IsPrivate);

	return IsPrivate != 0;
}

FCWRoomSummary UCWGameInstance::BuildRoomSummary(const FOnlineSessionSearchResult& SearchResult) const
{
	FCWRoomSummary Summary;

	SearchResult.Session.SessionSettings.Get(CrosswalkSession::RoomCodeKey, Summary.RoomCode);
	SearchResult.Session.SessionSettings.Get(CrosswalkSession::RoomNameKey, Summary.RoomName);

	Summary.HostName = SearchResult.Session.OwningUserName;

	// Null 서브시스템(로컬 멀티 PIE 테스트 전용)은 OwningUserName이 컴퓨터 이름이라 방 목록
	// UI에 다 안 들어갈 만큼 길다. 실제 스팀 닉네임은 이 문제가 없으니 Null일 때만 줄인다 -
	// 뒤 4글자만 남기면 같은 기기의 PIE 인스턴스끼리는 여전히 구분된다.
	if (IsLanOnlySubsystem() && Summary.HostName.Len() > 4)
	{
		Summary.HostName = Summary.HostName.Right(4);
	}
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
		// 비밀방은 목록에 노출하지 않는다. 방 코드 입장 경로는 이 필터를 타지 않는다
		if (IsPrivateRoom(SearchResult))
		{
			continue;
		}

		CachedRoomList.Add(BuildRoomSummary(SearchResult));
	}
}

bool UCWGameInstance::BeginSessionOperation(ECWSessionOperation Operation)
{
	// 목록 조회보다 사용자가 선택한 방 생성·참가·나가기를 우선한다.
	if (Operation != ECWSessionOperation::FindRooms)
	{
		CancelRoomSearch();
	}

	if (IsSessionOperationInFlight())
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
	OnSessionBusyChanged.Broadcast(IsSessionOperationInFlight());

	return true;
}

void UCWGameInstance::CompleteSessionOperation(ECWSessionOperation Operation, bool bSuccess, ECWSessionFailure Failure)
{
	/**
	 * 진행 중이던 작업이 끝났을 때만 상태를 내린다.
	 *
	 * 초기 검증에서 실패한 요청은 BeginSessionOperation 을 지난 적이 없다. 그런데도
	 * 무조건 플래그를 내리면, 다른 작업이 OSS 콜백을 기다리는 중에 엉뚱한 요청 하나가
	 * 조기 실패하는 것만으로 그 작업의 in-flight 가 꺼진다. UI 는 한가한 줄 알고
	 * 다음 요청을 받아버리고, 뒤늦게 도착한 콜백이 그 위에서 다시 돈다.
	 */
	if (CurrentSessionOperation == Operation)
	{
		ClearSessionOperationTimeout();

		bSessionOperationInFlight = false;
		CurrentSessionOperation = ECWSessionOperation::None;
	}

	OnSessionBusyChanged.Broadcast(IsSessionOperationInFlight());
	OnSessionOperationCompleted.Broadcast(Operation, bSuccess, Failure);
}

void UCWGameInstance::StartSessionOperationTimeout()
{
	ClearSessionOperationTimeout();

	const float Timeout = CurrentSessionOperation == ECWSessionOperation::FindRooms
		? CrosswalkSession::RoomSearchTimeout : SessionOperationTimeout;
	if (Timeout <= 0.f)
	{
		return;
	}

	// 월드가 아니라 게임 인스턴스의 타이머 매니저를 쓴다.
	// 방 생성은 성공하면 곧바로 ServerTravel 을 타는데, 월드 타이머는 그때 같이 날아간다.
	GetTimerManager().SetTimer(
		SessionOperationTimeoutHandle,
		this,
		&UCWGameInstance::HandleSessionOperationTimeout,
		Timeout,
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

	// 검색은 늦은 응답을 버릴 때까지 구독을 유지하고, 나머지 콜백은 정리한다.
	ClearCreateSessionCompleteDelegate();
	DiscardPendingSearch();
	ClearJoinSessionCompleteDelegate();
	ClearDestroySessionCompleteDelegate();

	CurrentFindPurpose = ECWSessionFindPurpose::None;

	// 롤백 중에 타임아웃이 나면 로컬 상태가 고착되므로 여기서 같이 푼다.
	// 네트워크 실패 복구는 BeginSessionOperation 을 안 타서 이 타이머가 돌지 않는다 - 전용 타이머가 맡는다.
	if (bRollingBackFailedCreate)
	{
		bRollingBackFailedCreate = false;

		CurrentRoomCode.Empty();
		CurrentRoomName.Empty();
		CurrentHostName.Empty();
		PendingRoomCode.Empty();
	}

	const float Timeout = TimedOutOperation == ECWSessionOperation::FindRooms
		? CrosswalkSession::RoomSearchTimeout : SessionOperationTimeout;
	SetStatus(FString::Printf(TEXT("Session operation timed out after %.0fs"), Timeout));
	if (TimedOutOperation == ECWSessionOperation::FindRooms)
	{
		CachedRoomList.Reset();
		OnRoomListUpdated.Broadcast(CachedRoomList);
	}
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
		CurrentRoomName.Empty();
		CurrentHostName.Empty();
		SetStatus(TEXT("CreateSession failed"));
		CompleteSessionOperation(ECWSessionOperation::CreateRoom, false, ECWSessionFailure::Unknown);
		return;
	}

	// 호스트 이름은 참가자와 같은 소스(OwningUserName)에서 읽어야 양쪽이 같은 문자열을 봄
	// 미로그인/Null 서브시스템이면 LoggedInDisplayName 이 비어 있어 이 경로가 사실상 유일
	if (SessionInterface.IsValid())
	{
		if (FNamedOnlineSession* NamedSession = SessionInterface->GetNamedSession(SessionName))
		{
			if (!NamedSession->OwningUserName.IsEmpty())
			{
				CurrentHostName = NamedSession->OwningUserName;
			}
		}
	}

	UWorld* World = GetWorld();

	// 아래 세 경로 모두 세션이 이미 만들어진 뒤의 실패다 - 전부 같은 롤백으로 보낸다
	if (World == nullptr)
	{
		RollbackCreatedSession(TEXT("world is null"));
		return;
	}

	// 넷드라이버가 이미 있으면 Listen 이 NetDriverAlreadyExists 로 실패하므로 미리 막음
	if (World->GetNetMode() != NM_Standalone)
	{
		RollbackCreatedSession(TEXT("world is already networked"));
		return;
	}

	// 트래블 대신 현재 월드를 그대로 리슨 서버로 전환한다.
	// 호스트는 맵을 다시 로딩하지 않으므로 방 화면이 곧바로 열린다.
	// 현재 월드 URL 을 그대로 쓰고 listen 옵션만 붙여 맵 경로와 포트를 다시 적지 않는다.
	FURL ListenURL = World->URL;
	ListenURL.AddOption(TEXT("listen"));

	if (!World->Listen(ListenURL))
	{
		RollbackCreatedSession(TEXT("Listen failed"));
		return;
	}

	// 트래블을 하지 않으므로 ?Name= 옵션이 소비될 자리가 없다.
	// 옵션 없이는 PlayerState 이름이 GameMode::DefaultPlayerName("Player")로 고정되므로
	// 호스트 이름은 여기서 직접 갱신한다. 참가자는 ClientTravel 이 그대로라 손댈 게 없다.
	if (!LoggedInDisplayName.IsEmpty())
	{
		AGameModeBase* GameMode = World->GetAuthGameMode();
		APlayerController* HostController = UGameplayStatics::GetPlayerController(this, 0);

		if (GameMode != nullptr && HostController != nullptr)
		{
			GameMode->ChangeName(HostController, LoggedInDisplayName, false);
		}
	}

	// 리슨이 실제로 열린 뒤에 알려야 UI 가 방 화면을 여는 시점과 어긋나지 않음
	SetStatus(FString::Printf(TEXT("CreateSession success. RoomCode: %s"), *CurrentRoomCode));
	CompleteSessionOperation(ECWSessionOperation::CreateRoom, true, ECWSessionFailure::None);
}

void UCWGameInstance::RollbackCreatedSession(const FString& Reason)
{
	SetStatus(FString::Printf(TEXT("CreateRoom failed: %s"), *Reason));

	// 여기서 CreateRoom 을 끝내지 않는다.
	// 실패를 먼저 방송하면 UI 는 생성이 끝난 줄 알고 방 만들기 버튼을 다시 연다.
	// 그런데 내부에서는 아직 DestroySession 이 돌고 있어서 그 사이에 누르면 Busy 가 난다.
	// 같은 이유로 사용자용 DestroyRoom 도 쓰지 않는다 - BeginSessionOperation 이
	// in-flight 를 보고 Busy 로 거절해 DestroySession 까지 가지도 못한다.
	bRollingBackFailedCreate = true;

	if (!SessionInterface.IsValid() ||
		SessionInterface->GetNamedSession(CrosswalkSession::GameSessionName) == nullptr)
	{
		// 되돌릴 세션이 없음
		FinishRollbackAsCreateFailure();
		return;
	}

	BindDestroySessionCompleteDelegate();

	// 즉시 false 면 완료 델리게이트가 오지 않는다 - 타임아웃까지 기다리지 않고 여기서 끝냄
	if (!SessionInterface->DestroySession(CrosswalkSession::GameSessionName))
	{
		ClearDestroySessionCompleteDelegate();
		FinishRollbackAsCreateFailure();
	}
}

void UCWGameInstance::FinishRollbackAsCreateFailure()
{
	// 동기 콜백과 실패 반환이 겹치면 두 번 불릴 수 있음
	if (!bRollingBackFailedCreate)
	{
		return;
	}

	bRollingBackFailedCreate = false;

	CurrentRoomCode.Empty();
	CurrentRoomName.Empty();
	CurrentHostName.Empty();
	PendingRoomCode.Empty();

	// DestroyRoom 완료가 아니라 CreateRoom 실패로 끝낸다 - 사용자가 요청한 작업은 생성이었음
	CompleteSessionOperation(
		ECWSessionOperation::CreateRoom,
		false,
		ECWSessionFailure::Unknown
	);
}

void UCWGameInstance::RecoverToMenuAfterNetworkFailure()
{
	// 네트워크 실패가 연속으로 들어와도 진행 중인 DestroySession 델리게이트와
	// 복구 타이머를 다시 덮어쓰지 않는다.
	if (bRecoveringFromNetworkFailure)
	{
		return;
	}

	bRecoveringFromNetworkFailure = true;

	// 연결이 끊긴 월드를 기억해 둔다.
	// 통합 후에는 방 화면도 Title 맵 위에 뜨므로 맵 이름으로는 구분되지 않는다.
	NetworkFailureWorld = GetWorld();

	// 진행 중이던 사용자 작업을 먼저 끊는다.
	// 아래에서 델리게이트를 다시 바인딩하면 BindDestroySessionCompleteDelegate 가
	// 기존 핸들을 지워버려 그 작업은 영영 끝나지 않는다.
	CancelCurrentSessionOperationForRecovery();

	if (!SessionInterface.IsValid() ||
		SessionInterface->GetNamedSession(CrosswalkSession::GameSessionName) == nullptr)
	{
		FinishNetworkFailureRecovery();
		return;
	}

	BindDestroySessionCompleteDelegate();

	// 타이머를 요청보다 먼저 건다.
	// DestroySession 이 완료 델리게이트를 동기로 쏘면 콜백이 타이머를 해제하고 돌아오는데,
	// 그 뒤에 타이머를 걸면 아무도 끄지 않는 타이머가 남아 복구가 한 번 더 돈다.
	StartNetworkFailureRecoveryTimeout();

	if (!SessionInterface->DestroySession(CrosswalkSession::GameSessionName))
	{
		ClearDestroySessionCompleteDelegate();
		ClearNetworkFailureRecoveryTimeout();
		FinishNetworkFailureRecovery();
	}
}

void UCWGameInstance::CancelCurrentSessionOperationForRecovery()
{
	// 강제로 메뉴로 돌아가는 경로이므로 진행 중이던 작업의 결과는 의미가 없다.
	// 실패 결과 대신 실제 작업 상태만 ViewModel에 전달한다.
	ClearSessionOperationTimeout();

	ClearCreateSessionCompleteDelegate();
	DiscardPendingSearch();
	ClearJoinSessionCompleteDelegate();
	ClearDestroySessionCompleteDelegate();

	CurrentFindPurpose = ECWSessionFindPurpose::None;
	CurrentSessionOperation = ECWSessionOperation::None;
	bSessionOperationInFlight = false;

	// 생성 롤백이 돌고 있었다면 그것도 여기서 끝난다
	bRollingBackFailedCreate = false;
	OnSessionBusyChanged.Broadcast(IsSessionOperationInFlight());
}

void UCWGameInstance::FinishNetworkFailureRecovery()
{
	// 동기 콜백과 실패 반환이 겹치면 두 번 불릴 수 있음
	if (!bRecoveringFromNetworkFailure)
	{
		return;
	}

	ClearNetworkFailureRecoveryTimeout();

	// 정리에 실패해 세션이 남아도 여기서 플래그를 내린다.
	// 남은 세션이 착지를 오염시키지 않기 때문이다 - 새 월드는 NM_Standalone 이고
	// ShouldEnterRoomPage 가 넷 모드를 함께 보므로 자동으로 메뉴로 간다.
	bRecoveringFromNetworkFailure = false;

	CurrentRoomCode.Empty();
	CurrentRoomName.Empty();
	CurrentHostName.Empty();
	PendingRoomCode.Empty();
	PendingJoinRoomCode.Empty();
	CachedRoomList.Reset();

	OnSessionBusyChanged.Broadcast(IsSessionOperationInFlight());
	OnRoomListUpdated.Broadcast(CachedRoomList);

	// BGM 도 여기서 정지. 메뉴로 보내는 것만으로는 스테이지 곡이 따라옴
	// Deinitialize 는 게임 인스턴스가 죽을 때만 불려 여기선 못 씀
	if (UCWMusicSubsystem* MusicSubsystem = GetSubsystem<UCWMusicSubsystem>())
	{
		MusicSubsystem->StopMusic();
	}

	// 메뉴로 돌려보내는 것까지가 이 경로의 책임이다.
	// 사용자용 DestroyRoom 을 안 타므로 완료도 방송되지 않고, 따라서 방 화면의
	// OpenLevel 도 실행되지 않는다. 여기서 안 옮기면 연결이 끊긴 화면에 그대로 남는다.
	// 다음 틱으로 미루는 이유는 세션이 없을 때 이 함수가 HandleNetworkFailure 에서
	// 동기적으로 불릴 수 있는데, 그 시점은 엔진이 연결을 정리하는 중이기 때문이다.
	TravelToMenuNextTickHandle =
		GetTimerManager().SetTimerForNextTick(this, &UCWGameInstance::TravelToMenuMap);
}

void UCWGameInstance::TravelToMenuMap()
{
	UWorld* CurrentWorld = GetWorld();

	if (!IsValid(CurrentWorld) || MenuMapPath.IsEmpty())
	{
		return;
	}

	// 맵 이름으로 판별하면 안 된다.
	// 통합 후에는 방 화면도 Title 맵 위에 뜨므로, 연결이 끊긴 그 월드에서도
	// "현재 맵 == MenuMapPath" 가 참이라 이동을 건너뛰고 죽은 화면에 남는다.
	// 월드 객체를 비교한다 - 같은 맵이어도 새로 열어야 넷드라이버와 UI 가 초기화된다.
	if (CurrentWorld == NetworkFailureWorld.Get())
	{
		UGameplayStatics::OpenLevel(this, FName(*MenuMapPath));
		return;
	}

	// 엔진이 이미 새 월드로 옮겼다. 그 월드는 NM_Standalone 이라 메뉴 화면이 선택된다
}

void UCWGameInstance::StartNetworkFailureRecoveryTimeout()
{
	ClearNetworkFailureRecoveryTimeout();

	if (SessionOperationTimeout <= 0.f)
	{
		return;
	}

	// 세션 작업 타임아웃과 같은 이유로 게임 인스턴스 타이머를 쓴다 - 월드 타이머는 트래블에 날아감
	GetTimerManager().SetTimer(
		NetworkFailureRecoveryTimeoutHandle,
		this,
		&UCWGameInstance::HandleNetworkFailureRecoveryTimeout,
		SessionOperationTimeout,
		false
	);
}

void UCWGameInstance::ClearNetworkFailureRecoveryTimeout()
{
	GetTimerManager().ClearTimer(NetworkFailureRecoveryTimeoutHandle);
}

void UCWGameInstance::HandleNetworkFailureRecoveryTimeout()
{
	SetStatus(TEXT("NetworkFailure recovery timed out"));

	// 응답이 끝내 오지 않았다. 남은 델리게이트를 걷어내고 메뉴 복귀는 그대로 진행한다
	ClearDestroySessionCompleteDelegate();
	FinishNetworkFailureRecovery();
}

void UCWGameInstance::OnFindSessionsComplete(bool bWasSuccessful)
{
	if (!bAwaitingSearchCompletion)
	{
		return;
	}

	ClearFindSessionsCompleteDelegate();
	bAwaitingSearchCompletion = false;

	const ECWSessionFindPurpose FinishedPurpose = CurrentFindPurpose;
	CurrentFindPurpose = ECWSessionFindPurpose::None;

	if (bDiscardSearchResult)
	{
		bDiscardSearchResult = false;
		const ECWSessionFindPurpose NextPurpose = DeferredFindPurpose;
		DeferredFindPurpose = ECWSessionFindPurpose::None;
		SessionSearch.Reset();
		if (NextPurpose != ECWSessionFindPurpose::None && bSessionOperationInFlight && !bRecoveringFromNetworkFailure)
		{
			StartFindSessions(NextPurpose);
		}
		return;
	}

	if (bWasSuccessful && SessionSearch.IsValid())
	{
		// 480 공용 풀 필터링 - 아래 어떤 목적(방 목록/방 코드 중복확인/방 코드 참가)으로 쓰이든
		// 남의 게임 로비가 섞여 있으면 안 되므로, 갈래를 타기 전에 한 번만 걸러낸다.
		SessionSearch->SearchResults.RemoveAll([](const FOnlineSessionSearchResult& SearchResult)
		{
			FString FoundGameId;
			SearchResult.Session.SessionSettings.Get(CrosswalkSession::GameIdKey, FoundGameId);
			return FoundGameId != CrosswalkSession::GameIdValue;
		});
	}

	switch (FinishedPurpose)
	{
	case ECWSessionFindPurpose::ManualRefresh:
		HandleFindSessionsForManualRefresh(bWasSuccessful);
		return;

	case ECWSessionFindPurpose::CheckRoomCodeForCreate:
		HandleFindSessionsForCreateRoomCodeCheck(bWasSuccessful);
		return;

	case ECWSessionFindPurpose::JoinByRoomCode:
		HandleFindSessionsForJoinByRoomCode(bWasSuccessful);
		return;

	default:
		return;
	}
}

void UCWGameInstance::HandleFindSessionsForManualRefresh(bool bWasSuccessful)
{
	if (!bWasSuccessful || !SessionSearch.IsValid())
	{
		SetStatus(TEXT("FindSessions failed"));
		CachedRoomList.Reset();
		OnRoomListUpdated.Broadcast(CachedRoomList);
		CompleteSessionOperation(ECWSessionOperation::FindRooms, false, ECWSessionFailure::Unknown);
		return;
	}

	SetStatus(FString::Printf(TEXT("FindSessions success. Results: %d"), SessionSearch->SearchResults.Num()));
	RefreshCachedRoomList();
	OnRoomListUpdated.Broadcast(CachedRoomList);
	CompleteSessionOperation(ECWSessionOperation::FindRooms, true, ECWSessionFailure::None);
}

void UCWGameInstance::HandleFindSessionsForCreateRoomCodeCheck(bool bWasSuccessful)
{
	if (!bWasSuccessful || !SessionSearch.IsValid())
	{
		SetStatus(TEXT("FindSessions failed"));
		CompleteSessionOperation(ECWSessionOperation::CreateRoom, false, ECWSessionFailure::Unknown);
		return;
	}

	if (HasRoomCodeInSearchResults(PendingRoomCode))
	{
		if (RoomCodeGenerateAttemptCount >= MaxRoomCodeGenerateAttempts)
		{
			SetStatus(TEXT("CreateRoom failed: could not generate unique room code"));
			CompleteSessionOperation(ECWSessionOperation::CreateRoom, false, ECWSessionFailure::RoomCodeCollision);
			return;
		}

		++RoomCodeGenerateAttemptCount;
		PendingRoomCode = GenerateRoomCode();
		StartFindSessions(ECWSessionFindPurpose::CheckRoomCodeForCreate);
		return;
	}

	CreateSessionWithRoomCode(PendingRoomCode);
}

void UCWGameInstance::HandleFindSessionsForJoinByRoomCode(bool bWasSuccessful)
{
	if (!bWasSuccessful || !SessionSearch.IsValid())
	{
		SetStatus(TEXT("FindSessions failed"));
		CompleteSessionOperation(ECWSessionOperation::JoinRoom, false, ECWSessionFailure::Unknown);
		return;
	}

	for (FOnlineSessionSearchResult& SearchResult : SessionSearch->SearchResults)
	{
		FString FoundRoomCode;

		if (SearchResult.Session.SessionSettings.Get(CrosswalkSession::RoomCodeKey, FoundRoomCode))
		{
			if (FoundRoomCode.Equals(PendingJoinRoomCode, ESearchCase::IgnoreCase))
			{
				BindJoinSessionCompleteDelegate();

				// [Steam 엔진 버그 우회] JoinFirstFoundRoom과 같은 이유 - 검색 결과의
				// bUseLobbiesIfAvailable이 로비 데이터 왕복에서 유실돼 항상 false로 복원되고,
				// bUsesPresence(true)와 어긋나면 JoinSession이 곧바로 UnknownError로 실패한다.
				SearchResult.Session.SessionSettings.bUseLobbiesIfAvailable =
					SearchResult.Session.SessionSettings.bUsesPresence;

				SetStatus(FString::Printf(TEXT("JoinSession requested. RoomCode: %s"), *PendingJoinRoomCode));
				SessionInterface->JoinSession(0, CrosswalkSession::GameSessionName, SearchResult);

				return;
			}
		}
	}

	SetStatus(FString::Printf(TEXT("JoinRoomByCode failed: room not found. RoomCode: %s"), *PendingJoinRoomCode));
	CompleteSessionOperation(ECWSessionOperation::JoinRoom, false, ECWSessionFailure::RoomNotFound);
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

	// 호스트가 광고한 제목을 그대로 씀. 없으면 빈 값 - 대체 표시는 UI 담당
	CurrentRoomName.Empty();
	CurrentHostName.Empty();

	if (FNamedOnlineSession* NamedSession = SessionInterface->GetNamedSession(SessionName))
	{
		NamedSession->SessionSettings.Get(CrosswalkSession::RoomCodeKey, JoinedRoomCode);
		NamedSession->SessionSettings.Get(CrosswalkSession::RoomNameKey, CurrentRoomName);
		CurrentHostName = NamedSession->OwningUserName;
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

	// 내부 경로 두 개가 같은 콜백을 쓴다. 기존 성공·실패 판정보다 앞에서 갈라야 한다.
	// 내부 정리는 OSS 성공 여부와 무관하게 로컬 복귀를 계속해야 하기 때문이다.
	if (bRollingBackFailedCreate)
	{
		FinishRollbackAsCreateFailure();
		return;
	}

	if (bRecoveringFromNetworkFailure)
	{
		FinishNetworkFailureRecovery();
		return;
	}

	if (!bWasSuccessful)
	{
		SetStatus(TEXT("DestroySession failed"));
		CompleteSessionOperation(ECWSessionOperation::DestroyRoom, false, ECWSessionFailure::Unknown);
		return;
	}

	CurrentRoomCode.Empty();
	CurrentRoomName.Empty();
	CurrentHostName.Empty();
	PendingRoomCode.Empty();
	PendingJoinRoomCode.Empty();
	CachedRoomList.Reset();

	SetStatus(TEXT("DestroySession success"));
	OnRoomListUpdated.Broadcast(CachedRoomList);
	CompleteSessionOperation(ECWSessionOperation::DestroyRoom, true, ECWSessionFailure::None);
}
