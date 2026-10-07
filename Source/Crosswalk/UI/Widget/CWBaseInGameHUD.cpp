// CWBaseInGameHUD.cpp

#include "UI/Widget/CWBaseInGameHUD.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Traffic/CWTrafficDirector.h"
#include "UI/CWUILog.h"
#include "UI/Core/CWUISubsystem.h"

namespace
{
	// BP 타이머와 같은 주기 - 위젯 Tick 은 Collapsed 일 때 멈추므로 월드 타이머를 유지
	constexpr float ClearTickInterval = 0.033f;
	constexpr float MinSignalTimeoutSeconds = 0.5f;
}

bool FCWClearSignalTiming::IsLeadInElapsed(float ElapsedSeconds, float LeadInSeconds)
{
	return ElapsedSeconds >= LeadInSeconds;
}

bool FCWClearSignalTiming::IsTimedOut(float ElapsedSeconds, float TimeoutSeconds)
{
	return ElapsedSeconds >= FMath::Max(TimeoutSeconds, MinSignalTimeoutSeconds);
}

void UCWBaseInGameHUD::NativeConstruct()
{
	// Super 앞에서 끝냄 - BP Construct 의 뷰모델 초기화가 즉시 SetStageCleared 를 부를 수 있음
	bClearResultOpened = false;
	CancelClear(TEXT("construct"));
	if (UCWUISubsystem* UI = GetUISubsystem(); IsValid(UI))
	{
		UI->ClearClearSummary();
	}

	Super::NativeConstruct();
}

void UCWBaseInGameHUD::NativeDestruct()
{
	CancelClear(TEXT("destruct"));
	Super::NativeDestruct();
}

bool UCWBaseInGameHUD::EvaluateDirectorClearSignal_Implementation(ACWTrafficDirector* Director)
{
	// 판정이 연결되지 않으면 신호를 확인하지 않고 결과로 넘어가지 않도록 미준비로 보고 타임아웃에 맡김
	if (!bWarnedMissingSignalEvaluation)
	{
		bWarnedMissingSignalEvaluation = true;
		UE_LOG(LogCWUI, Warning, TEXT("[ClearPresentation] EvaluateDirectorClearSignal is not implemented; treating signals as not ready until timeout."));
	}
	return false;
}

void UCWBaseInGameHUD::SetStageCleared(bool bCleared)
{
	bStageCleared = bCleared;
	if (bCleared)
	{
		BeginClear();
	}
	else
	{
		CancelClear(TEXT("stage not cleared"));
	}
}

float UCWBaseInGameHUD::GetClearElapsedSeconds() const
{
	const UWorld* World = GetWorld();
	return (bClearActive && World) ? World->GetTimeSeconds() - ClearStartedAt : 0.f;
}

void UCWBaseInGameHUD::BeginClear()
{
	if (bClearActive || bClearResultOpened)
	{
		return;
	}

	UCWUISubsystem* UI = GetUISubsystem();
	UWorld* World = GetWorld();
	if (!IsValid(UI) || !World)
	{
		UE_LOG(LogCWUI, Warning, TEXT("[ClearPresentation] begin refused. UI subsystem or world is invalid."));
		return;
	}

	// 일시정지 메뉴와 입장 안내 등 연출을 가리는 것을 먼저 닫음
	UI->DismissOverlaysForClear();

	bClearActive = true;
	ClearPhase = EClearPhase::WaitingSignal;
	bWarnedMissingSignalEvaluation = false;
	ClearStartedAt = World->GetTimeSeconds();

	TArray<AActor*> Directors;
	UGameplayStatics::GetAllActorsOfClass(this, ACWTrafficDirector::StaticClass(), Directors);
	ClearSignalDirectors.Reset();
	for (AActor* Actor : Directors)
	{
		ClearSignalDirectors.Add(Cast<ACWTrafficDirector>(Actor));
	}

	OnClearVisualChanged(ECWClearVisual::Preparing);
	World->GetTimerManager().SetTimer(
		ClearTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]() { TickClear(); }),
		ClearTickInterval,
		true
	);
	RefreshSettingsButtonState(UI);

	UE_LOG(LogCWUI, Log, TEXT("[ClearPresentation] begin. Directors=%d"), ClearSignalDirectors.Num());
}

void UCWBaseInGameHUD::CancelClear(const TCHAR* Reason)
{
	const bool bWasActive = bClearActive;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ClearTimer);
	}
	bClearActive = false;
	ClearPhase = EClearPhase::Idle;
	OnClearVisualChanged(ECWClearVisual::Hidden);

	UCWUISubsystem* UI = GetUISubsystem();
	ReleaseInput(UI);
	RefreshSettingsButtonState(UI);

	// 정상 생성·파기처럼 진행 중이 아니었던 호출은 로그에서 제외해 클리어 1회에 한 세트만 남김
	if (bWasActive)
	{
		UE_LOG(LogCWUI, Log, TEXT("[ClearPresentation] cancel. Reason=%s"), Reason);
	}
}

