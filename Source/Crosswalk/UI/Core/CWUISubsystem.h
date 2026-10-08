// CWUISubsystem.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Data/Enums/CWSessionTypes.h"
#include "Data/Enums/CWUITypes.h"
#include "UI/Data/CWUIPageConfigDataAsset.h"
#include "UI/Feedback/CWBaseConfirmDialog.h"
#include "CWUISubsystem.generated.h"

class UCWUIRootWidget;
class UCWUIScreenWidget;
class UCWSessionViewModel;
class UCWToastWidget;
class UUserWidget;
class UWorld;
class UCWPauseMenuWidget;

// 페이지가 바뀐 뒤 확정된 값만 전달하며 열기·닫기 도중의 중간 상태는 내보내지 않음
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FCWPageChangedSignature,
	ECWUIPage, OldPage,
	ECWUIPage, NewPage
);

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

	// 현재 페이지를 열기 직전에 떠 있던 페이지를 반환
	// 겹쳐 연 화면을 닫을 때 어디로 돌아갈지 고르는 값임
	UFUNCTION(BlueprintPure, Category = "CW|UI")
	ECWUIPage GetPageBeforeCurrent() const { return PageBeforeCurrentPage; }

	// 돌아갈 페이지가 있는지 여부
	// 인게임은 페이지 없이 시작해 None 이 곧 게임 화면으로의 복귀를 뜻함
	UFUNCTION(BlueprintPure, Category = "CW|UI")
	bool HasPageToReturn() const { return PageBeforeCurrentPage != ECWUIPage::None; }

#pragma endregion

#pragma region Input

public:
	// 로컬 PlayerController에 지정한 입력 모드를 적용
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Input")
	void ApplyInputMode(ECWUIInputMode InInputMode);

	void SetPauseMenu(UCWPauseMenuWidget* InMenu);
	void ClearPauseMenu(UCWPauseMenuWidget* InMenu);
	UCWSessionViewModel* GetSessionViewModel() const;

	// 방 나가기(DestroyRoom) 완료의 공통 후속 처리 - 성공이면 메뉴 맵을 열고, 실패하면 사유를 토스트로 알림
	// 화면마다 복제하던 처리를 한 곳에 모은 것이며, 방 나가기가 아닌 작업의 완료는 처리하지 않고 false 를 반환
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Session")
	bool HandleRoomLeaveResult(ECWSessionOperation Operation, bool bSuccess);

	// HUD 버튼과 같은 입구를 키 입력에도 열어 줌
	// 다른 페이지가 열려 있거나 차단 중이면 열지 않음
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Pause")
	bool OpenPauseMenu();

	// 클리어 연출처럼 메뉴가 끼어들면 안 되는 구간을 HUD 가 알려 줌
	// 입력 모드에만 기대면 다른 호출 경로가 생겼을 때 그대로 우회됨
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Pause")
	void SetPauseMenuBlocked(bool bBlocked);

	// 클리어 연출이 시작될 때 연출을 가리는 것을 닫음 - 일시정지 메뉴와 열려 있는 입장 안내 팝업
	// 입장 안내를 닫지 않으면 클리어 뒤에도 결과 화면 위에 남음
	void DismissOverlaysForClear();

	// 설정 화면을 닫은 뒤 기존 메뉴로 키보드 포커스를 돌려줌
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Pause")
	bool RestorePauseMenuFocus();

#pragma endregion

#pragma region HUD

public:
	// 현재 Root의 HUD 레이어에 Widget을 추가
	UFUNCTION(BlueprintCallable, Category = "CW|UI|HUD")
	bool AddHUDWidget(UUserWidget* InWidget);

	// 현재 Root의 HUD 레이어 표시 여부를 변경
	UFUNCTION(BlueprintCallable, Category = "CW|UI|HUD")
	void SetHUDVisible(bool bVisible);

	// 입력이 막힌 메뉴나 클리어 화면에서 실행 가능한 안내처럼 보이지 않도록 함
	bool CanShowInteractionPrompts() const;

#pragma endregion

#pragma region ClearSummary

public:
	// 클리어 연출이 결과 화면에 넘길 값을 기록 - 기록한 스테이지(맵)도 함께 저장
	// HUD 위젯을 찾아 읽던 결합을 없애려는 것이며, 별 개수는 0~3 으로 보정
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Clear")
	void SetClearSummary(float ElapsedSeconds, int32 StarCount);

	// 현재 스테이지에서 기록된 값이 있으면 true - 없거나 다른 스테이지의 값이면 false 이며 출력값은 0
	// 서브시스템은 맵 이동을 넘어 살아 있으므로 이전 스테이지의 값이 남아 있을 수 있어 맵 이름을 대조함
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Clear")
	bool TryGetClearSummary(float& OutElapsedSeconds, int32& OutStarCount) const;

	// 기록을 지움 - HUD 가 만들어질 때 새 판의 시작으로 호출
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Clear")
	void ClearClearSummary();

#pragma endregion

#pragma region Feedback

