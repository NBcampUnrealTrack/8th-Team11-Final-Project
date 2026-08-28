// CWSettingsViewModel.cpp

#include "UI/ViewModel/CWSettingsViewModel.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"

#pragma region ViewModelLifecycle

void UCWSettingsViewModel::Initialize(UObject* WorldContextObject)
{
	// 재초기화 시 이전 상태가 남지 않도록 먼저 정리 (중복 초기화 안전)
	Uninitialize();

	if (!IsValid(WorldContextObject))
	{
		return;
	}

	// 10초 확인 타이머(5-3)에 쓸 World만 확보. 없어도 5-2 동작에는 지장 없음
	BoundWorld = WorldContextObject->GetWorld();

	bIsInitialized = true;
}

void UCWSettingsViewModel::Uninitialize()
{
	// 5-3에서 pending 비디오 모드 복구·타이머 정리를 여기 추가 (World Reset보다 먼저)

	bIsInitialized = false;

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
	if (!bIsInitialized)
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

	// 이미 같은 해상도면 불필요한 적용은 생략하고 선택값만 맞춘다
	if (GameUserSettings->GetScreenResolution() != NewResolution)
	{
		GameUserSettings->SetScreenResolution(NewResolution);
		ApplyAndSave(GameUserSettings);
	}

	SetSelectedResolutionIndex(Index);

	return true;
}

bool UCWSettingsViewModel::RequestSetWindowModeByIndex(int32 Index)
{
	if (!bIsInitialized)
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

	if (GameUserSettings->GetFullscreenMode() != NewWindowMode)
	{
		GameUserSettings->SetFullscreenMode(NewWindowMode);
		ApplyAndSave(GameUserSettings);
	}

	SetSelectedWindowModeIndex(Index);

	// 창 모드 변경 뒤 해상도 목록 재조회는 WBP가 다음 Tick에 RefreshSettingsData로 처리
	return true;
}

bool UCWSettingsViewModel::RequestSetQualityByIndex(int32 Index)
{
	if (!bIsInitialized)
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

void UCWSettingsViewModel::ApplyAndSave(
	UGameUserSettings* GameUserSettings
) const
{
	// ApplySettings 내부에서 해상도·창 모드를 적용하고 설정까지 저장한다
	GameUserSettings->ApplySettings(false);
}

void UCWSettingsViewModel::ApplyQualityAndSave(
	UGameUserSettings* GameUserSettings
) const
{
	// 품질만 적용해 해상도 적용과 분리한 뒤 저장
	GameUserSettings->ApplyNonResolutionSettings();
	GameUserSettings->SaveSettings();
}

#pragma endregion
