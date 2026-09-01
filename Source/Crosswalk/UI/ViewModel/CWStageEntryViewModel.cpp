// CWStageEntryViewModel.cpp

#include "UI/ViewModel/CWStageEntryViewModel.h"
#include "Data/Structs/CWStageInfo.h"

void UCWStageEntryViewModel::SetStageInfo(const FCWStageInfo& StageInfo)
{
	// 식별자는 표시용이 아니라 확정에만 쓰므로 그대로 보관
	StageId = StageInfo.StageId;

	// 표시 상태는 변경 알림과 함께 반영
	UE_MVVM_SET_PROPERTY_VALUE(DisplayName, StageInfo.DisplayName);
	UE_MVVM_SET_PROPERTY_VALUE(Description, StageInfo.Description);
	UE_MVVM_SET_PROPERTY_VALUE(Thumbnail, StageInfo.Thumbnail);

	// 설명·썸네일 유무 파생값도 갱신 통지
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(HasDescription);
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(HasThumbnail);
}
