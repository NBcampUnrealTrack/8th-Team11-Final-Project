// CWGameUserSettings.cpp

#include "Core/CWGameUserSettings.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Scalability.h"

#pragma region GameUserSettingsOverrides

void UCWGameUserSettings::SetToDefaults()
{
	// 해상도·품질 등 부모의 기본값을 먼저 채운 뒤 볼륨을 덮어씀
	// 순서가 바뀌면 부모 구현이 볼륨까지 초기화할 여지가 있음
	Super::SetToDefaults();

	// 설정 초기화 시에도 음소거 상태로 시작하지 않도록 최대값으로 복구
	MasterVolume = 1.0f;
	BGMVolume = 1.0f;
	SFXVolume = 1.0f;

	// 부모 기본값(0 = 무제한) 대신 60으로 시작한다. 그래픽 설정을 적용할 때마다
	// UEngine::SetMaxFPS()가 이 값으로 t.MaxFPS를 다시 맞추므로(ApplyNonResolutionSettings
	// 경로), 여기 기본값이 0이면 그래픽 옵션만 바꿔도 프레임 제한이 풀린다.
	SetFrameRateLimit(60.0f);
}

void UCWGameUserSettings::ApplyNonResolutionSettings()
{
	Super::ApplyNonResolutionSettings();
	if (FullWorldResolutionRequests > 0)
	{
		ApplyWorldResolutionOverride();
	}
}

#pragma endregion

#pragma region WorldUIResolution

void UCWGameUserSettings::AcquireFullWorldResolution()
{
	if (++FullWorldResolutionRequests == 1)
	{
		ApplyWorldResolutionOverride();
	}
}

void UCWGameUserSettings::ReleaseFullWorldResolution()
{
	if (FullWorldResolutionRequests <= 0)
	{
		return;
	}
	if (--FullWorldResolutionRequests == 0)
	{
		ApplyWorldResolutionOverride();
	}
}

void UCWGameUserSettings::SetWorldResolutionPreview(bool bPreview)
{
	if (bWorldResolutionPreview == bPreview)
	{
		return;
	}
	bWorldResolutionPreview = bPreview;
	if (FullWorldResolutionRequests > 0)
	{
		ApplyWorldResolutionOverride();
	}
}

void UCWGameUserSettings::ApplyWorldResolutionOverride() const
{
	static IConsoleVariable* ResolutionQuality =
		IConsoleManager::Get().FindConsoleVariable(TEXT("sg.ResolutionQuality"));
	if (!ResolutionQuality)
	{
		return;
	}
	// 품질 적용과 같은 우선순위로 넣어야 이후 설정 화면의 품질 적용이 정상적으로 덮어씀
	const float Value = FullWorldResolutionRequests > 0 && !bWorldResolutionPreview
		? Scalability::MaxResolutionScale
		: ScalabilityQuality.ResolutionQuality;
	ResolutionQuality->Set(Value, ECVF_SetByScalability);
}

#pragma endregion

#pragma region Accessors

UCWGameUserSettings* UCWGameUserSettings::GetCWGameUserSettings()
{
	// 엔진 초기화 이전이나 종료 중에는 GEngine 이 없음
	if (!GEngine)
	{
		return nullptr;
	}

	// DefaultEngine.ini 의 GameUserSettingsClassName 등록이 누락되면
	// 엔진 기본 UGameUserSettings 가 반환되어 Cast 가 실패함
	// 즉 여기서 nullptr 이면 저장 로직이 아니라 ini 등록을 먼저 확인할 것
	return Cast<UCWGameUserSettings>(GEngine->GetGameUserSettings());
}

bool UCWGameUserSettings::SetMasterVolume(float InVolume)
{
	return ApplyVolume(MasterVolume, InVolume);
}

bool UCWGameUserSettings::SetBGMVolume(float InVolume)
{
	return ApplyVolume(BGMVolume, InVolume);
}

bool UCWGameUserSettings::SetSFXVolume(float InVolume)
{
	return ApplyVolume(SFXVolume, InVolume);
}

#pragma endregion

#pragma region PrivateFunctions

bool UCWGameUserSettings::ApplyVolume(float& OutStored, float InVolume)
{
	// ini 를 직접 편집한 경우를 포함해 저장값이 항상 유효 범위 안에 있도록 보정
	const float ClampedVolume = FMath::Clamp(InVolume, 0.0f, 1.0f);

	// 슬라이더는 드래그 중 같은 값으로 여러 번 들어올 수 있음
	// 부동소수 오차를 감안해 비교하고, 변화가 없으면 저장 대상에서 제외
	if (FMath::IsNearlyEqual(OutStored, ClampedVolume))
	{
		return false;
	}

	OutStored = ClampedVolume;

	// 여기서 SaveSettings 를 부르지 않는 이유
	// OnValueChanged 마다 디스크에 기록되면 드래그 한 번에 수십 번 쓰기가 발생함
	// 실제 저장은 슬라이더를 놓는 시점(CaptureEnd)이나 화면 종료 시 호출자가 수행
	// 반환한 true 는 호출자가 "저장할 변경분이 있다"고 판단하는 근거임
	return true;
}

#pragma endregion
