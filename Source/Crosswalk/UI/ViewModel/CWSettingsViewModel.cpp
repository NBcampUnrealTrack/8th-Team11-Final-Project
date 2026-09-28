// CWSettingsViewModel.cpp

#include "UI/ViewModel/CWSettingsViewModel.h"
#include "Core/CWGameUserSettings.h"
#include "Audio/CWAudioSubsystem.h"
#include "Engine/GameInstance.h"
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
	// 슬라이더 놓기 이벤트를 놓치고 화면이 닫히는 경로를 대비한 백업 저장
	SaveVolumeSettingsIfDirty();

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

	// 그래픽 품질 (0~3 = 엔진 ScalabilityLevel). 복구 후 콤보를 되돌리려면 여기서 다시 읽어야 함
	SetSelectedQualityIndex(
		FMath::Clamp(
			GameUserSettings->GetOverallScalabilityLevel(),
			0,
			3
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

	// 저장된 채널 볼륨을 슬라이더 초기값으로 반영
	// ini 미등록이면 nullptr 이므로 볼륨만 건너뛰고 나머지 설정은 정상 표시
	if (UCWGameUserSettings* CWGameUserSettings =
		UCWGameUserSettings::GetCWGameUserSettings())
	{
		SetMasterVolume(CWGameUserSettings->GetMasterVolume());
		SetBGMVolume(CWGameUserSettings->GetBGMVolume());
		SetSFXVolume(CWGameUserSettings->GetSFXVolume());
	}
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
	// 같은 값은 적용하지 않고 정상 처리
	if (GameUserSettings->GetOverallScalabilityLevel() == Index)
	{
		SetSelectedQualityIndex(Index);
		return true;
	}

	// 해상도·창 모드와 같은 확인 흐름을 탐. 스냅샷은 셋을 함께 보관
	CaptureVideoModeSnapshot(GameUserSettings);

	GameUserSettings->SetOverallScalabilityLevel(Index);

	bIsVideoModeConfirmationPending = true;

	// 클립보드가 떠 있는 동안 고정된 해상도를 확인 중에만 풀어 고른 화질 그대로 보여줌
	// 확정·되돌리기에서 다시 고정함 (FinishPendingVideoMode)
	if (UCWGameUserSettings* CWGameUserSettings = Cast<UCWGameUserSettings>(GameUserSettings))
	{
		CWGameUserSettings->SetWorldResolutionPreview(true);
	}

	// 확인 전에는 화면에만 임시 적용하고 저장하지 않음
	GameUserSettings->ApplyNonResolutionSettings();

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

	// 표시용 카운트다운. 만료 자체는 위 타이머가 책임짐
	SetVideoModeConfirmationRemainingSeconds(
		FMath::CeilToInt(VideoModeConfirmationDuration)
	);

	World->GetTimerManager().SetTimer(
		VideoModeCountdownTimerHandle,
		this,
		&UCWSettingsViewModel::HandleVideoModeCountdownTick,
		1.0f,
		true
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
			// 현재 비디오 모드와 품질을 확정하고 영구 저장
			GameUserSettings->ConfirmVideoMode();
			GameUserSettings->ApplyNonResolutionSettings();
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

			GameUserSettings->SetOverallScalabilityLevel(
				QualityBeforePending
			);

			GameUserSettings->ApplyResolutionSettings(false);
			GameUserSettings->ApplyNonResolutionSettings();

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

		// 확정·되돌리기로 품질이 정해졌으니 미리보기를 끝내고 클립보드 해상도 고정을 되살림
		if (UCWGameUserSettings* CWGameUserSettings = Cast<UCWGameUserSettings>(GameUserSettings))
		{
			CWGameUserSettings->SetWorldResolutionPreview(false);
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

		World->GetTimerManager().ClearTimer(
			VideoModeCountdownTimerHandle
		);
	}

	VideoModeConfirmationTimerHandle.Invalidate();
	VideoModeCountdownTimerHandle.Invalidate();

	// 대기 상태가 아니면 남은 시간도 없음
	SetVideoModeConfirmationRemainingSeconds(0);
}

void UCWSettingsViewModel::HandleVideoModeCountdownTick()
{
	if (VideoModeConfirmationRemainingSeconds <= 0)
	{
		return;
	}

	SetVideoModeConfirmationRemainingSeconds(
		VideoModeConfirmationRemainingSeconds - 1
	);
}

void UCWSettingsViewModel::SetVideoModeConfirmationRemainingSeconds(int32 InSeconds)
{
	const int32 Clamped = FMath::Max(InSeconds, 0);

	if (UE_MVVM_SET_PROPERTY_VALUE(
		VideoModeConfirmationRemainingSeconds,
		Clamped
	))
	{
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(
			GetVideoModeConfirmationRemainingText
		);
	}
}

FText UCWSettingsViewModel::GetVideoModeConfirmationRemainingText() const
{
	if (VideoModeConfirmationRemainingSeconds <= 0)
	{
		return FText::GetEmpty();
	}

	return FText::Format(
		NSLOCTEXT("CWSettings", "VideoModeRevertCountdown", "설정을 저장하시겠습니까?\n선택하지 않으면 {0}초 후에 설정을 되돌립니다."),
		FText::AsNumber(VideoModeConfirmationRemainingSeconds)
	);
}

#pragma endregion

#pragma region AudioSettings

bool UCWSettingsViewModel::RequestSetMasterVolume(float InVolume)
{
	// 볼륨은 비디오 모드와 무관하므로 확인 대기 중에도 조절 가능
	// 기존 Request 들과 달리 bIsVideoModeConfirmationPending 을 검사하지 않음
	if (!bIsInitialized)
	{
		return false;
	}

	UCWGameUserSettings* GameUserSettings =
		UCWGameUserSettings::GetCWGameUserSettings();

	if (!IsValid(GameUserSettings))
	{
		return false;
	}

	// 저장값이 실제로 바뀐 경우에만 저장 대기 상태로 표시
	if (GameUserSettings->SetMasterVolume(InVolume))
	{
		bHasUnsavedVolumeChanges = true;
	}

	// 표시값은 Clamp 를 거친 저장값을 따라감
	SetMasterVolume(GameUserSettings->GetMasterVolume());

	// 저장값 기준으로 즉시 소리에 반영
	// 서브시스템이 없으면 저장과 표시만 되고 소리는 이전 상태를 유지함
	if (UCWAudioSubsystem* AudioSubsystem = GetAudioSubsystem())
	{
		AudioSubsystem->SetChannelVolume(
			ECWAudioChannel::Master,
			GameUserSettings->GetMasterVolume()
		);
	}

	return true;
}

bool UCWSettingsViewModel::RequestSetBGMVolume(float InVolume)
{
	// 볼륨은 비디오 모드와 무관하므로 확인 대기 중에도 조절 가능
	if (!bIsInitialized)
	{
		return false;
	}

	UCWGameUserSettings* GameUserSettings =
		UCWGameUserSettings::GetCWGameUserSettings();

	if (!IsValid(GameUserSettings))
	{
		return false;
	}

	// 저장값이 실제로 바뀐 경우에만 저장 대기 상태로 표시
	if (GameUserSettings->SetBGMVolume(InVolume))
	{
		bHasUnsavedVolumeChanges = true;
	}

	// 표시값은 Clamp 를 거친 저장값을 따라감
	SetBGMVolume(GameUserSettings->GetBGMVolume());

	// 저장값 기준으로 즉시 소리에 반영
	// 서브시스템이 없으면 저장과 표시만 되고 소리는 이전 상태를 유지함
	if (UCWAudioSubsystem* AudioSubsystem = GetAudioSubsystem())
	{
		AudioSubsystem->SetChannelVolume(
			ECWAudioChannel::BGM,
			GameUserSettings->GetBGMVolume()
		);
	}

	return true;
}

bool UCWSettingsViewModel::RequestSetSFXVolume(float InVolume)
{
	// 볼륨은 비디오 모드와 무관하므로 확인 대기 중에도 조절 가능
	if (!bIsInitialized)
	{
		return false;
	}

	UCWGameUserSettings* GameUserSettings =
		UCWGameUserSettings::GetCWGameUserSettings();

	if (!IsValid(GameUserSettings))
	{
		return false;
	}

	// 저장값이 실제로 바뀐 경우에만 저장 대기 상태로 표시
	if (GameUserSettings->SetSFXVolume(InVolume))
	{
		bHasUnsavedVolumeChanges = true;
	}

	// 표시값은 Clamp 를 거친 저장값을 따라감
	SetSFXVolume(GameUserSettings->GetSFXVolume());

	// 저장값 기준으로 즉시 소리에 반영
	// 서브시스템이 없으면 저장과 표시만 되고 소리는 이전 상태를 유지함
	if (UCWAudioSubsystem* AudioSubsystem = GetAudioSubsystem())
	{
		AudioSubsystem->SetChannelVolume(
			ECWAudioChannel::SFX,
			GameUserSettings->GetSFXVolume()
		);
	}

	return true;
}

void UCWSettingsViewModel::SaveVolumeSettingsIfDirty()
{
	// 변경분이 없으면 화면을 여닫기만 해도 디스크에 쓰이는 것을 막음
	if (!bHasUnsavedVolumeChanges)
	{
		return;
	}

	UCWGameUserSettings* GameUserSettings =
		UCWGameUserSettings::GetCWGameUserSettings();

	if (!IsValid(GameUserSettings))
	{
		return;
	}

	GameUserSettings->SaveSettings();

	bHasUnsavedVolumeChanges = false;
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



void UCWSettingsViewModel::SetMasterVolume(float InVolume)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		MasterVolume,
		InVolume
	);
}

void UCWSettingsViewModel::SetBGMVolume(float InVolume)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		BGMVolume,
		InVolume
	);
}

