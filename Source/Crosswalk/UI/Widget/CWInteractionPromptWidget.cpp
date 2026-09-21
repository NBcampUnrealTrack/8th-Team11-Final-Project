// CWInteractionPromptWidget.cpp

#include "UI/Widget/CWInteractionPromptWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/MeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/Components/CWInteractionPromptComponent.h"
#include "UI/Core/CWUISubsystem.h"
#include "UI/CWUILog.h"
#include "UI/Widget/CWInteractionPromptEntryWidget.h"

UCWInteractionPromptWidget::UCWInteractionPromptWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 안내판이라 입력을 가로채지 않음
	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	// 손에 든 도구를 내려놓는 조작은 이미 알고 있어 안내가 소음이 됨
	HiddenActions = { ECWInteractionPromptAction::DropTool };
}

TSharedRef<SWidget> UCWInteractionPromptWidget::RebuildWidget()
{
	// WBP 로 만든 경우 디자이너의 캔버스가 이미 잡혀 있음
	// 자동 테스트처럼 C++ 클래스만으로 만든 경우에만 대신 만들어 둠
	if (WidgetTree && !Canvas_InteractionPrompts)
	{
		Canvas_InteractionPrompts = WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), TEXT("Canvas_InteractionPrompts"));
		WidgetTree->RootWidget = Canvas_InteractionPrompts;
		Canvas_InteractionPrompts->SetClipping(EWidgetClipping::ClipToBounds);
	}
	return Super::RebuildWidget();
}

void UCWInteractionPromptWidget::NativeConstruct()
{
	Super::NativeConstruct();

	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC) || !PC->IsLocalController())
	{
		return;
	}

	BoundController = PC;
	PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &UCWInteractionPromptWidget::HandlePawnChanged);

	// 소유 폰이 이미 복제된 경우에도 첫 프롬프트를 놓치지 않도록 즉시 조회함
	HandlePawnChanged(nullptr, PC->GetPawn());
}

void UCWInteractionPromptWidget::NativeDestruct()
{
	if (APlayerController* PC = BoundController.Get())
	{
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &UCWInteractionPromptWidget::HandlePawnChanged);
	}
	BoundController.Reset();
	UnbindPrompts();
	RefreshGroups({});
	Super::NativeDestruct();
}

void UCWInteractionPromptWidget::UnbindPrompts()
{
	if (UCWInteractionPromptComponent* Source = PromptSource.Get())
	{
		Source->OnInteractionPromptsChanged.RemoveDynamic(this, &UCWInteractionPromptWidget::HandlePromptsChanged);
	}
	PromptSource.Reset();
}

void UCWInteractionPromptWidget::HandlePawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	UnbindPrompts();

	UCWInteractionPromptComponent* Source = IsValid(NewPawn)
		? NewPawn->FindComponentByClass<UCWInteractionPromptComponent>() : nullptr;
	if (!IsValid(Source))
	{
		RefreshGroups({});
		return;
	}

	PromptSource = Source;

	// 구독 전에 발행된 값도 표시하되 구독과 조회 사이의 변경을 놓치지 않도록 함
	Source->OnInteractionPromptsChanged.AddUniqueDynamic(this, &UCWInteractionPromptWidget::HandlePromptsChanged);
	RefreshGroups(Source->GetActivePrompts());
}

void UCWInteractionPromptWidget::HandlePromptsChanged(UCWInteractionPromptComponent* Component,
	const TArray<FCWInteractionPrompt>& Prompts)
{
	if (Component == PromptSource.Get())
	{
		RefreshGroups(Prompts);
	}
}

