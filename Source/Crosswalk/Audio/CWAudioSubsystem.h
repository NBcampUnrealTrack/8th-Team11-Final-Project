// CWAudioSubsystem.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CWAudioSubsystem.generated.h"

class USoundClass;
class USoundMix;
class UWorld;

// 환경설정에서 조절하는 오디오 채널
UENUM(BlueprintType)
enum class ECWAudioChannel : uint8
{
	Master,
	BGM,
	SFX
};

// 채널별 볼륨을 SoundMix override 로 적용하는 서브시스템
// 재생 책임은 UCWMusicSubsystem 이 갖고, 이 클래스는 볼륨만 담당함
// override 는 현재 재생 중인 소리와 이후 재생될 소리 모두에 적용됨
UCLASS(Config = Game)
class CROSSWALK_API UCWAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

#pragma region SubsystemOverrides

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

#pragma endregion

#pragma region PublicFunctions

public:
	// 채널 볼륨을 0~1 로 설정하고 즉시 적용함
	// 저장은 하지 않음. 저장값의 원본은 UCWGameUserSettings 임
	UFUNCTION(BlueprintCallable, Category = "CW|Audio")
	void SetChannelVolume(ECWAudioChannel Channel, float Volume01);

	// 저장값을 다시 읽어 세 채널을 한꺼번에 적용함 (게임 시작·설정 초기화 시)
	UFUNCTION(BlueprintCallable, Category = "CW|Audio")
	void ApplySavedVolumes();

#pragma endregion

#pragma region PrivateFunctions

private:
	// UCWGameUserSettings 의 저장값을 캐시로 읽어옴
	void LoadSavedVolumes();

	// 캐시된 세 채널을 오디오 디바이스에 적용함
	void ApplyAllChannels();

	// 채널 하나를 SoundMix override 로 적용함
	void ApplyChannel(ECWAudioChannel Channel);

	// SoundMix 가 아직 활성화되지 않았으면 한 번만 Push 함
	void EnsureMixPushed(USoundMix* SoundMix);

	// 맵 로드로 SoundMix 가 초기화된 경우를 대비해 재적용함
	void HandlePostLoadMap(UWorld* LoadedWorld);

	// 채널에 대응하는 SoundClass 를 반환함. 최초 1회만 로드하고 하드 레퍼런스로 보관함
	USoundClass* ResolveSoundClass(ECWAudioChannel Channel);

	// MainMix 를 반환함. 최초 1회만 로드하고 하드 레퍼런스로 보관함
	USoundMix* ResolveSoundMix();

	// 캐시에 보관 중인 채널 볼륨을 반환함
	float GetCachedVolume(ECWAudioChannel Channel) const;

#pragma endregion

#pragma region Variables

private:
	// DefaultGame.ini 에서 지정함 비어 있으면 볼륨 적용을 건너뜀
	UPROPERTY(Config, EditAnywhere, Category = "CW|Audio")
	TSoftObjectPtr<USoundMix> MainMix;

	UPROPERTY(Config, EditAnywhere, Category = "CW|Audio")
	TSoftObjectPtr<USoundClass> MasterClass;

	UPROPERTY(Config, EditAnywhere, Category = "CW|Audio")
	TSoftObjectPtr<USoundClass> BGMClass;

	UPROPERTY(Config, EditAnywhere, Category = "CW|Audio")
	TSoftObjectPtr<USoundClass> SFXClass;

	// LoadSynchronous 결과를 하드 레퍼런스로 보관함
	// TSoftObjectPtr 만으로는 GC 를 막지 못해, 오디오 디바이스가 참조 중인 상태에서
	// 애셋이 수거되면 FAudioDevice::ApplyClassAdjusters 에서 크래시함
	UPROPERTY(Transient)
	TObjectPtr<USoundMix> LoadedMainMix;

	UPROPERTY(Transient)
	TObjectPtr<USoundClass> LoadedMasterClass;

	UPROPERTY(Transient)
	TObjectPtr<USoundClass> LoadedBGMClass;

	UPROPERTY(Transient)
	TObjectPtr<USoundClass> LoadedSFXClass;

	// 저장값을 읽어둔 캐시 맵 이동 후 재적용의 기준이 됨
	float MasterVolume = 1.0f;

	// 배경음 채널 캐시
	float BGMVolume = 1.0f;

	// 효과음 채널 캐시
	float SFXVolume = 1.0f;

	// SoundMix 를 중복 Push 하지 않기 위한 표시
	bool bIsMixPushed = false;

	// 맵 로드 델리게이트 해제를 위한 핸들
	FDelegateHandle PostLoadMapHandle;

#pragma endregion
};
