// CWBaseGameStateViewModel.cpp

#include "UI/ViewModel/CWBaseGameStateViewModel.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"

#pragma region ViewModelLifecycle

void UCWBaseGameStateViewModel::Initialize(UObject* WorldContextObject)
{
	// 재초기화 시 기존 구독이 중복 등록되지 않도록 정리
	Uninitialize();

	if (!IsValid(WorldContextObject))
	{
		return;
	}

	UWorld* World = WorldContextObject->GetWorld();

	if (!IsValid(World))
	{
		return;
	}

	BoundContextObject = WorldContextObject;
	BoundWorld = World;

	// GameState가 ViewModel보다 늦게 생성되는 상황 처리
	GameStateSetHandle = World->GameStateSetEvent.AddUObject(
		this,
		&UCWBaseGameStateViewModel::HandleGameStateSet
	);

	// 이미 생성돼 있으면 파생이 지정한 GameState 타입을 확인하고 즉시 구독
	BindGameState(World->GetGameState<AGameStateBase>());
}

void UCWBaseGameStateViewModel::Uninitialize()
{
	// 현재 GameState의 이벤트 구독 해제
	UnbindGameState();

	// 월드의 GameState 생성 이벤트 구독 해제
	if (UWorld* World = BoundWorld.Get())
	{
		if (GameStateSetHandle.IsValid())
		{
			World->GameStateSetEvent.Remove(GameStateSetHandle);
		}
	}

	GameStateSetHandle.Reset();
	BoundContextObject.Reset();
	BoundWorld.Reset();

	// 이전 월드의 데이터가 다음 화면에 남지 않도록 초기화
	ResetViewModelState();
}

#pragma endregion

#pragma region GameStateBinding

APlayerController* UCWBaseGameStateViewModel::GetOwningPlayerController() const
{
	if (
		const UUserWidget* OwningWidget =
		Cast<UUserWidget>(BoundContextObject.Get())
		)
	{
		// 위젯인데 OwningPlayer가 아직 없으면
		// 잘못된 컨트롤러를 고르지 않고 null을 반환한다.
		// 리슨 서버에서 GetFirstPlayerController()는 참가자 것을 돌려줄 수 있다
		return OwningWidget->GetOwningPlayer();
	}

	// 위젯이 아닌 객체로 초기화한 경우에만 예비 경로 사용
	if (const UWorld* World = BoundWorld.Get())
	{
		return World->GetFirstPlayerController();
	}

	return nullptr;
}

UWorld* UCWBaseGameStateViewModel::GetBoundWorld() const
{
	return BoundWorld.Get();
}

bool UCWBaseGameStateViewModel::IsLocalHost() const
{
	const APlayerController* PlayerController =
		GetOwningPlayerController();

	// 리슨 서버의 로컬 컨트롤러만 호스트로 판정 (연결 지연 시 null 방어)
	return
		IsValid(PlayerController) &&
		PlayerController->IsLocalController() &&
		PlayerController->HasAuthority();
}

AGameStateBase* UCWBaseGameStateViewModel::GetBoundGameState() const
{
	return BoundGameState.Get();
}

void UCWBaseGameStateViewModel::BindGameState(AGameStateBase* InGameState)
{
	// 타입이 다르거나 GameState가 무효면 기존 구독과 표시 상태를 정리
	if (!IsValid(InGameState) ||
		!InGameState->IsA(GetGameStateClass()))
	{
		UnbindGameState();
		ResetViewModelState();
		return;
	}

	// 같은 GameState에 중복 바인딩하지 않음
	if (BoundGameState.Get() == InGameState)
	{
		return;
	}

	UnbindGameState();

	BoundGameState = InGameState;

	SubscribeToGameState(InGameState);

	// OnRep가 이미 지난 경우를 위해 현재 값을 즉시 한 번 반영
	RefreshFromGameState();
}

void UCWBaseGameStateViewModel::UnbindGameState()
{
	// GameState가 이미 무효여도 파생의 정리(타이머 등)는 실행돼야 하므로 항상 호출
	UnsubscribeFromGameState(BoundGameState.Get());

	BoundGameState.Reset();
}

void UCWBaseGameStateViewModel::HandleGameStateSet(AGameStateBase* InGameState)
{
	// 파생이 지정한 GameState 타입 확인과 바인딩을 공통 처리
	BindGameState(InGameState);
}

#pragma endregion
