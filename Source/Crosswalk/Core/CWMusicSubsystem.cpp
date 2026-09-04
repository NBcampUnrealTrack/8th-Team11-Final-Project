//CWMusicsubsystem.cpp

#include "CWMusicSubsystem.h"

#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"


void UCWMusicSubsystem::Deinitialize()
{
	if (MusicComponent)
	{
		MusicComponent->Stop();
		MusicComponent = nullptr;
	}

	CurrentMusic = nullptr;

	Super::Deinitialize();
}

void UCWMusicSubsystem::PlayMusic(USoundBase* NewMusic, float FadeInTime)
{
	if (!NewMusic)
	{
		return;
	}

	if (CurrentMusic == NewMusic && MusicComponent && MusicComponent->IsPlaying())
	{
		return;
	}

	StopMusic(FadeInTime * 0.5f);

	MusicComponent = UGameplayStatics::SpawnSound2D(
		this,
		NewMusic,
		1.0f,
		1.0f,
		0.0f,
		nullptr,
		true,
		false);

	if (!MusicComponent)
	{
		return;
	}

	CurrentMusic = NewMusic;

	if (FadeInTime > 0.0f)
	{
		MusicComponent->FadeIn(FadeInTime, 1.0f);
	}
}

void UCWMusicSubsystem::StopMusic(float FadeOutTime)
{
	if (!MusicComponent)
	{
		return;
	}

	if (FadeOutTime > 0.0f)
	{
		MusicComponent->FadeOut(FadeOutTime, 0.0f);
	}
	else
	{
		MusicComponent->Stop();
	}

	MusicComponent = nullptr;
	CurrentMusic = nullptr;
}
