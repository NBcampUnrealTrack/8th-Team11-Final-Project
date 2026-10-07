// CWObjectiveViewModel.cpp

#include "UI/ViewModel/CWObjectiveViewModel.h"
#include "CoreMinimal.h"
#include "Core/CWGameState.h"
#include "GameFramework/GameStateBase.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/RetainerBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UI/Progress/CWLocalStageProgress.h"
#include "UObject/UnrealType.h"

#pragma region ObjectiveData

void UCWObjectiveViewModel::PublishObjectives(const TArray<FCWStageObjective>& InObjectives)
{
	if (Objectives == InObjectives) return;
	struct FCompletedRowState
	{
		TWeakObjectPtr<UUserWidget> Panel;
		FText Name;
		double Elapsed = 0.0;
		float Progress = 0.f;
	};
	TArray<FCompletedRowState> CompletedRows;
	TArray<UUserWidget*> Widgets;
	if (GetBoundWorld()) UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GetBoundWorld(), Widgets, UUserWidget::StaticClass(), false);
	for (UUserWidget* Panel : Widgets)
	{
		UVerticalBox* List = Cast<UVerticalBox>(Panel->GetWidgetFromName(TEXT("VerticalBox_Objectives")));
		if (!List) continue;
		for (UWidget* Child : List->GetAllChildren())
		{
			UUserWidget* Row = Cast<UUserWidget>(Child);
			if (!Row) continue;
			const FTextProperty* Name = FindFProperty<FTextProperty>(Row->GetClass(), TEXT("ObjectiveText"));
			const FIntProperty* Completed = FindFProperty<FIntProperty>(Row->GetClass(), TEXT("CompletedCount"));
			const FIntProperty* Total = FindFProperty<FIntProperty>(Row->GetClass(), TEXT("TotalCount"));
			const FNumericProperty* Time = FindFProperty<FNumericProperty>(Row->GetClass(), TEXT("CompletedPaintElapsed"));
			const FObjectPropertyBase* Material = FindFProperty<FObjectPropertyBase>(Row->GetClass(), TEXT("CompletedPaintMID"));
			if (!Name || !Completed || !Total || !Time || !Time->IsFloatingPoint() || !Material) continue;
			UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Material->GetObjectPropertyValue_InContainer(Row));
			const int32 Required = Total->GetPropertyValue_InContainer(Row);
			if (Required <= 0 || Completed->GetPropertyValue_InContainer(Row) < Required || !MID) continue;
			FCompletedRowState& State = CompletedRows.AddDefaulted_GetRef();
			State.Panel = Panel;
			State.Name = Name->GetPropertyValue_InContainer(Row);
			State.Elapsed = Time->GetFloatingPointPropertyValue(Time->ContainerPtrToValuePtr<void>(Row));
			State.Progress = MID->K2_GetScalarParameterValue(TEXT("PaintProgress"));
		}
	}
	UE_MVVM_SET_PROPERTY_VALUE(Objectives, InObjectives);
	// 목록 바인딩이 모든 행의 완료 타이머를 초기화해도 기존 완료 행의 진행도는 이어지게 함
	const auto RestoreCompletedRows = [CompletedRows]()
	{
	for (const FCompletedRowState& State : CompletedRows)
	{
		UUserWidget* Panel = State.Panel.Get();
		UVerticalBox* List = Panel ? Cast<UVerticalBox>(Panel->GetWidgetFromName(TEXT("VerticalBox_Objectives"))) : nullptr;
		if (!List) continue;
		for (UWidget* Child : List->GetAllChildren())
		{
			UUserWidget* Row = Cast<UUserWidget>(Child);
			if (!Row) continue;
			const FTextProperty* Name = FindFProperty<FTextProperty>(Row->GetClass(), TEXT("ObjectiveText"));
			const FIntProperty* Completed = FindFProperty<FIntProperty>(Row->GetClass(), TEXT("CompletedCount"));
			const FIntProperty* Total = FindFProperty<FIntProperty>(Row->GetClass(), TEXT("TotalCount"));
			FNumericProperty* Time = FindFProperty<FNumericProperty>(Row->GetClass(), TEXT("CompletedPaintElapsed"));
			const FObjectPropertyBase* Material = FindFProperty<FObjectPropertyBase>(Row->GetClass(), TEXT("CompletedPaintMID"));
			if (!Name || !Completed || !Total || !Time || !Time->IsFloatingPoint() || !Material) continue;
			const int32 Required = Total->GetPropertyValue_InContainer(Row);
			if (Required <= 0 || Completed->GetPropertyValue_InContainer(Row) < Required
				|| !Name->GetPropertyValue_InContainer(Row).EqualTo(State.Name)) continue;
			const double Elapsed = FMath::Max(State.Elapsed,
				Time->GetFloatingPointPropertyValue(Time->ContainerPtrToValuePtr<void>(Row)));
			Time->SetFloatingPointPropertyValue(Time->ContainerPtrToValuePtr<void>(Row), Elapsed);
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Material->GetObjectPropertyValue_InContainer(Row)))
			{
				MID->SetScalarParameterValue(TEXT("PaintProgress"), FMath::Max(State.Progress,
					MID->K2_GetScalarParameterValue(TEXT("PaintProgress"))));
			}
		}
	}
	};
	RestoreCompletedRows();
	// 지연 MVVM 바인딩 이후에도 이미 완료된 행만 복원해 새 완료 행의 붓칠은 유지함
	if (UWorld* World = GetBoundWorld(); World && !CompletedRows.IsEmpty())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, RestoreCompletedRows));
	}
}

