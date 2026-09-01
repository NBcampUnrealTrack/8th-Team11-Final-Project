// CWStageCatalog.cpp

#include "Data/Assets/CWStageCatalog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "CWStageCatalog"

const FCWStageInfo* UCWStageCatalog::FindStageById(FName StageId) const
{
	if (StageId.IsNone())
	{
		return nullptr;
	}

	// StageId 가 일치하는 첫 항목 (검증에서 중복을 막으므로 유일)
	return Stages.FindByPredicate(
		[StageId](const FCWStageInfo& Stage)
		{
			return Stage.StageId == StageId;
		}
	);
}

#if WITH_EDITOR
EDataValidationResult UCWStageCatalog::IsDataValid(
	FDataValidationContext& Context
) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// 1차: StageId 유효성 + 조회용 집합/맵 구성
	TSet<FName> StageIds;
	TMap<FName, FName> RequiredById;

	for (int32 Index = 0; Index < Stages.Num(); ++Index)
	{
		const FCWStageInfo& Stage = Stages[Index];

		// StageId 누락
		if (Stage.StageId.IsNone())
		{
			Context.AddError(FText::Format(
				LOCTEXT("EmptyStageId", "Stages[{0}] 의 StageId 가 비어 있음"),
				FText::AsNumber(Index)
			));
			Result = EDataValidationResult::Invalid;
		}
		// StageId 중복
		else if (StageIds.Contains(Stage.StageId))
		{
			Context.AddError(FText::Format(
				LOCTEXT("DuplicateStageId", "StageId '{0}' 가 중복됨 (Stages[{1}])"),
				FText::FromName(Stage.StageId),
				FText::AsNumber(Index)
			));
			Result = EDataValidationResult::Invalid;
		}
		else
		{
			StageIds.Add(Stage.StageId);
			RequiredById.Add(Stage.StageId, Stage.RequiredStageId);
		}

		// GameplayMap 누락 (썸네일·설명은 비어도 통과)
		if (Stage.GameplayMap.IsNull())
		{
			Context.AddError(FText::Format(
				LOCTEXT("MissingGameplayMap", "Stages[{0}] (StageId '{1}') 의 GameplayMap 이 지정되지 않음"),
				FText::AsNumber(Index),
				FText::FromName(Stage.StageId)
			));
			Result = EDataValidationResult::Invalid;
		}

		// 자기 자신을 선행 조건으로 요구
		if (!Stage.StageId.IsNone() &&
			Stage.RequiredStageId == Stage.StageId)
		{
			Context.AddError(FText::Format(
				LOCTEXT("SelfRequire", "StageId '{0}' 가 자기 자신을 해금 조건으로 요구함"),
				FText::FromName(Stage.StageId)
			));
			Result = EDataValidationResult::Invalid;
		}
	}

	// 2차: RequiredStageId 존재 여부 + 순환 참조 검출
	for (const FCWStageInfo& Stage : Stages)
	{
		if (Stage.StageId.IsNone() ||
			Stage.RequiredStageId.IsNone())
		{
			continue;
		}

		// 존재하지 않는 선행 스테이지
		if (!StageIds.Contains(Stage.RequiredStageId))
		{
			Context.AddError(FText::Format(
				LOCTEXT("MissingRequired", "StageId '{0}' 의 RequiredStageId '{1}' 가 카탈로그에 없음"),
				FText::FromName(Stage.StageId),
				FText::FromName(Stage.RequiredStageId)
			));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		// 순환 참조 — RequiredStageId 체인을 따라가며 자기 재방문 확인
		TSet<FName> Visited;
		FName Current = Stage.StageId;

		while (!Current.IsNone())
		{
			if (Visited.Contains(Current))
			{
				Context.AddError(FText::Format(
					LOCTEXT("CyclicRequire", "StageId '{0}' 의 해금 조건이 순환 참조됨"),
					FText::FromName(Stage.StageId)
				));
				Result = EDataValidationResult::Invalid;
				break;
			}

			Visited.Add(Current);

			const FName* Next = RequiredById.Find(Current);
			Current = Next ? *Next : NAME_None;
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
