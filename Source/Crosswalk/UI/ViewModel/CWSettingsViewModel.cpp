// CWSettingsViewModel.cpp

#include "UI/ViewModel/CWSettingsViewModel.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
	// 비디오 모드 변경 확인 제한시간
	constexpr float VideoModeConfirmationDuration = 10.0f;
}

#pragma region ViewModelLifecycle

void UCWSettingsViewModel::Initialize(UObject* WorldContextObject)
{
	// 재초기화 시 이전 상태가 남지 않도록 먼저 정리
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

	// 비디오 모드 확인 타이머에 사용할 현재 World를 보관
	BoundWorld = World;

	bIsInitialized = true;
}

void UCWSettingsViewModel::Uninitialize()
{
	// 화면이 사라질 때 미확정 비디오 모드를 변경 전 값으로 복구
	FinishPendingVideoMode(false, false);

	// pending 상태가 아니어도 남아 있을 수 있는 타이머를 안전하게 정리
	ClearVideoModeConfirmationTimer();

	// 예외 경로에서 남을 수 있는 비디오 모드 복구값까지 정리
	ClearVideoModeSnapshot();

	bIsInitialized = false;

	// 타이머와 복구값을 정리한 뒤 World 참조를 해제
	BoundWorld.Reset();
}

#pragma endregion

#pragma region SettingsData

void UCWSettingsViewModel::RefreshSettingsData()
{
	UGameUserSettings* GameUserSettings = GetGameUserSettings();

	if (!IsValid(GameUserSettings))
	{
		return;
	}

	// 지원 해상도 목록을 새로 조회 (VM이 단독 소유)
	TArray<FIntPoint> NewSupportedResolutions;
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(
		NewSupportedResolutions
	);

	// 현재 해상도가 목록에 없으면 추가해 선택 가능하게 함
	const FIntPoint CurrentResolution =
		GameUserSettings->GetScreenResolution();

	int32 NewSelectedResolutionIndex =
		NewSupportedResolutions.IndexOfByKey(CurrentResolution);

	if (NewSelectedResolutionIndex == INDEX_NONE)
	{
		NewSupportedResolutions.Add(CurrentResolution);
		NewSelectedResolutionIndex = NewSupportedResolutions.Num() - 1;
	}

	SupportedResolutions = NewSupportedResolutions;

	// 콤보박스에 표시할 문자열 목록 구성
	TArray<FString> NewResolutionOptions;
	NewResolutionOptions.Reserve(SupportedResolutions.Num());

	for (const FIntPoint& Resolution : SupportedResolutions)
	{
		NewResolutionOptions.Add(
			FString::Printf(
				TEXT("%d x %d"),
				Resolution.X,
				Resolution.Y
			)
		);
	}

	SetResolutionOptions(NewResolutionOptions);
	SetSelectedResolutionIndex(NewSelectedResolutionIndex);

	// 창 모드 (0 Fullscreen / 1 WindowedFullscreen / 2 Windowed)
	SetSelectedWindowModeIndex(
		FMath::Clamp(
			static_cast<int32>(GameUserSettings->GetFullscreenMode()),
			0,
			2
		)
	);

	// 그래픽 품질 (4옵션, 콤보 인덱스 = 엔진 ScalabilityLevel 0~3)
	const int32 ScalabilityLevel =
		GameUserSettings->GetOverallScalabilityLevel();

	int32 NewSelectedQualityIndex;

	if (ScalabilityLevel >= 0 && ScalabilityLevel <= 3)
	{
		NewSelectedQualityIndex = ScalabilityLevel;
	}
	else if (ScalabilityLevel == 4)
	{
		// Cinematic(4)은 최고 옵션(매우높음)으로 맞춤
		NewSelectedQualityIndex = 3;
	}
	else
	{
		// 사용자 지정(-1) 등은 중간으로 표시
		NewSelectedQualityIndex = 1;
	}

	SetSelectedQualityIndex(NewSelectedQualityIndex);
}

