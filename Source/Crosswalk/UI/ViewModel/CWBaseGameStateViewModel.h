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

	// GameState가 준비되면 호출. 파생 클래스가 타입을 확인하고 구독한다
	virtual void BindGameState(AGameStateBase* InGameState)
		PURE_VIRTUAL(UCWBaseGameStateViewModel::BindGameState, );

	// 현재 구독 중인 GameState에서 구독을 해제한다
	virtual void UnbindGameState()
		PURE_VIRTUAL(UCWBaseGameStateViewModel::UnbindGameState, );

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

#pragma endregion
};
