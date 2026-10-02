// CWSinkholeSoundSet.cpp

#include "Sinkhole/Data/CWSinkholeSoundSet.h"

const FCWSinkholeSoundEntry* UCWSinkholeSoundSet::FindEntry(ECWSinkholeSoundCue Cue) const
{
	const FCWSinkholeSoundEntry* Entry = Sounds.Find(Cue);

	// 칸만 만들어 두고 사운드를 아직 안 꽂은 경우도 "없음" 으로 친다. 호출부가 한 번만 검사하면 된다.
	if (Entry == nullptr || Entry->Sound == nullptr)
	{
		return nullptr;
	}

	return Entry;
}