void UCWObjectiveViewModel::SetObjectives(
	const TArray<FCWStageObjective>& InObjectives
)
{
	// 서버 완료 조건을 유지하면서 본 작업 이후에만 정리 목표를 안내하기 위함
	const auto IsCleanupObjective = [](const FCWStageObjective& Objective)
	{
		const FString Name = Objective.DisplayName.ToString();
		return Name == TEXT("페인트 정리") || Name == TEXT("데칼 정리");
	};

	bool bHasCleanupObjective = false;
	bool bCleanupIncomplete = false;
	bool bAllMainObjectivesCompleted = true;
	for (const FCWStageObjective& Objective : InObjectives)
	{
		if (IsCleanupObjective(Objective))
		{
			bHasCleanupObjective = true;
			bCleanupIncomplete |= !Objective.IsCompleted();
		}
		else if (!Objective.IsCompleted())
		{
			bAllMainObjectivesCompleted = false;
		}
	}

	// 이미 정리까지 끝난 경우에는 불필요한 페이지 교체 없이 기존 클리어 흐름을 유지함
	const bool bShowCleanup = bHasCleanupObjective && bAllMainObjectivesCompleted
		&& (bCleanupIncomplete || bCleanupVisible);
	// 표시 중인 정리 항목은 완료 후에도 유지해야 기존 완료 붓칠이 재생될 수 있음
	TArray<FCWStageObjective> VisibleObjectives;
	VisibleObjectives.Reserve(InObjectives.Num());
	for (const FCWStageObjective& Objective : InObjectives)
	{
		if (!bHasCleanupObjective
			|| (!bShowCleanup && !IsCleanupObjective(Objective))
			|| (bShowCleanup && IsCleanupObjective(Objective)
				&& (!Objective.IsCompleted()
					|| Objectives.ContainsByPredicate([&Objective](const FCWStageObjective& Previous)
					{ return Previous.DisplayName.EqualTo(Objective.DisplayName); })
					|| PendingObjectives.ContainsByPredicate([&Objective](const FCWStageObjective& Previous)
					{ return Previous.DisplayName.EqualTo(Objective.DisplayName); }))))
		{
			VisibleObjectives.Add(Objective);
		}
	}

	if (bTransitioningObjectives && bShowCleanup)
	{
		PendingObjectives = VisibleObjectives;
		if (bTransitionListReplaced)
		{
			PublishObjectives(VisibleObjectives);
		}
		return;
	}

	const bool bStartTransition = bShowCleanup && !bCleanupVisible && !Objectives.IsEmpty();
	bCleanupVisible = bShowCleanup;
	if (bStartTransition && GetBoundWorld() && PrepareObjectiveInkLayer())
	{
		// 마지막 본 작업의 완료 붓칠이 지우기 전에 보이도록 최신 완료 값을 먼저 반영함
		TArray<FCWStageObjective> CompletedMainObjectives;
		for (const FCWStageObjective& Objective : InObjectives)
		{
			if (!IsCleanupObjective(Objective)) CompletedMainObjectives.Add(Objective);
		}
		PublishObjectives(CompletedMainObjectives);
		PendingObjectives = VisibleObjectives;
		bTransitioningObjectives = true;
		bTransitionListReplaced = false;
		ObjectiveTransitionStarted = GetBoundWorld()->GetTimeSeconds();
		GetBoundWorld()->GetTimerManager().SetTimer(ObjectiveTransitionTimer,
			this, &UCWObjectiveViewModel::UpdateObjectiveTransition, 1.f / 60.f, true);
		return;
	}
	StopObjectiveTransition();
	PublishObjectives(VisibleObjectives);
}

