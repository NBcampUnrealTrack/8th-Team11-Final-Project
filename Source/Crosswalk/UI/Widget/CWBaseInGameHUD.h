// CWBaseInGameHUD.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TimerManager.h"
#include "CWBaseInGameHUD.generated.h"

class ACWTrafficDirector;
class UCWUISubsystem;

// 클리어 연출 중 BP 가 반영할 표시 상태 - 캔버스·별 배치는 에디터에 두고 상태만 알림
UENUM(BlueprintType)
enum class ECWClearVisual : uint8
{
	// 연출 캔버스를 숨김
	Hidden,
	// 연출 시작 직전 - 캔버스를 숨기고 별을 투명하게 되돌림
	Preparing,
	// 연출 캔버스를 보임 (입력은 통과시킴)
	Showing
};

// 신호 대기 판정의 시간 규칙 - 월드 없이 시험할 수 있도록 순수 함수로 분리
struct CROSSWALK_API FCWClearSignalTiming
{
	// 신호를 확인하기 시작해도 되는 시점인지
	static bool IsLeadInElapsed(float ElapsedSeconds, float LeadInSeconds);

	// 신호가 준비되지 않아도 연출을 진행할 시점인지 - 하한 0.5초로 설정 실수에도 즉시 통과하지 않게 함
	static bool IsTimedOut(float ElapsedSeconds, float TimeoutSeconds);

	// 결과 화면에 표시할 소요 시간 - 클리어 시작 시각에서 스테이지 시작 시각을 뺌
	// 월드 시간은 맵 이동에도 이어져 그대로 쓰면 스테이지를 거칠 때마다 누적되므로 시작 시각이 없으면(음수) 이전 값 그대로 돌려줌
	static float StageElapsedSeconds(float ClearStartedAt, float StageStartedAt);
};

// 인게임 HUD 의 공통 부모
// 스테이지 클리어 연출(신호 대기 → 결과 화면 열기)의 단계·타이머·입력 잠금·취소를 단독으로 소유
// 자식 BP 는 MVVM 목적지 함수에서 SetStageCleared 만 부르고, 표시와 신호 판정은 아래 이벤트로 맡음
UCLASS(Abstract)
class CROSSWALK_API UCWBaseInGameHUD : public UUserWidget
{
	GENERATED_BODY()

public:
	// 스테이지 클리어 여부 반영 - true 면 연출 시작, false 면 취소
	UFUNCTION(BlueprintCallable, Category = "CW|UI|HUD|Clear")
	void SetStageCleared(bool bCleared);

	// 클리어 연출을 시작한 뒤 흐른 시간(초) - 신호 판정 BP 가 리드인 이후인지 확인하는 데 씀
	// 진행 중이 아니면 0
	UFUNCTION(BlueprintPure, Category = "CW|UI|HUD|Clear")
	float GetClearElapsedSeconds() const;

protected:
	// 상태 초기화를 먼저 끝내야 Super 가 부르는 BP Construct 와 MVVM 초기 동기화가 깨끗한 상태에서 시작함
	virtual void NativeConstruct() override;

	// 타이머와 입력 잠금을 BP Destruct 보다 먼저 정리
	virtual void NativeDestruct() override;

	// 교통 신호가 클리어 연출을 진행해도 될 상태인지 - 규칙을 읽는 부분이라 BP 가 기존 판정 함수를 호출해 구현
	// 구현이 없으면 경고 후 미준비로 보고, 기존 타임아웃 뒤에 진행
	UFUNCTION(BlueprintNativeEvent, Category = "CW|UI|HUD|Clear")
	bool EvaluateDirectorClearSignal(ACWTrafficDirector* Director);

	// 연출 표시 상태가 바뀜 - 위젯 배치는 BP 에 있으므로 상태만 알림
	UFUNCTION(BlueprintImplementableEvent, Category = "CW|UI|HUD|Clear")
	void OnClearVisualChanged(ECWClearVisual Visual);

	// 연출·결과 화면 중 설정 버튼을 잠가야 하는지가 바뀜
	UFUNCTION(BlueprintImplementableEvent, Category = "CW|UI|HUD|Clear")
	void OnSettingsButtonLockChanged(bool bLocked);

private:
	enum class EClearPhase : uint8
	{
		Idle,
		WaitingSignal,
		OpeningResult
	};

	void BeginClear();
	void CancelClear(const TCHAR* Reason);
	void TickClear();
	void WaitForSignals();
	bool AreSignalsReady();
	void OpenResult();
	void LockInput(UCWUISubsystem* UI);
	void ReleaseInput(UCWUISubsystem* UI);
	void RefreshSettingsButtonState(UCWUISubsystem* UI);
	UCWUISubsystem* GetUISubsystem() const;

public:
	// 신호 확인을 시작하기까지의 대기 시간(초)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|HUD|Clear", meta = (ClampMin = "0"))
	float SignalLeadInSeconds = 0.6f;

	// 신호가 준비되지 않아도 연출을 진행하는 한계 시간(초)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|HUD|Clear", meta = (ClampMin = "0"))
	float SignalTimeoutSeconds = 10.f;

	// 결과 화면에 넘기는 임시 별 개수 - 별점 규칙이 정해지면 대체
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|HUD|Clear", meta = (ClampMin = "0", ClampMax = "3"))
	int32 PreviewStarCount = 3;

private:
	EClearPhase ClearPhase = EClearPhase::Idle;
	bool bClearActive = false;
	bool bClearResultOpened = false;
	bool bClearInputLocked = false;
	bool bStageCleared = false;

	// 신호 판정 미구현 경고를 연출 1회당 한 번만 남기기 위함
	bool bWarnedMissingSignalEvaluation = false;

	// 클리어 시작 시점의 게임 시간(초)
	float ClearStartedAt = 0.f;

	// 이 HUD 가 처음 만들어진 시점의 게임 시간(초) - 스테이지 시작으로 보고 소요 시간의 기준으로 씀, 음수는 아직 기록 전
	float StageStartedAt = -1.f;

	// 맵 이동 중 파기될 수 있어 약참조로 보관
	TArray<TWeakObjectPtr<ACWTrafficDirector>> ClearSignalDirectors;

	FTimerHandle ClearTimer;
};
