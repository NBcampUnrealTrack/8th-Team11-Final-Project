// CWSinkholeSoundSet.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Sinkhole/Data/CWSinkholeSoundTypes.h"
#include "CWSinkholeSoundSet.generated.h"

class USoundAttenuation;

/**
 * 싱크홀 기믹의 소리 전부를 한 표로 모은 애셋.
 *
 * 사운드 담당이 만지는 곳은 이 애셋 하나뿐이다. 어느 BP 를 열어 어느 이벤트에
 * 노드를 꽂아야 하는지 몰라도 된다. 재생 시점과 네트워크는 C++ 이 이미 정해 두었다.
 *
 * 어떤 애셋을 쓸지는 DefaultGame.ini 의 UCWSinkholeAudioSubsystem 항목이 정한다.
 */
UCLASS(BlueprintType)
class CROSSWALK_API UCWSinkholeSoundSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 등록되지 않았거나 Sound 가 비어 있으면 nullptr. */
	const FCWSinkholeSoundEntry* FindEntry(ECWSinkholeSoundCue Cue) const;

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	TMap<ECWSinkholeSoundCue, FCWSinkholeSoundEntry> Sounds;

	// 모든 소리에 함께 쓰는 거리 감쇠. 비워 두면 각 사운드 애셋 자신의 설정을 따른다.
	//
	// 쿼터뷰라 리스너가 화면 중앙 위에 떠 있다. 감쇠가 없으면 맵 반대편 싱크홀도
	// 바로 옆처럼 들려서 "어디서 났는지" 가 소리로 읽히지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	TObjectPtr<USoundAttenuation> Attenuation;
};