public:
	// 상호작용을 차단하는 팝업을 Root의 Popup 레이어에 표시
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Feedback")
	bool OpenPopup(UUserWidget* InWidget);

	// 현재 표시 중인 팝업을 닫고 이전 입력 모드로 복귀
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Feedback")
	bool ClosePopup(UUserWidget* InWidget);

	// 확인·취소 팝업을 만들어 표시하고 그 팝업을 반환. 열지 못하면 null 이며 콜백은 부르지 않음
	// ESC 는 취소와 같고, 팝업이 결과 없이 사라지면(맵 이동·소유자 파기) 어느 콜백도 부르지 않음
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Feedback")
	UCWBaseConfirmDialog* ShowConfirmPopup(
		TSubclassOf<UCWBaseConfirmDialog> DialogClass,
		FText Message,
		FCWConfirmResultSignature OnConfirmed,
		FCWConfirmResultSignature OnCancelled
	);

	// 확인 버튼만 있는 안내 팝업. 확인을 누르면 닫히고 콜백은 없음
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Feedback")
	UCWBaseConfirmDialog* ShowNoticePopup(TSubclassOf<UCWBaseConfirmDialog> DialogClass, FText Message);

	// 비차단 안내 문구를 토스트 큐에 추가
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Feedback")
	void ShowToast(const FText& Message);

	// 문자열을 시스템 클립보드에 복사
	// 엔진이 클립보드를 BP 로 노출하지 않아(ClipboardCopy 는 UFUNCTION 아님) 여기서 감쌈
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Feedback")
	void CopyTextToClipboard(const FString& Text);

#pragma endregion

#pragma region PageInternals

private:
	// 현재 페이지를 닫고 참조를 정리
	UFUNCTION(BlueprintCallable, Category = "CW|UI")
	void CloseCurrentPage();

	// 화면의 페이지 전환 요청을 처리
	UFUNCTION()
	void HandleRequestOpenPage(ECWUIPage TargetPage);

	// 공개 작업의 시작~끝을 묶어 끝난 시점의 확정 페이지만 OnPageChanged 로 통보
	// 열기 중 CloseCurrentPage 가 만드는 중간 None 이 구독자에게 보이지 않게 하고,
	// 중첩 호출(OpenPage 안의 EnsureUIRoot)은 가장 바깥 작업이 한 번만 통보함
	struct FPageChangeScope
	{
		explicit FPageChangeScope(UCWUISubsystem& InOwner);
		~FPageChangeScope();

		UCWUISubsystem& Owner;
	};

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

#pragma region FeedbackInternals

private:
	// 확인 팝업을 만들고 열기까지의 공통 경로
	UCWBaseConfirmDialog* OpenConfirmDialog(
		TSubclassOf<UCWBaseConfirmDialog> DialogClass,
		const FText& Message,
		bool bShowCancel,
		FCWConfirmResultSignature OnConfirmed,
		FCWConfirmResultSignature OnCancelled
	);

	// 대기 중인 다음 토스트를 생성하고 표시
	void ShowNextToast();

	// 현재 토스트를 제거하고 다음 요청을 처리
	void HandleToastFinished();

	// 현재 토스트와 대기 요청 및 타이머 상태를 모두 정리
	void ResetToastState();

#pragma endregion

#pragma region FocusTarget

private:
	// 지금 키보드 포커스를 가져야 할 위젯. 없으면 nullptr
	UUserWidget* GetFocusTargetWidget() const;

#pragma endregion

#pragma region Variables

private:
	// Global Viewmodel Collection에 등록하거나 조회한 Session ViewModel
	UPROPERTY(Transient)
	TWeakObjectPtr<UCWSessionViewModel> SessionViewModel;

	// 이 서브시스템이 전역 Session ViewModel을 직접 등록했는지 구분
	bool bOwnsSessionViewModelRegistration = false;

	// 현재 사용할 Root Widget 클래스
	UPROPERTY()
	TSubclassOf<UCWUIRootWidget> UIRootClass;

	// 병합된 페이지 정책. 맵 이동 시 이전 것을 유지하며 같은 키만 덮어씀
	UPROPERTY()
	TMap<ECWUIPage, FCWUIPageConfig> PageConfigs;

	UPROPERTY(Transient)
	TWeakObjectPtr<UCWPauseMenuWidget> PauseMenu;

	// 클리어 연출 등으로 일시정지를 막아 둔 상태인지
	bool bPauseMenuBlocked = false;

	// 클리어 요약 - 유효 여부·기록한 맵·값
	bool bClearSummaryValid = false;
	FString ClearSummaryMap;
	float ClearSummaryElapsedSeconds = 0.f;
	int32 ClearSummaryStarCount = 0;

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

	// 현재 페이지를 열기 직전에 떠 있던 페이지
	// 닫은 뒤에도 값을 남겨 둠 - 복귀 지점을 고르는 쪽이 ClosePage 뒤에 읽기 때문임
	ECWUIPage PageBeforeCurrentPage = ECWUIPage::None;

	// 페이지 통보 범위의 중첩 깊이와 가장 바깥 작업이 시작될 때의 페이지
	int32 PageChangeDepth = 0;
	ECWUIPage PageAtOperationStart = ECWUIPage::None;

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

#pragma region Event

public:
	// 페이지가 바뀐 뒤 1회 호출 (구독 직후 현재 값은 GetCurrentPage 로 직접 읽을 것)
	UPROPERTY(BlueprintAssignable, Category = "CW|UI")
	FCWPageChangedSignature OnPageChanged;

#pragma endregion
};