bool UCWSettingsViewModel::RequestSetResolutionByIndex(int32 Index)
{
	if (!bIsInitialized || bIsVideoModeConfirmationPending)
	{
		return false;
	}

	UGameUserSettings* GameUserSettings = GetGameUserSettings();

	if (!IsValid(GameUserSettings) ||
		!SupportedResolutions.IsValidIndex(Index))
	{
		return false;
	}

	const FIntPoint NewResolution = SupportedResolutions[Index];

	// 같은 값은 적용하지 않고 정상 처리
	if (GameUserSettings->GetScreenResolution() == NewResolution)
	{
		SetSelectedResolutionIndex(Index);
		return true;
	}

	// 엔진의 LastConfirmed는 창 크기 변경 중 자동 갱신될 수 있으므로
	// VM이 변경 전 비디오 모드를 별도로 보관한다
	CaptureVideoModeSnapshot(GameUserSettings);

	GameUserSettings->SetScreenResolution(NewResolution);

	bIsVideoModeConfirmationPending = true;

	// 확인 전에는 화면에만 임시 적용하고 저장하지 않음
	GameUserSettings->ApplyResolutionSettings(false);

	SetSelectedResolutionIndex(Index);

	return true;
}

bool UCWSettingsViewModel::RequestSetWindowModeByIndex(int32 Index)
{
	if (!bIsInitialized || bIsVideoModeConfirmationPending)
	{
		return false;
	}

	UGameUserSettings* GameUserSettings = GetGameUserSettings();

	if (!IsValid(GameUserSettings) || Index < 0 || Index > 2)
	{
		return false;
	}

	const EWindowMode::Type NewWindowMode =
		static_cast<EWindowMode::Type>(Index);

	// 같은 값은 적용하지 않고 정상 처리
	if (GameUserSettings->GetFullscreenMode() == NewWindowMode)
	{
		SetSelectedWindowModeIndex(Index);
		return true;
	}

	// 엔진의 LastConfirmed는 창 크기 변경 중 자동 갱신될 수 있으므로
	// VM이 변경 전 비디오 모드를 별도로 보관한다
	CaptureVideoModeSnapshot(GameUserSettings);

	GameUserSettings->SetFullscreenMode(NewWindowMode);

	bIsVideoModeConfirmationPending = true;

	// 확인 전에는 화면에만 임시 적용하고 저장하지 않음
	GameUserSettings->ApplyResolutionSettings(false);

	SetSelectedWindowModeIndex(Index);

	// 창 모드 변경 뒤 해상도 목록 재조회는 WBP가 다음 Tick에 처리
	return true;
}

bool UCWSettingsViewModel::RequestSetQualityByIndex(int32 Index)
{
	if (!bIsInitialized || bIsVideoModeConfirmationPending)
	{
		return false;
	}

	UGameUserSettings* GameUserSettings = GetGameUserSettings();

	if (!IsValid(GameUserSettings) || Index < 0 || Index > 3)
	{
		return false;
	}

	// 콤보 인덱스 0~3이 엔진 ScalabilityLevel과 직결
	if (GameUserSettings->GetOverallScalabilityLevel() != Index)
	{
		GameUserSettings->SetOverallScalabilityLevel(Index);
		ApplyQualityAndSave(GameUserSettings);
	}

	SetSelectedQualityIndex(Index);

	return true;
}

#pragma endregion

#pragma region VideoModeConfirmation

void UCWSettingsViewModel::StartVideoModeConfirmation()
{
	if (!bIsInitialized || !bIsVideoModeConfirmationPending)
	{
		return;
	}

	UWorld* World = BoundWorld.Get();

	if (!IsValid(World))
	{
		// 타이머를 시작할 수 없으면 미확정 설정을 즉시 복구
		FinishPendingVideoMode(false, true);
		return;
	}

	// 중복 타이머를 방지하고 확인 제한시간을 새로 시작
	ClearVideoModeConfirmationTimer();

	World->GetTimerManager().SetTimer(
		VideoModeConfirmationTimerHandle,
		this,
		&UCWSettingsViewModel::HandleVideoModeConfirmationExpired,
		VideoModeConfirmationDuration,
		false
	);
}

void UCWSettingsViewModel::ConfirmPendingVideoMode()
{
	FinishPendingVideoMode(true, false);
}

void UCWSettingsViewModel::CancelPendingVideoMode()
{
	FinishPendingVideoMode(false, false);
}

