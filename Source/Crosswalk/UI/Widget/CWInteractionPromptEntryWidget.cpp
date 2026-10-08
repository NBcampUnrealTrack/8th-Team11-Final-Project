// CWInteractionPromptEntryWidget.cpp

#include "UI/Widget/CWInteractionPromptEntryWidget.h"

#include "Components/TextBlock.h"
#include "UI/CWUILog.h"

#define LOCTEXT_NAMESPACE "CWInteractionPrompt"

UCWInteractionPromptEntryWidget::UCWInteractionPromptEntryWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 안내판이라 입력을 가로채지 않음
	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	// 2번 파트가 제안한 문구를 기본값으로 두고 WBP 에서 덮어쓰게 함
	ActionLabels = {
		{ ECWInteractionPromptAction::PickUpTool, LOCTEXT("PickUpTool", "집기") },
		{ ECWInteractionPromptAction::DropTool, LOCTEXT("DropTool", "내려놓기") },
		{ ECWInteractionPromptAction::Interact, LOCTEXT("Interact", "상호작용") },
		{ ECWInteractionPromptAction::PickUpCan, LOCTEXT("PickUpCan", "통 꺼내기") },
		{ ECWInteractionPromptAction::ChargeTool, LOCTEXT("ChargeTool", "페인트 충전") },
		{ ECWInteractionPromptAction::ReturnCan, LOCTEXT("ReturnCan", "통 반납") },
		{ ECWInteractionPromptAction::ReloadAsphaltGun, LOCTEXT("ReloadAsphaltGun", "재장전") }
	};
}

void UCWInteractionPromptEntryWidget::SetPrompts(const TArray<FCWInteractionPrompt>& InPrompts)
{
	Lines.Reset();

	for (const FCWInteractionPrompt& Prompt : InPrompts)
	{
		FCWInteractionPromptEntryLine Line;
		if (!BuildLine(Prompt, Line))
		{
			continue;
		}

		if (Lines.Num() >= MaxLines)
		{
			// 고정 행을 넘으면 조용히 사라지므로 계약이 바뀐 신호로 남김
			if (!bWarnedTooManyLines)
			{
				bWarnedTooManyLines = true;
				UE_LOG(LogCWUI, Warning,
					TEXT("[InteractionPromptUI] More than %d lines on one target"), MaxLines);
			}
			break;
		}

		Lines.Add(MoveTemp(Line));
	}

	ApplyRow(Row_Primary, Box_PrimaryKey, Text_PrimaryKey, Text_PrimaryAction,
		Lines.IsValidIndex(0) ? &Lines[0] : nullptr);
	ApplyRow(Row_Secondary, Box_SecondaryKey, Text_SecondaryKey, Text_SecondaryAction,
		Lines.IsValidIndex(1) ? &Lines[1] : nullptr);
}

bool UCWInteractionPromptEntryWidget::BuildLine(const FCWInteractionPrompt& Prompt,
	FCWInteractionPromptEntryLine& OutLine) const
{
	if (!Prompt.IsValidPrompt())
	{
		return false;
	}

	const FText* Label = ActionLabels.Find(Prompt.Action);
	if (!Label || Label->IsEmpty())
	{
		// 늘어난 enum 항목이 빈 줄로 그려지지 않게 함
		if (!bWarnedMissingLabel)
		{
			bWarnedMissingLabel = true;
			UE_LOG(LogCWUI, Warning,
				TEXT("[InteractionPromptUI] Missing action label: %d"), static_cast<int32>(Prompt.Action));
		}
		return false;
	}

	OutLine.Key = Prompt.Key;
	OutLine.Label = *Label;

	// 매핑을 못 찾으면 EKeys::Invalid 가 오므로 키 칸 자체를 접음
	OutLine.bKeyShown = Prompt.Key.IsValid();

	return true;
}

void UCWInteractionPromptEntryWidget::ApplyRow(UWidget* Row, UWidget* KeyBox, UTextBlock* KeyText,
	UTextBlock* ActionText, const FCWInteractionPromptEntryLine* Line) const
{
	if (IsValid(Row))
	{
		Row->SetVisibility(Line ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (!Line)
	{
		return;
	}

	// 배경이 바인딩되지 않은 디자인에서는 글씨만 접어 최소한 키를 만들어 내지 않음
	UWidget* KeyTarget = IsValid(KeyBox) ? KeyBox : Cast<UWidget>(KeyText);
	if (IsValid(KeyTarget))
	{
		KeyTarget->SetVisibility(Line->bKeyShown
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (IsValid(KeyText))
	{
		KeyText->SetText(Line->Key.GetDisplayName());
	}

	if (IsValid(ActionText))
	{
		ActionText->SetText(Line->Label);
	}
}

#undef LOCTEXT_NAMESPACE