bool UCWObjectiveViewModel::PrepareObjectiveInkLayer()
{
	if (ObjectiveInkLayer.IsValid()) return true;
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GetBoundWorld(), Widgets, UUserWidget::StaticClass(), false);
	for (UUserWidget* Widget : Widgets)
	{
		if (!Widget || Widget->GetOwningPlayer() != GetOwningPlayerController() || !Widget->WidgetTree) continue;
		UVerticalBox* Entries = Cast<UVerticalBox>(Widget->WidgetTree->FindWidget(TEXT("VerticalBox_Objectives")));
		UVerticalBox* Parent = Entries ? Cast<UVerticalBox>(Entries->GetParent()) : nullptr;
		// 다른 형제의 순서를 바꾸지 않도록 확인된 단일 목록 컨테이너에만 마스크를 삽입함
		if (!Parent || Parent->GetChildrenCount() != 1) continue;
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/CrossWalk/UI/Clipboard/Materials/M_UI_PaperInkWipe.M_UI_PaperInkWipe"));
		if (!Material) return false;
		UVerticalBoxSlot* PreviousSlot = Cast<UVerticalBoxSlot>(Entries->Slot);
		if (!PreviousSlot) continue;
		const FMargin Padding = PreviousSlot->GetPadding();
		const FSlateChildSize Size = PreviousSlot->GetSize();
		const EHorizontalAlignment HAlign = PreviousSlot->GetHorizontalAlignment();
		const EVerticalAlignment VAlign = PreviousSlot->GetVerticalAlignment();
		const FVector2D OriginalExtent = Entries->GetCachedGeometry().GetLocalSize();
		if (OriginalExtent.X <= 0.f || OriginalExtent.Y <= 0.f) continue;
		URetainerBox* Layer = Widget->WidgetTree->ConstructWidget<URetainerBox>(URetainerBox::StaticClass());
		Layer->SetRenderingPhase(0, 1);
		Layer->SetTextureParameter(TEXT("Texture"));
		Layer->SetEffectMaterial(Material);
		// 목표가 한 줄로 줄어도 ScaleBox의 기준 크기가 바뀌어 글씨와 게이지가 축소되지 않도록 고정함
		USizeBox* StableBounds = Widget->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		StableBounds->SetWidthOverride(OriginalExtent.X);
		StableBounds->SetHeightOverride(OriginalExtent.Y);
		Entries->RemoveFromParent();
		USizeBoxSlot* ContentSlot = Cast<USizeBoxSlot>(StableBounds->AddChild(Entries));
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Top);
		Layer->SetContent(StableBounds);
		UVerticalBoxSlot* NewSlot = Parent->AddChildToVerticalBox(Layer);
		NewSlot->SetPadding(Padding);
		NewSlot->SetSize(Size);
		NewSlot->SetHorizontalAlignment(HAlign);
		NewSlot->SetVerticalAlignment(VAlign);
		Layer->SetRetainRendering(false);
		ObjectiveInkLayer = Layer;
		return true;
	}
	return false;
}

void UCWObjectiveViewModel::UpdateObjectiveTransition()
{
	UWorld* World = GetBoundWorld();
	URetainerBox* Layer = ObjectiveInkLayer.Get();
	if (!World || !Layer)
	{
		PublishObjectives(PendingObjectives);
		StopObjectiveTransition();
		return;
	}
	const float Elapsed = World->GetTimeSeconds() - ObjectiveTransitionStarted;
	if (Elapsed < 0.65f) return;
	Layer->SetRetainRendering(true);
	UMaterialInstanceDynamic* Mask = Layer->GetEffectMaterial();
	if (!Mask)
	{
		PublishObjectives(PendingObjectives);
		StopObjectiveTransition();
		return;
	}
	if (Elapsed < 1.25f)
	{
		Mask->SetScalarParameterValue(TEXT("Reveal"), 0.f);
		Mask->SetScalarParameterValue(TEXT("Progress"), FMath::SmoothStep(0.f, 1.f, (Elapsed - 0.65f) / 0.6f));
	}
	else
	{
		Mask->SetScalarParameterValue(TEXT("Reveal"), 1.f);
		Mask->SetScalarParameterValue(TEXT("Progress"), FMath::SmoothStep(0.f, 1.f, (Elapsed - 1.25f) / 0.8f));
		if (!bTransitionListReplaced)
		{
			bTransitionListReplaced = true;
			PublishObjectives(PendingObjectives);
		}
	}
	Layer->RequestRender();
	if (Elapsed >= 2.05f) StopObjectiveTransition();
}

