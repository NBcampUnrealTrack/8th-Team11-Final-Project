#include "UI/Progress/CWLocalStageProgress.h"
#include "Core/CWGameState.h"
#include "Data/Assets/CWStageCatalog.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "UI/CWUILog.h"

namespace
{
	const FString ProgressSlot = TEXT("CWLocalUIStageProgress_v1");
}

UCWLocalStageProgressSave* FCWLocalStageProgress::Load()
{
	if (UGameplayStatics::DoesSaveGameExist(ProgressSlot, 0))
	{
		UCWLocalStageProgressSave* Save = Cast<UCWLocalStageProgressSave>(UGameplayStatics::LoadGameFromSlot(ProgressSlot, 0));
		// 다른 버전의 저장 파일을 새 데이터로 덮어쓰지 않기 위함
		if (Save && Save->Version == 1) return Save;
		UE_LOG(LogCWUI, Warning, TEXT("Local stage progress could not be loaded; keeping the existing slot."));
		return nullptr;
	}
	return Cast<UCWLocalStageProgressSave>(UGameplayStatics::CreateSaveGameObject(UCWLocalStageProgressSave::StaticClass()));
}

UCWStageCatalog* FCWLocalStageProgress::GetCatalog()
{
	return LoadObject<UCWStageCatalog>(nullptr, TEXT("/Game/CrossWalk/Maps/Stages/DA_StageCatalog.DA_StageCatalog"));
}

bool FCWLocalStageProgress::IsUnlocked(const UCWStageCatalog* Catalog, int32 Index, const UCWLocalStageProgressSave* Save)
{
	if (!Catalog || !Catalog->GetStages().IsValidIndex(Index)) return false;
	const TArray<FCWStageInfo>& Stages = Catalog->GetStages();
	// 이전 단계가 모두 완료된 경우만 허용하여 합류한 상위 스테이지로 순서를 건너뛰지 않게 함
	for (int32 Previous = 0; Previous < Index; ++Previous)
	{
		if (!Save || !Save->ClearedStages.Contains(Stages[Previous].StageId)) return false;
	}
	const FName Required = Stages[Index].RequiredStageId;
	return Required.IsNone() || (Save && Save->ClearedStages.Contains(Required));
}

bool FCWLocalStageProgress::CanSelect(FName StageId)
{
	UCWStageCatalog* Catalog = GetCatalog();
	if (!Catalog) return false;
	const int32 Index = Catalog->GetStages().IndexOfByPredicate([StageId](const FCWStageInfo& Stage) { return Stage.StageId == StageId; });
	return IsUnlocked(Catalog, Index, Load());
}

void FCWLocalStageProgress::RecordClear(UWorld* World)
{
	ACWGameState* State = World ? World->GetGameState<ACWGameState>() : nullptr;
	UCWStageCatalog* Catalog = GetCatalog();
	if (!State || !State->IsStageCleared() || !Catalog) return;
	// 선택창의 공유 값이 다음 선택으로 바뀌어도 현재 클리어한 맵만 기록하기 위함
	const FString MapName = UGameplayStatics::GetCurrentLevelName(World, true);
	const FCWStageInfo* Cleared = Catalog->GetStages().FindByPredicate([&MapName](const FCWStageInfo& Stage)
	{
		return !Stage.GameplayMap.IsNull() && Stage.GameplayMap.ToSoftObjectPath().GetAssetName() == MapName;
	});
	if (!Cleared) return;
	UCWLocalStageProgressSave* Save = Load();
	if (!Save || Save->ClearedStages.Contains(Cleared->StageId)) return;
	Save->ClearedStages.Add(Cleared->StageId);
	// 클리어 직후 선택창에서도 해금이 보이도록 작은 UI 진행 데이터만 즉시 저장함
	if (!UGameplayStatics::SaveGameToSlot(Save, ProgressSlot, 0))
	{
		UE_LOG(LogCWUI, Error, TEXT("Failed to save local stage progress for %s."), *Cleared->StageId.ToString());
	}
}
