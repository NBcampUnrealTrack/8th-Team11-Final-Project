// CWAudioSubsystem.cpp

#include "Audio/CWAudioSubsystem.h"

#include "Core/CWGameUserSettings.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "UObject/UObjectGlobals.h"

#pragma region SubsystemOverrides

void UCWAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 저장값만 먼저 읽어둠
	// 이 시점에는 월드가 없어 오디오 디바이스에 적용할 수 없으므로,
	// 실제 적용은 첫 맵 로드 이후 HandlePostLoadMap 에서 수행함
	LoadSavedVolumes();

	PostLoadMapHandle =
		FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
			this,
			&UCWAudioSubsystem::HandlePostLoadMap
		);
}

void UCWAudioSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);

	PostLoadMapHandle.Reset();

	// 에디터의 오디오 디바이스는 PIE 세션보다 오래 살아남음
	// Push 한 SoundMix 를 남겨두면 다음 세션에서 이미 수거된 애셋을 참조하게 됨
	if (bIsMixPushed && IsValid(LoadedMainMix) && GetWorld())
	{
		UGameplayStatics::PopSoundMixModifier(this, LoadedMainMix);
	}

	bIsMixPushed = false;

	LoadedMainMix = nullptr;
	LoadedMasterClass = nullptr;
	LoadedBGMClass = nullptr;
	LoadedSFXClass = nullptr;

	Super::Deinitialize();
}

#pragma endregion

#pragma region PublicFunctions

void UCWAudioSubsystem::SetChannelVolume(ECWAudioChannel Channel, float Volume01)
{
	const float ClampedVolume = FMath::Clamp(Volume01, 0.0f, 1.0f);

	switch (Channel)
	{
	case ECWAudioChannel::Master:
		MasterVolume = ClampedVolume;
		break;

	case ECWAudioChannel::BGM:
		BGMVolume = ClampedVolume;
		break;

	case ECWAudioChannel::SFX:
		SFXVolume = ClampedVolume;
		break;
	}

	// Master 는 BGM·SFX 의 최종 볼륨에 곱해지므로 세 채널을 함께 갱신함
	if (Channel == ECWAudioChannel::Master)
	{
		ApplyAllChannels();

		return;
	}

	ApplyChannel(Channel);
}

void UCWAudioSubsystem::ApplySavedVolumes()
{
	LoadSavedVolumes();

	ApplyAllChannels();
}

#pragma endregion

#pragma region PrivateFunctions

void UCWAudioSubsystem::LoadSavedVolumes()
{
	UCWGameUserSettings* GameUserSettings =
		UCWGameUserSettings::GetCWGameUserSettings();

	if (!IsValid(GameUserSettings))
	{
		// ini 에 클래스가 등록되지 않은 경우로, 기본값 1.0 을 그대로 사용함
		return;
	}

	MasterVolume = GameUserSettings->GetMasterVolume();
	BGMVolume = GameUserSettings->GetBGMVolume();
	SFXVolume = GameUserSettings->GetSFXVolume();
}

void UCWAudioSubsystem::ApplyAllChannels()
{
	if (MainMix.IsNull())
	{
		// 지정이 없으면 볼륨이 조용히 무시되므로 원인을 남김
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[CWAudio] MainMix 가 지정되지 않아 채널 볼륨을 적용하지 못함. DefaultGame.ini 확인 필요")
		);

		return;
	}

	ApplyChannel(ECWAudioChannel::Master);
	ApplyChannel(ECWAudioChannel::BGM);
	ApplyChannel(ECWAudioChannel::SFX);
}

void UCWAudioSubsystem::ApplyChannel(ECWAudioChannel Channel)
{
	// 월드가 없으면 오디오 디바이스를 찾을 수 없음
	// 첫 맵 로드 이후 HandlePostLoadMap 이 다시 적용하므로 여기서는 건너뜀
	if (!GetWorld())
	{
		return;
	}

	USoundMix* SoundMix = ResolveSoundMix();
	USoundClass* SoundClass = ResolveSoundClass(Channel);

	if (!IsValid(SoundMix) || !IsValid(SoundClass))
	{
		return;
	}

	EnsureMixPushed(SoundMix);

	// SoundMix override 는 대상 클래스에 직접 속한 소리에만 적용됨
	// SCL_Master 에 직접 물린 사운드가 없어 Master override 만으로는 효과가 없으므로
	// BGM·SFX 의 최종 볼륨에 Master 를 곱해 전체 음량을 반영함
	// bApplyToChildren 을 true 로 주면 Master 가 자식 override 를 덮어써 채널별 조절이 사라짐
	const float FinalVolume =
		(Channel == ECWAudioChannel::Master)
		? MasterVolume
		: MasterVolume * GetCachedVolume(Channel);

	// FadeInTime = 0 은 슬라이더 드래그에 즉시 반응하기 위함
	UGameplayStatics::SetSoundMixClassOverride(
		this,
		SoundMix,
		SoundClass,
		FinalVolume,
		1.0f,
		0.0f,
		false
	);
}

void UCWAudioSubsystem::EnsureMixPushed(USoundMix* SoundMix)
{
	if (bIsMixPushed)
	{
		return;
	}

	// Push 하지 않으면 override 를 설정해도 실제 소리에 반영되지 않음
	UGameplayStatics::PushSoundMixModifier(this, SoundMix);

	bIsMixPushed = true;
}

void UCWAudioSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	// 맵 이동 시 오디오 디바이스가 활성 SoundMix 를 정리할 수 있으므로
	// Push 상태를 초기화하고 캐시된 값으로 다시 적용함
	bIsMixPushed = false;

	ApplyAllChannels();
}

USoundClass* UCWAudioSubsystem::ResolveSoundClass(ECWAudioChannel Channel)
{
	// 로드 결과를 UPROPERTY 로 붙잡아야 오디오 디바이스가 쓰는 동안 GC 되지 않음
	switch (Channel)
	{
	case ECWAudioChannel::Master:
		if (!IsValid(LoadedMasterClass))
		{
			LoadedMasterClass = MasterClass.LoadSynchronous();
		}
		return LoadedMasterClass;

	case ECWAudioChannel::BGM:
		if (!IsValid(LoadedBGMClass))
		{
			LoadedBGMClass = BGMClass.LoadSynchronous();
		}
		return LoadedBGMClass;

	case ECWAudioChannel::SFX:
		if (!IsValid(LoadedSFXClass))
		{
			LoadedSFXClass = SFXClass.LoadSynchronous();
		}
		return LoadedSFXClass;
	}

	return nullptr;
}

USoundMix* UCWAudioSubsystem::ResolveSoundMix()
{
	if (!IsValid(LoadedMainMix))
	{
		LoadedMainMix = MainMix.LoadSynchronous();
	}

	return LoadedMainMix;
}

float UCWAudioSubsystem::GetCachedVolume(ECWAudioChannel Channel) const
{
	switch (Channel)
	{
	case ECWAudioChannel::Master:
		return MasterVolume;

	case ECWAudioChannel::BGM:
		return BGMVolume;

	case ECWAudioChannel::SFX:
		return SFXVolume;
	}

	return 1.0f;
}

#pragma endregion