void UCWBaseInGameHUD::TickClear()
{
	if (!bClearActive)
	{
		return;
	}

	// 서브시스템·부모·클리어 상태 검사를 한 곳에서 - 하나라도 어긋나면 연출을 취소
	UCWUISubsystem* UI = GetUISubsystem();
	if (!IsValid(UI) || GetParent() == nullptr || !bStageCleared)
	{
		CancelClear(TEXT("invalid state"));
		return;
	}

	switch (UI->GetCurrentPage())
	{
	case ECWUIPage::None:
		LockInput(UI);
		if (ClearPhase == EClearPhase::WaitingSignal)
		{
			WaitForSignals();
		}
		else if (ClearPhase == EClearPhase::OpeningResult)
		{
			OpenResult();
		}
		break;

	case ECWUIPage::Settings:
		// 설정 화면이 연출을 가리므로 닫고 입력을 다시 잠금
		bClearInputLocked = false;
		UI->ClosePage();
		LockInput(UI);
		break;

	default:
		CancelClear(TEXT("page changed"));
		break;
	}
}

void UCWBaseInGameHUD::WaitForSignals()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Elapsed = World->GetTimeSeconds() - ClearStartedAt;
	if (!FCWClearSignalTiming::IsLeadInElapsed(Elapsed, SignalLeadInSeconds))
	{
		return;
	}

	if (AreSignalsReady())
	{
		UE_LOG(LogCWUI, Log, TEXT("[ClearPresentation] signal ready. Elapsed=%.2f"), Elapsed);
	}
	else if (FCWClearSignalTiming::IsTimedOut(Elapsed, SignalTimeoutSeconds))
	{
		UE_LOG(LogCWUI, Warning, TEXT("[ClearPresentation] signal wait timed out. Elapsed=%.2f"), Elapsed);
	}
	else
	{
		return;
	}

	ClearPhase = EClearPhase::OpeningResult;
	OnClearVisualChanged(ECWClearVisual::Showing);
	OpenResult();
}

bool UCWBaseInGameHUD::AreSignalsReady()
{
	bool bAllReady = true;
	for (const TWeakObjectPtr<ACWTrafficDirector>& Director : ClearSignalDirectors)
	{
		// 맵 이동 등으로 사라진 디렉터는 기다릴 대상이 아님
		if (Director.IsValid() && !EvaluateDirectorClearSignal(Director.Get()))
		{
			bAllReady = false;
		}
	}
	return bAllReady;
}

void UCWBaseInGameHUD::OpenResult()
{
	UCWUISubsystem* UI = GetUISubsystem();
	if (!IsValid(UI))
	{
		CancelClear(TEXT("ui subsystem lost"));
		return;
	}

	// 결과 화면이 열리는 순간 읽으므로 반드시 열기 전에 기록
	UI->SetClearSummary(ClearStartedAt, PreviewStarCount);
	if (UI->OpenPage(ECWUIPage::Result))
	{
		bClearResultOpened = true;
		UE_LOG(LogCWUI, Log, TEXT("[ClearPresentation] result opened."));
		CancelClear(TEXT("result opened"));
		SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		UE_LOG(LogCWUI, Error, TEXT("[ClearPresentation] could not open Result page."));
		CancelClear(TEXT("result open failed"));
	}
}

void UCWBaseInGameHUD::LockInput(UCWUISubsystem* UI)
{
	if (bClearInputLocked || !IsValid(UI) || !GetOwningPlayer())
	{
		return;
	}

	if (UI->GetCurrentPage() == ECWUIPage::None)
	{
		UI->ApplyInputMode(ECWUIInputMode::UIOnly);
		bClearInputLocked = true;
	}
}

void UCWBaseInGameHUD::ReleaseInput(UCWUISubsystem* UI)
{
	if (!bClearInputLocked)
	{
		return;
	}

	bClearInputLocked = false;
	// 위젯이 이미 떼어졌거나 다른 페이지가 입력을 잡고 있으면 복귀시키지 않음
	if (GetParent() != nullptr && IsValid(UI) && GetOwningPlayer() && UI->GetCurrentPage() == ECWUIPage::None)
	{
		UI->ApplyInputMode(ECWUIInputMode::GameAndUI);
	}
}

void UCWBaseInGameHUD::RefreshSettingsButtonState(UCWUISubsystem* UI)
{
	const bool bLocked = bClearActive || bClearResultOpened;
	OnSettingsButtonLockChanged(bLocked);
	if (IsValid(UI))
	{
		UI->SetPauseMenuBlocked(bLocked);
	}
}

UCWUISubsystem* UCWBaseInGameHUD::GetUISubsystem() const
{
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetSubsystem<UCWUISubsystem>() : nullptr;
}