void UCWInteractionPromptWidget::RefreshGroups(const TArray<FCWInteractionPrompt>& Prompts)
{
	if (!Canvas_InteractionPrompts)
	{
		return;
	}

	// 문구는 변경 통보에서만 구성하며 매 프레임에는 위치만 갱신함
	Canvas_InteractionPrompts->ClearChildren();
	Groups.Reset();

	if (Prompts.IsEmpty())
	{
		return;
	}

	if (!EntryWidgetClass)
	{
		// 안내판 클래스가 없으면 아무것도 못 그리므로 조용히 비지 않게 함
		UE_LOG(LogCWUI, Warning, TEXT("[InteractionPromptUI] EntryWidgetClass is not set."));
		return;
	}

	// 같은 액터의 프롬프트를 한 장에 모음 - 박스 앞에서 E 와 R 이 함께 뜨는 것이 기본 화면임
	TArray<TWeakObjectPtr<AActor>> Targets;
	TArray<TArray<FCWInteractionPrompt>> Grouped;
	for (const FCWInteractionPrompt& Prompt : Prompts)
	{
		if (!IsValid(Prompt.TargetActor) || !Prompt.IsValidPrompt())
		{
			continue;
		}

		// 안내에서 뺄 동작은 묶기 전에 걸러 빈 종이가 생기지 않게 함
		if (HiddenActions.Contains(Prompt.Action))
		{
			continue;
		}

		const int32 Index = Targets.IndexOfByPredicate([&Prompt](const TWeakObjectPtr<AActor>& Item)
		{
			return Item.Get() == Prompt.TargetActor.Get();
		});

		if (Index == INDEX_NONE)
		{
			Targets.Add(Prompt.TargetActor);
			Grouped.AddDefaulted_GetRef().Add(Prompt);
		}
		else
		{
			Grouped[Index].Add(Prompt);
		}
	}

	for (int32 Index = 0; Index < Targets.Num(); ++Index)
	{
		// 소유 위젯으로 만들어 플레이어 문맥을 물려줌
		// 로컬 플레이어가 아직 없는 경로에서도 월드를 타고 생성됨
		UCWInteractionPromptEntryWidget* Entry =
			CreateWidget<UCWInteractionPromptEntryWidget>(this, EntryWidgetClass);
		if (!IsValid(Entry))
		{
			continue;
		}

		Entry->SetPrompts(Grouped[Index]);

		// 투영 전 원점에서 한 프레임 보이는 것을 막음
		Entry->SetVisibility(ESlateVisibility::Hidden);

		UCanvasPanelSlot* CanvasSlot = Canvas_InteractionPrompts->AddChildToCanvas(Entry);
		CanvasSlot->SetAutoSize(true);
		CanvasSlot->SetAlignment(FVector2D(0.f, 0.5f));

		FCWInteractionPromptGroup& Group = Groups.AddDefaulted_GetRef();
		Group.Target = Targets[Index];
		Group.Entry = Entry;
	}
}

FVector UCWInteractionPromptWidget::GetTargetAnchor(AActor* Target) const
{
	// 상호작용 감지 구체가 아닌 보이는 메시 위를 기준으로 삼음
	FBox Bounds(ForceInit);
	TInlineComponentArray<UMeshComponent*> Meshes(Target);
	for (const UMeshComponent* Mesh : Meshes)
	{
		if (IsValid(Mesh) && Mesh->IsRegistered() && Mesh->IsVisible())
		{
			Bounds += Mesh->Bounds.GetBox();
		}
	}

	// 위가 아니라 옆에 붙이므로 꼭대기가 아닌 세로 중심이 기준임
	FVector Anchor = Bounds.IsValid ? Bounds.GetCenter() : Target->GetActorLocation();
	Anchor.Z += AnchorHeightOffset;
	return Anchor;
}

bool UCWInteractionPromptWidget::IsHeldByOwner(const AActor* Target) const
{
	const AActor* PromptOwner = PromptSource.IsValid() ? PromptSource->GetOwner() : nullptr;
	return Target != nullptr && PromptOwner != nullptr && Target->IsAttachedTo(PromptOwner);
}

void UCWInteractionPromptWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (Groups.IsEmpty())
	{
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	ULocalPlayer* Player = IsValid(PC) ? PC->GetLocalPlayer() : nullptr;
	const UCWUISubsystem* UI = Player ? Player->GetSubsystem<UCWUISubsystem>() : nullptr;
	const bool bCanDisplay = IsValid(PC) && PC->IsLocalController() && PromptSource.IsValid()
		&& PC->GetPawn() == PromptSource->GetOwner() && UI && UI->CanShowInteractionPrompts();
	const FGeometry PlayerGeometry = bCanDisplay
		? UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(PC) : FGeometry();

	for (FCWInteractionPromptGroup& Group : Groups)
	{
		if (!IsValid(Group.Entry))
		{
			continue;
		}

		AActor* Target = Group.Target.Get();
		FVector2D Position = FVector2D::ZeroVector;
		// 집는 순간 도구가 손에 붙지만 프롬프트 갱신은 0.05초 주기이고
		// 클라이언트는 서버 왕복까지 기다려 그 사이 낡은 안내판이 손으로 끌려감
		// 문구 갱신을 기다리지 않도록 매 프레임 붙음 여부로 끊음
		bool bVisible = bCanDisplay && IsValid(Target) && !Target->IsHidden() && !IsHeldByOwner(Target);
		if (bVisible)
		{
			bVisible = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, GetTargetAnchor(Target), Position, true);
			if (bVisible)
			{
				// DPI와 플레이어 화면 원점을 반영해 HUD 로컬 좌표로 옮김
				Position = MyGeometry.AbsoluteToLocal(PlayerGeometry.LocalToAbsolute(Position));

				// 대상을 가리지 않도록 화면에서 옆으로 띄움
				Position += ScreenOffset;

				const FVector2D Size = MyGeometry.GetLocalSize();
				bVisible = Position.X >= 0.f && Position.Y >= 0.f && Position.X <= Size.X && Position.Y <= Size.Y;
			}
		}

		Group.Entry->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		if (bVisible)
		{
			CastChecked<UCanvasPanelSlot>(Group.Entry->Slot)->SetPosition(Position);
		}
	}
}
