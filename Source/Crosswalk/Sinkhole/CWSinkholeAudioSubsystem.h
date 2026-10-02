// CWSinkholeAudioSubsystem.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Sinkhole/Data/CWSinkholeSoundTypes.h"
#include "CWSinkholeAudioSubsystem.generated.h"

class APawn;
class UCWSinkholeSoundSet;
class USceneComponent;

/**
 * 싱크홀 기믹의 소리를 한 입구로 재생한다.
 *
 * 각 클래스는 "지금 이 순간이 일어났다" 만 알리고(PlayCue), 무슨 소리를 어떤 크기로
 * 낼지는 UCWSinkholeSoundSet 한 장이 정한다. 그래서 BP 마다 따로 사운드 노드를 꽂을 일이 없다.
 *
 * 네트워크
 *   이 클래스는 아무것도 보내지 않는다. 부르는 쪽이 이미 모든 머신에서 도는 지점(OnRep,
 *   기존 Multicast, 리플리케이트된 액터의 BeginPlay)이라 각 머신이 자기 화면에서 울린다.
 *   데디케이티드 서버에서는 들을 사람이 없으므로 아무것도 하지 않는다.
 */
UCLASS(Config = Game)
class CROSSWALK_API UCWSinkholeAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Cue 에 등록된 소리를 Location 에서 한 번 재생한다. 등록이 없으면 아무 일도 없다.
	 *
	 * @param AttachTo 넘기면 그 컴포넌트에 붙여 재생해 따라다닌다. 움직이는 대상(플레이어, 들고 있는 총)용.
	 *                 비우면 Location 에 고정된다. 곧 사라지는 액터(복구된 싱크홀)는 반드시 비워야
	 *                 액터와 함께 소리가 끊기지 않는다.
	 * @param SubjectPawn 이 소리의 주인공(빠진 플레이어, 총을 든 플레이어). 이 화면의 본인이면
	 *                    bNonSpatialForLocalPlayer 가 켜진 소리를 2D 로 낸다. 주인공이 없으면 비운다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Sinkhole|Sound", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "AttachTo,SubjectPawn"))
	static void PlayCue(const UObject* WorldContextObject, ECWSinkholeSoundCue Cue, FVector Location, USceneComponent* AttachTo = nullptr, const APawn* SubjectPawn = nullptr);

private:
	void PlayCueInternal(ECWSinkholeSoundCue Cue, const FVector& Location, USceneComponent* AttachTo, const APawn* SubjectPawn);

	/** 처음 부를 때 한 번만 로드하고 하드 레퍼런스로 붙잡아 둔다. 비어 있으면 nullptr. */
	const UCWSinkholeSoundSet* ResolveSoundSet();

private:
	// DefaultGame.ini 에서 지정한다. 비워 두면 싱크홀 소리가 전부 꺼진다.
	UPROPERTY(Config)
	TSoftObjectPtr<UCWSinkholeSoundSet> SoundSet;

	// LoadSynchronous 결과. TSoftObjectPtr 만으로는 GC 를 막지 못한다.
	UPROPERTY(Transient)
	TObjectPtr<UCWSinkholeSoundSet> LoadedSoundSet;

	// 로드를 시도했는지. 경로가 틀렸을 때 매번 다시 로드하고 매번 로그를 찍지 않게 한다.
	bool bSoundSetResolved = false;
};
