//CWMusicsubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CWMusicSubsystem.generated.h"

class USoundBase;
class UAudioComponent;

UCLASS()
class CROSSWALK_API UCWMusicSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	//새로운 BGM 재생용
	UFUNCTION(BlueprintCallable, Category = "Audio|Music")
	void PlayMusic(USoundBase* PlayMusic, float FadeInTime = 1.0f);

	//BGM 중단
	UFUNCTION(BlueprintCallable, Category = "Audio|Music")
	void StopMusic(float FadeOutTime = 1.0f);


	//BGM 볼륨 조절
	UFUNCTION(BlueprintCallable, Category = "Audio|Music")
	void SetMusicVolume(float Volume);

private:
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MusicComponent;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CurrentMusic;
};
