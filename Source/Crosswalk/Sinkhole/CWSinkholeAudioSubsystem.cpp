// CWSinkholeAudioSubsystem.cpp

#include "Sinkhole/CWSinkholeAudioSubsystem.h"

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Sinkhole/Data/CWSinkholeSoundSet.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

void UCWSinkholeAudioSubsystem::PlayCue(const UObject* WorldContextObject, ECWSinkholeSoundCue Cue, FVector Location, USceneComponent* AttachTo, const APawn* SubjectPawn)
{
	if (GEngine == nullptr)
	{
		return;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (UCWSinkholeAudioSubsystem* Subsystem = World->GetSubsystem<UCWSinkholeAudioSubsystem>())
	{
		Subsystem->PlayCueInternal(Cue, Location, AttachTo, SubjectPawn);
	}
}

void UCWSinkholeAudioSubsystem::PlayCueInternal(ECWSinkholeSoundCue Cue, const FVector& Location, USceneComponent* AttachTo, const APawn* SubjectPawn)
{
	const UCWSinkholeSoundSet* Set = ResolveSoundSet();
	if (Set == nullptr)
	{
		return;
	}

	const FCWSinkholeSoundEntry* Entry = Set->FindEntry(Cue);
	if (Entry == nullptr)
	{
		return;
	}

	const float Pitch = 1.f + FMath::FRandRange(-Entry->PitchVariance, Entry->PitchVariance);

	// 내 화면의 내 캐릭터 일이면 위치와 상관없이 귀 옆에서 낸다. 붙일 필요도 없다.
	if (Entry->bNonSpatialForLocalPlayer && SubjectPawn != nullptr && SubjectPawn->IsLocallyControlled())
	{
		UGameplayStatics::PlaySound2D(this, Entry->Sound, Entry->VolumeMultiplier, Pitch);
		return;
	}

	USoundAttenuation* Attenuation = Entry->AttenuationOverride != nullptr ? Entry->AttenuationOverride.Get() : Set->Attenuation.Get();

	if (AttachTo != nullptr)
	{
		// 컴포넌트가 지금 있는 자리에서 시작해 그대로 따라간다. Location 은 쓰지 않는다.
		UGameplayStatics::SpawnSoundAttached(
			Entry->Sound,
			AttachTo,
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget,
			false,
			Entry->VolumeMultiplier,
			Pitch,
			0.f,
			Attenuation);
		return;
	}

	UGameplayStatics::PlaySoundAtLocation(
		GetWorld(),
		Entry->Sound,
		Location,
		Entry->VolumeMultiplier,
		Pitch,
		0.f,
		Attenuation);
}

const UCWSinkholeSoundSet* UCWSinkholeAudioSubsystem::ResolveSoundSet()
{
	if (bSoundSetResolved)
	{
		return LoadedSoundSet;
	}

	bSoundSetResolved = true;

	if (SoundSet.IsNull())
	{
		return nullptr;
	}

	LoadedSoundSet = SoundSet.LoadSynchronous();

	return LoadedSoundSet;
}
