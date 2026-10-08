// CWStageEntryViewModel.cpp

#include "UI/ViewModel/CWStageEntryViewModel.h"
#include "Data/Structs/CWStageInfo.h"

void UCWStageEntryViewModel::SetIsUnlocked(bool bInUnlocked)
{
	UE_MVVM_SET_PROPERTY_VALUE(bIsUnlocked, bInUnlocked);

	// 표시 이름은 항상 원래 이름에서 다시 만듦 - 여러 번 불려도 접미사가 겹치지 않고 해금되면 원래 이름으로 돌아감
	const FText NewDisplayName = bInUnlocked
		? BaseDisplayName
		: FText::Format(NSLOCTEXT("CWUI", "LockedStageName", "{0} · 잠김"), BaseDisplayName);
	UE_MVVM_SET_PROPERTY_VALUE(DisplayName, NewDisplayName);
}

void UCWStageEntryViewModel::SetStageInfo(const FCWStageInfo& StageInfo)
{
	// 식별자는 표시용이 아니라 확정에만 쓰므로 그대로 보관
	StageId = StageInfo.StageId;

	// 표시 상태는 변경 알림과 함께 반영 - 잠김 접미사의 기준이 되는 원래 이름을 따로 보관
	BaseDisplayName = StageInfo.DisplayName;
	UE_MVVM_SET_PROPERTY_VALUE(DisplayName, StageInfo.DisplayName);
	UE_MVVM_SET_PROPERTY_VALUE(Description, StageInfo.Description);
	UE_MVVM_SET_PROPERTY_VALUE(Thumbnail, StageInfo.Thumbnail);

	// 설명·썸네일 유무 파생값도 갱신 통지
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(HasDescription);
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(HasThumbnail);
}