void UCWObjectiveViewModel::StopObjectiveTransition()
{
	if (UWorld* World = GetBoundWorld()) World->GetTimerManager().ClearTimer(ObjectiveTransitionTimer);
	if (URetainerBox* Layer = ObjectiveInkLayer.Get()) Layer->SetRetainRendering(false);
	bTransitioningObjectives = false;
	bTransitionListReplaced = false;
	PendingObjectives.Reset();
}

void UCWObjectiveViewModel::SetIsStageCleared(
	bool bInIsStageCleared
)
{
	// 클리어 여부가 실제로 변경된 경우에만 MVVM 알림 발생
	// 결과 화면의 다음 선택 목록이 만들어지기 전에 로컬 해금을 기록하기 위함
	if (bInIsStageCleared && GetOwningPlayerController())
	{
		FCWLocalStageProgress::RecordClear(GetBoundWorld());
	}
	UE_MVVM_SET_PROPERTY_VALUE(
		bIsStageCleared,
		bInIsStageCleared
	);
}

#pragma endregion

#pragma region GameStateBinding

UClass* UCWObjectiveViewModel::GetGameStateClass() const
{
	return ACWGameState::StaticClass();
}

void UCWObjectiveViewModel::SubscribeToGameState(
	AGameStateBase* InGameState
)
{
	ACWGameState* GameState =
		Cast<ACWGameState>(InGameState);

	if (!IsValid(GameState))
	{
		return;
	}

	// 복제된 목표 목록이 변경될 때 알림 수신
	GameState->OnStageObjectivesChanged.AddDynamic(
		this,
		&UCWObjectiveViewModel::HandleStageObjectivesChanged
	);

	// 복제된 스테이지 상태가 변경될 때 알림 수신
	GameState->OnStageStateChanged.AddDynamic(
		this,
		&UCWObjectiveViewModel::HandleStageStateChanged
	);
}

void UCWObjectiveViewModel::UnsubscribeFromGameState(
	AGameStateBase* InGameState
)
{
	StopObjectiveTransition();
	ACWGameState* GameState =
		Cast<ACWGameState>(InGameState);

	if (!IsValid(GameState))
	{
		return;
	}

	GameState->OnStageObjectivesChanged.RemoveDynamic(
		this,
		&UCWObjectiveViewModel::HandleStageObjectivesChanged
	);

	GameState->OnStageStateChanged.RemoveDynamic(
		this,
		&UCWObjectiveViewModel::HandleStageStateChanged
	);
}

void UCWObjectiveViewModel::RefreshFromGameState()
{
	ACWGameState* GameState =
		Cast<ACWGameState>(GetBoundGameState());

	if (!IsValid(GameState))
	{
		return;
	}

	// OnRep가 이미 지난 경우를 위해 현재 값을 즉시 반영
	SetObjectives(GameState->GetObjectives());
	SetIsStageCleared(GameState->IsStageCleared());
}

void UCWObjectiveViewModel::ResetViewModelState()
{
	StopObjectiveTransition();
	ObjectiveInkLayer.Reset();
	bCleanupVisible = false;
	// 이전 월드의 목표가 다음 월드에 남지 않도록 초기화
	SetObjectives(TArray<FCWStageObjective>());

	// 이전 스테이지의 클리어 상태가 다음 월드에 남지 않도록 초기화
	SetIsStageCleared(false);
}

void UCWObjectiveViewModel::HandleStageObjectivesChanged(
	ACWGameState* InGameState
)
{
	if (!IsValid(InGameState))
	{
		return;
	}

	// 서버에서 갱신·복제된 최신 목표 배열 저장
	SetObjectives(InGameState->GetObjectives());
}

void UCWObjectiveViewModel::HandleStageStateChanged(
	ACWGameState* InGameState,
	ECWStageState InStageState
)
{
	if (!IsValid(InGameState))
	{
		return;
	}

	// GameMode가 확정하고 복제한 스테이지 상태를 그대로 반영
	SetIsStageCleared(
		InStageState == ECWStageState::Cleared
	);
}

#pragma endregion