void UCWSettingsViewModel::FinishPendingVideoMode(
	bool bConfirm,
	bool bNotifyExpiration
)
{
	// 이미 처리된 늦은 콜백은 무시
	if (!bIsVideoModeConfirmationPending)
	{
		return;
	}

	ClearVideoModeConfirmationTimer();

	bIsVideoModeConfirmationPending = false;

	UGameUserSettings* GameUserSettings = GetGameUserSettings();

	if (IsValid(GameUserSettings))
	{
		if (bConfirm)
		{
			// 현재 비디오 모드를 확정하고 영구 저장
			GameUserSettings->ConfirmVideoMode();
			GameUserSettings->SaveSettings();
		}
		else if (bHasVideoModeSnapshot)
		{
			// 엔진의 LastConfirmed 값 대신 VM이 저장한 변경 전 값으로 복구
			GameUserSettings->SetScreenResolution(
				ResolutionBeforePending
			);

			GameUserSettings->SetFullscreenMode(
				static_cast<EWindowMode::Type>(
					WindowModeBeforePending
					)
			);

			GameUserSettings->ApplyResolutionSettings(false);

			// 창 크기 변경 과정에서 자동 저장됐을 가능성까지 덮어쓰기 위해
			// 복구된 값을 최종 확정하고 설정 파일에 저장
			GameUserSettings->ConfirmVideoMode();
			GameUserSettings->SaveSettings();
		}
		else
		{
			// 스냅샷이 없는 예외 상황에서는 엔진 기본 복구 사용
			GameUserSettings->RevertVideoMode();
			GameUserSettings->ApplyResolutionSettings(false);
		}
	}

	ClearVideoModeSnapshot();

	// 타임아웃에서만 WBP에 팝업 종료를 요청
	if (bNotifyExpiration)
	{
		OnVideoModeConfirmationExpired.Broadcast();
	}
}

void UCWSettingsViewModel::HandleVideoModeConfirmationExpired()
{
	FinishPendingVideoMode(false, true);
}

void UCWSettingsViewModel::ClearVideoModeConfirmationTimer()
{
	if (UWorld* World = BoundWorld.Get())
	{
		World->GetTimerManager().ClearTimer(
			VideoModeConfirmationTimerHandle
		);
	}

	VideoModeConfirmationTimerHandle.Invalidate();
}

#pragma endregion

#pragma region StateSetters

void UCWSettingsViewModel::SetResolutionOptions(
	const TArray<FString>& InOptions
)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		ResolutionOptions,
		InOptions
	);
}

void UCWSettingsViewModel::SetSelectedResolutionIndex(int32 InIndex)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		SelectedResolutionIndex,
		InIndex
	);
}

void UCWSettingsViewModel::SetSelectedWindowModeIndex(int32 InIndex)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		SelectedWindowModeIndex,
		InIndex
	);
}

void UCWSettingsViewModel::SetSelectedQualityIndex(int32 InIndex)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		SelectedQualityIndex,
		InIndex
	);
}

#pragma endregion

#pragma region SettingsHelpers

UGameUserSettings* UCWSettingsViewModel::GetGameUserSettings() const
{
	return GEngine ? GEngine->GetGameUserSettings() : nullptr;
}

void UCWSettingsViewModel::ApplyQualityAndSave(UGameUserSettings* GameUserSettings) const
{
	// 품질만 적용해 해상도 적용과 분리한 뒤 저장
	GameUserSettings->ApplyNonResolutionSettings();
	GameUserSettings->SaveSettings();
}

void UCWSettingsViewModel::CaptureVideoModeSnapshot(
	UGameUserSettings* GameUserSettings
)
{
	if (!IsValid(GameUserSettings))
	{
		return;
	}

	ResolutionBeforePending =
		GameUserSettings->GetScreenResolution();

	WindowModeBeforePending =
		static_cast<int32>(
			GameUserSettings->GetFullscreenMode()
			);

	bHasVideoModeSnapshot = true;
}

void UCWSettingsViewModel::ClearVideoModeSnapshot()
{
	ResolutionBeforePending = FIntPoint::ZeroValue;
	WindowModeBeforePending = 0;
	bHasVideoModeSnapshot = false;
}

#pragma endregion
