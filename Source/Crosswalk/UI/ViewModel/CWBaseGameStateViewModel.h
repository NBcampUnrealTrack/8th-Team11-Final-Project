// CWBaseGameStateViewModel.h

#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "CWBaseGameStateViewModel.generated.h"

class AGameStateBase;
class APlayerController;
class UWorld;

UCLASS(Abstract)
class CROSSWALK_API UCWBaseGameStateViewModel : public UMVVMViewModelBase
{
	GENERATED_BODY()

#pragma region ViewModelLifecycle

public:
	// 현재 월드의 GameState를 찾아 구독을 시작
	UFUNCTION(BlueprintCallable, Category = "CW|UI|ViewModel")
	void Initialize(UObject* WorldContextObject);

	// GameState와 World 이벤트 구독을 정리
	UFUNCTION(BlueprintCallable, Category = "CW|UI|ViewModel")
	void Uninitialize();

#pragma endregion

#pragma region GameStateBinding

protected:
	// 현재 화면을 소유한 로컬 PlayerController를 반환
	APlayerController* GetOwningPlayerController() const;

	// 초기화에 사용된 현재 플레이 월드를 반환
	UWorld* GetBoundWorld() const;

	// 리슨 서버의 로컬 호스트인지 (파생 공용)
	bool IsLocalHost() const;

	// 현재 구독 중인 GameState 반환 (파생은 Cast 해서 사용)
	AGameStateBase* GetBoundGameState() const;

	// 공통 바인딩 골격 (타입 확인·중복 방지·구독·즉시 갱신) — 파생이 override 하지 않음
	void BindGameState(AGameStateBase* InGameState);

	// 현재 GameState 구독 해제 (무효여도 파생 정리 hook 호출)
	void UnbindGameState();

	// --- 파생이 구현할 hook ---

	// 이 ViewModel이 바인딩할 GameState 클래스
	virtual UClass* GetGameStateClass() const
		PURE_VIRTUAL(UCWBaseGameStateViewModel::GetGameStateClass, return nullptr;);

	// 전달된 GameState에 자기 delegate 구독
	virtual void SubscribeToGameState(AGameStateBase* InGameState)
		PURE_VIRTUAL(UCWBaseGameStateViewModel::SubscribeToGameState, );

	// 전달된 GameState에서 자기 delegate 해제 (무효여도 타이머 등 정리)
	virtual void UnsubscribeFromGameState(AGameStateBase* InGameState)
		PURE_VIRTUAL(UCWBaseGameStateViewModel::UnsubscribeFromGameState, );

	// 현재 GameState에서 최신 값을 즉시 읽어 반영
	virtual void RefreshFromGameState()
		PURE_VIRTUAL(UCWBaseGameStateViewModel::RefreshFromGameState, );

	// 표시 상태를 기본값으로 되돌린다
	virtual void ResetViewModelState()
		PURE_VIRTUAL(UCWBaseGameStateViewModel::ResetViewModelState, );

private:
	// GameState가 늦게 생성된 경우 다시 구독
	void HandleGameStateSet(AGameStateBase* InGameState);

#pragma endregion

#pragma region ViewModelState

private:
	// 초기화에 사용된 현재 플레이 월드
	UPROPERTY()
	TWeakObjectPtr<UWorld> BoundWorld;

	// Initialize에 전달된 위젯
	// Seamless Travel 이후에도 현재 화면의 Owning Player를 찾는 데 사용
	UPROPERTY()
	TWeakObjectPtr<UObject> BoundContextObject;

	// GameState 생성 이벤트 구독 해제용 핸들
	FDelegateHandle GameStateSetHandle;

	// 현재 구독 중인 GameState (private — 파생은 GetBoundGameState()로만 읽음)
	UPROPERTY()
	TWeakObjectPtr<AGameStateBase> BoundGameState;

#pragma endregion
};
