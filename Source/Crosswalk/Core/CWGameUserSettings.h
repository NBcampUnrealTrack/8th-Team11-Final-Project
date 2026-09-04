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

#pragma region PrivateFunctions

private:
	// 공통 Clamp 및 변경 여부 판정
	static bool ApplyVolume(float& OutStored, float InVolume);

#pragma endregion

#pragma region Variables

private:
	// Config 지정이 없으면 GameUserSettings.ini 에 기록되지 않음
	UPROPERTY(Config)
	float MasterVolume = 1.0f;

	UPROPERTY(Config)
	float BGMVolume = 1.0f;

	UPROPERTY(Config)
	float SFXVolume = 1.0f;

#pragma endregion
};