void UCWSettingsViewModel::SetSFXVolume(float InVolume)
{
	UE_MVVM_SET_PROPERTY_VALUE(
		SFXVolume,
		InVolume
	);
}

#pragma endregion

#pragma region SettingsHelpers

UGameUserSettings* UCWSettingsViewModel::GetGameUserSettings() const
{
	return GEngine ? GEngine->GetGameUserSettings() : nullptr;
}

UCWAudioSubsystem* UCWSettingsViewModel::GetAudioSubsystem() const
{
	// Initialize 에서 보관한 월드를 통해 GameInstance 서브시스템에 접근함
	UWorld* World = BoundWorld.Get();

	if (!IsValid(World))
	{
		return nullptr;
	}

	UGameInstance* GameInstance = World->GetGameInstance();

	if (!IsValid(GameInstance))
	{
		return nullptr;
	}

	return GameInstance->GetSubsystem<UCWAudioSubsystem>();
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

	QualityBeforePending =
		GameUserSettings->GetOverallScalabilityLevel();

	bHasVideoModeSnapshot = true;
}

void UCWSettingsViewModel::ClearVideoModeSnapshot()
{
	ResolutionBeforePending = FIntPoint::ZeroValue;
	WindowModeBeforePending = 0;
	bHasVideoModeSnapshot = false;
}

#pragma endregion
