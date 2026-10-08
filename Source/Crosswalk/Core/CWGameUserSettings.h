// CWGameUserSettings.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "CWGameUserSettings.generated.h"

// 오디오 채널 볼륨을 저장하는 GameUserSettings 확장
// 저장값이 단일 원본이며 설정 화면 슬라이더의 초기값도 여기서 읽음
// 실제 소리 적용은 UCWAudioSubsystem 이 담당하고 이 클래스는 저장만 책임짐
UCLASS(Config = GameUserSettings)
class CROSSWALK_API UCWGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

#pragma region GameUserSettingsOverrides

public:
	// 설정을 초기화해도 세 채널이 1.0 으로 시작하도록 보장
	virtual void SetToDefaults() override;

	// 설정 화면에서 품질을 바꾸면 해상도 품질도 사용자 값으로 다시 적용되므로 고정 요청이 있으면 뒤에서 다시 올림
	virtual void ApplyNonResolutionSettings() override;

#pragma endregion

#pragma region WorldUIResolution

public:
	// 클립보드처럼 3D 월드에 그린 UI 가 떠 있는 동안만 해상도 품질을 100% 로 올림
	// 해상도 품질은 월드 전체를 낮춰 그린 뒤 늘리는 값이라 종이 글씨만 따로 선명하게 할 수 없음
	// 요청마다 한 번씩 풀어야 하며, 요청이 모두 풀리면 사용자가 고른 값으로 돌아감
	void AcquireFullWorldResolution();
	void ReleaseFullWorldResolution();

	// 품질 변경 확인 중에는 고정을 잠시 풀어 사용자가 고른 화질 그대로 보여줌
	// 설정 화면은 클립보드 위라 고정된 채면 품질을 바꿔도 가장 큰 차이인 해상도가 안 보임
	void SetWorldResolutionPreview(bool bPreview);

#pragma endregion

#pragma region Accessors

public:
	// 프로젝트의 GameUserSettings 를 CW 타입으로 반환
	// DefaultEngine.ini 에 GameUserSettingsClassName 이 등록되지 않았으면 nullptr
	UFUNCTION(BlueprintPure, Category = "CW|Settings|Audio")
	static UCWGameUserSettings* GetCWGameUserSettings();

	UFUNCTION(BlueprintPure, Category = "CW|Settings|Audio")
	float GetMasterVolume() const
	{
		return MasterVolume;
	}

	UFUNCTION(BlueprintPure, Category = "CW|Settings|Audio")
	float GetBGMVolume() const
	{
		return BGMVolume;
	}

	UFUNCTION(BlueprintPure, Category = "CW|Settings|Audio")
	float GetSFXVolume() const
	{
		return SFXVolume;
	}

	// 0~1 로 Clamp 하여 저장값만 갱신함
	// 디스크 기록은 호출자가 SaveSettings 로 수행 (드래그 중 저장을 피하기 위함)
	// 값이 실제로 바뀌었으면 true 를 반환해 호출자가 저장 필요 여부를 판단
	UFUNCTION(BlueprintCallable, Category = "CW|Settings|Audio")
	bool SetMasterVolume(float InVolume);

	UFUNCTION(BlueprintCallable, Category = "CW|Settings|Audio")
	bool SetBGMVolume(float InVolume);

	UFUNCTION(BlueprintCallable, Category = "CW|Settings|Audio")
	bool SetSFXVolume(float InVolume);

#pragma endregion

#pragma region WorldUIResolutionInternals

private:
	// 저장값(ScalabilityQuality)은 건드리지 않고 적용 중인 값만 바꿔 설정 파일에는 사용자 값이 남음
	void ApplyWorldResolutionOverride() const;

#pragma endregion

#pragma region PrivateFunctions

private:
	// 공통 Clamp 및 변경 여부 판정
	static bool ApplyVolume(float& OutStored, float InVolume);

#pragma endregion

#pragma region Variables

private:
	int32 FullWorldResolutionRequests = 0;

	bool bWorldResolutionPreview = false;

	// Config 지정이 없으면 GameUserSettings.ini 에 기록되지 않음
	UPROPERTY(Config)
	float MasterVolume = 1.0f;

	UPROPERTY(Config)
	float BGMVolume = 1.0f;

	UPROPERTY(Config)
	float SFXVolume = 1.0f;

#pragma endregion
};
