// CWStageSelectViewModel.cpp

#include "UI/ViewModel/CWStageSelectViewModel.h"
#include "Data/Assets/CWStageCatalog.h"
#include "Data/Structs/CWStageInfo.h"
#include "UI/ViewModel/CWStageEntryViewModel.h"
#include "UI/Progress/CWLocalStageProgress.h"

void UCWStageSelectViewModel::Initialize(UCWStageCatalog* Catalog)
{
	// 재초기화 대비 목록을 새로 구성
	TArray<TObjectPtr<UCWStageEntryViewModel>> NewEntries;
	UCWLocalStageProgressSave* Progress = FCWLocalStageProgress::Load();

	if (IsValid(Catalog))
	{
		const TArray<FCWStageInfo>& Stages = Catalog->GetStages();

		NewEntries.Reserve(Stages.Num());

		for (const FCWStageInfo& Stage : Stages)
		{
			// 카드 1장을 이 VM 소유로 생성해 채움
			UCWStageEntryViewModel* Entry =
				NewObject<UCWStageEntryViewModel>(this);

			Entry->SetStageInfo(Stage);
			Entry->SetIsUnlocked(FCWLocalStageProgress::IsUnlocked(Catalog, NewEntries.Num(), Progress));

			NewEntries.Add(Entry);
		}
	}

	UE_MVVM_SET_PROPERTY_VALUE(StageEntries, NewEntries);

	// 항목이 있으면 첫 스테이지를 기본 선택, 없으면 선택 없음
	SelectedIndex = NewEntries.Num() > 0 ? 0 : INDEX_NONE;
	// 다시 열 때 가장 최근에 해금된 작업으로 바로 이동하기 위함
	for (int32 Index = 0; Index < NewEntries.Num(); ++Index)
	{
		if (NewEntries[Index]->GetIsUnlocked()) SelectedIndex = Index;
	}
	UE_MVVM_SET_PROPERTY_VALUE(SelectedDisplayName,
		HasSelection() ? StageEntries[SelectedIndex]->GetDisplayName() : FText::GetEmpty());

	// 목록이 통째로 바뀌었으므로 선택·파생 표시를 강제로 다시 통지
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(SelectedIndex);
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(HasSelection);
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetSelectedEntry);
}

bool UCWStageSelectViewModel::RequestSelectByIndex(int32 Index)
{
	// 범위 밖 인덱스는 무시 (유효한 선택만 허용)
	if (!StageEntries.IsValidIndex(Index) || !StageEntries[Index]->GetIsUnlocked())
	{
		return false;
	}

	// 선택이 실제로 바뀐 경우에만 파생값 통지
	if (UE_MVVM_SET_PROPERTY_VALUE(SelectedIndex, Index))
	{
		UE_MVVM_SET_PROPERTY_VALUE(SelectedDisplayName, StageEntries[Index]->GetDisplayName());
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(HasSelection);
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetSelectedEntry);
	}

	return true;
}

bool UCWStageSelectViewModel::ConfirmSelection()
{
	const FName StageId = GetSelectedStageId();

	// 유효한 선택이 없으면 확정 불가
	if (StageId.IsNone() || !GetSelectedEntry()->GetIsUnlocked())
	{
		return false;
	}

	OnStageConfirmed.Broadcast(StageId);

	return true;
}

FName UCWStageSelectViewModel::GetSelectedStageId() const
{
	const UCWStageEntryViewModel* Entry = GetSelectedEntry();

	return IsValid(Entry) ? Entry->GetStageId() : NAME_None;
}

UCWStageEntryViewModel* UCWStageSelectViewModel::GetSelectedEntry() const
{
	if (!StageEntries.IsValidIndex(SelectedIndex))
	{
		return nullptr;
	}

	return StageEntries[SelectedIndex];
}
