// CWUISubsystem.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Data/Enums/CWUITypes.h"
#include "UI/Data/CWUIPageConfigDataAsset.h"
#include "CWUISubsystem.generated.h"

class UCWUIRootWidget;
class UCWUIScreenWidget;
class UCWSessionViewModel;
class UCWToastWidget;
class UUserWidget;
class UWorld;

UCLASS()
class CROSSWALK_API UCWUISubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

#pragma region Initialize

public:
	// 서브시스템 생성 시 전역 Session ViewModel을 등록
	virtual void Initialize(
		FSubsystemCollectionBase& Collection
	) override;

	// 서브시스템 종료 시 등록한 Session ViewModel을 정리
	virtual void Deinitialize() override;

	// UI 클래스 설정을 저장하고 Root를 준비
	UFUNCTION(BlueprintCallable, Category = "CW|UI")
	void InitializeUI(
		TSubclassOf<UCWUIRootWidget> InRootClass,
		UCWUIPageConfigDataAsset* InPageConfigData
	);

#pragma endregion

#pragma region Page

public:
	// 요청한 페이지로 화면을 전환
	UFUNCTION(BlueprintCallable, Category = "CW|UI")
	bool OpenPage(ECWUIPage TargetPage);

	// 열린 페이지를 제거하고 열기 전 입력 모드·HUD 상태로 복귀
	UFUNCTION(BlueprintCallable, Category = "CW|UI")
	bool ClosePage();

	// 현재 표시 중인 페이지를 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI")
	ECWUIPage GetCurrentPage() const;

private:
	// 현재 페이지를 닫고 참조를 정리
	UFUNCTION(BlueprintCallable, Category = "CW|UI")
	void CloseCurrentPage();

	// 화면의 페이지 전환 요청을 처리
	UFUNCTION()
	void HandleRequestOpenPage(ECWUIPage TargetPage);

#pragma endregion

#pragma region Root

private:
	// 현재 World에서 사용할 Root를 확인하거나 생성
	bool EnsureUIRoot();

#pragma endregion

#pragma region Transition

private:
	// 화면 전환 상태와 입력 차단을 시작
	void BeginTransition();

	// 화면 전환 상태와 입력 차단을 종료
	void EndTransition();

#pragma endregion

#pragma region Input

public:
	// 로컬 PlayerController에 지정한 입력 모드를 적용
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Input")
	void ApplyInputMode(ECWUIInputMode InInputMode);

#pragma endregion

#pragma region HUD

public:
	// 현재 Root의 HUD 레이어에 Widget을 추가
	UFUNCTION(BlueprintCallable, Category = "CW|UI|HUD")
	bool AddHUDWidget(UUserWidget* InWidget);

	// 현재 Root의 HUD 레이어에서 Widget을 제거
	UFUNCTION(BlueprintCallable, Category = "CW|UI|HUD")
	bool RemoveHUDWidget(UUserWidget* InWidget);

	// 현재 Root의 HUD 레이어를 모두 정리
	UFUNCTION(BlueprintCallable, Category = "CW|UI|HUD")
	void ClearHUDWidgets();

	// 현재 Root의 HUD 레이어 표시 여부를 변경
	UFUNCTION(BlueprintCallable, Category = "CW|UI|HUD")
	void SetHUDVisible(bool bVisible);

#pragma endregion

#pragma region Feedback

public:
	// 상호작용을 차단하는 팝업을 Root의 Popup 레이어에 표시
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Feedback")
	bool OpenPopup(UUserWidget* InWidget);

	// 현재 표시 중인 팝업을 닫고 이전 입력 모드로 복귀
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Feedback")
	bool ClosePopup(UUserWidget* InWidget);

	// 비차단 안내 문구를 토스트 큐에 추가
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Feedback")
	void ShowToast(const FText& Message);

private:
	// 대기 중인 다음 토스트를 생성하고 표시
	void ShowNextToast();

	// 현재 토스트를 제거하고 다음 요청을 처리
	void HandleToastFinished();

	// 현재 토스트와 대기 요청 및 타이머 상태를 모두 정리
	void ResetToastState();

#pragma endregion

#pragma region ViewModel

private:
	// Global Viewmodel Collection에 등록하거나 조회한 Session ViewModel
	UPROPERTY(Transient)
	TWeakObjectPtr<UCWSessionViewModel> SessionViewModel;

	// 이 서브시스템이 전역 Session ViewModel을 직접 등록했는지 구분
	bool bOwnsSessionViewModelRegistration = false;

#pragma endregion

#pragma region UIConfiguration

private:
	// 현재 사용할 Root Widget 클래스
	UPROPERTY()
	TSubclassOf<UCWUIRootWidget> UIRootClass;

	// 병합된 페이지 정책. 맵 이동 시 이전 것을 유지하며 같은 키만 덮어씀
	UPROPERTY()
	TMap<ECWUIPage, FCWUIPageConfig> PageConfigs;

#pragma endregion

#pragma region RuntimeState

private:
	// 현재 World에 표시 중인 Root
	UPROPERTY(Transient)
	TWeakObjectPtr<UCWUIRootWidget> UIRoot;

	// 현재 Screen Layer에 표시 중인 화면
	UPROPERTY(Transient)
	TWeakObjectPtr<UCWUIScreenWidget> CurrentScreen;

	// 현재 표시 중인 페이지
	ECWUIPage CurrentPage = ECWUIPage::None;

	// 화면 전환 진행 여부
	bool bIsTransitioning = false;

	// 현재 로컬 플레이어에게 적용된 입력 모드
	ECWUIInputMode CurrentInputMode = ECWUIInputMode::UIOnly;

	// 현재 HUD 레이어의 실제 표시 상태
	bool bIsHUDVisible = true;

	// 현재 페이지가 열리기 전 HUD 표시 상태
	bool bHUDWasVisibleBeforePage = true;

	// 현재 페이지 정책이 HUD를 숨겼는지 여부
	bool bCurrentPageHidesHUD = false;

	// 현재 페이지를 열기 전 사용하던 입력 모드
	ECWUIInputMode InputModeBeforeCurrentPage = ECWUIInputMode::UIOnly;

	// 팝업을 닫은 뒤 복원할 입력 모드
	ECWUIInputMode PopupReturnInputMode = ECWUIInputMode::UIOnly;

	// 현재 Popup 레이어에 표시 중인 팝업
	UPROPERTY(Transient)
	TWeakObjectPtr<UUserWidget> CurrentPopup;

	// 순서대로 표시할 토스트 문구
	TArray<FText> ToastQueue;

	// 현재 Toast 레이어에 표시 중인 토스트
	UPROPERTY(Transient)
	TWeakObjectPtr<UCWToastWidget> CurrentToast;

	// 현재 표시 중인 문구
	FText CurrentToastMessage;

	// 현재 토스트의 표시 종료를 예약하는 타이머
	FTimerHandle ToastTimerHandle;

	// 현재 토스트 상태가 만들어진 World
	// 맵 이동 후 이전 화면의 알림을 구분하는 데 사용
	UPROPERTY(Transient)
	TWeakObjectPtr<UWorld> ToastWorld;

#pragma endregion



};
