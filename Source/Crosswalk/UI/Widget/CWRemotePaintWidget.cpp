#include "UI/Widget/CWRemotePaintWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Characters/CWPlayerCharacter.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CapsuleComponent.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Painting/Components/CWToolHolderComponent.h"
#include "Painting/Tools/CWBaseTool.h"
#include "Sinkhole/Components/CWSinkholeFallComponent.h"
#include "UI/Core/CWUISubsystem.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "UI/CWUILog.h"

UCWRemotePaintWidget::UCWRemotePaintWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	static ConstructorHelpers::FClassFinder<UUserWidget> Style(
		TEXT("/Game/CrossWalk/UI/HUD/PlayerStatus/WBP_PlayerStatusEntry"));
	StatusStyleClass = Style.Class;
}

TSharedRef<SWidget> UCWRemotePaintWidget::RebuildWidget()
{
	if (WidgetTree && !Canvas)
	{
		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		Canvas->SetClipping(EWidgetClipping::ClipToBounds);
		WidgetTree->RootWidget = Canvas;
	}
	return Super::RebuildWidget();
}

void UCWRemotePaintWidget::AddPlayer(ACWPlayerCharacter* Character)
{
	UUserWidget* Status = StatusStyleClass ? CreateWidget<UUserWidget>(GetOwningPlayer(), StatusStyleClass) : nullptr;
	if (!Status || !Status->GetWidgetFromName(TEXT("Image_Portrait"))
		|| !Status->GetWidgetFromName(TEXT("WBP_ToolPaintGauge_Paint")))
	{
		if (!bWarnedInvalidStatus)
		{
			bWarnedInvalidStatus = true;
			UE_LOG(LogCWUI, Error, TEXT("Remote paint: player status icon/gauge configuration is invalid."));
		}
		return;
	}
	FCWRemotePaintEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Character = Character;
	Entry.Status = Status;
	Status->SetVisibility(ESlateVisibility::Hidden);
	UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Status);
	PanelSlot->SetSize(FVector2D(174.f, 24.f));
	// 아이콘을 포함한 전체 폭 대신 잔량 바 중앙을 발밑에 맞춰 오른쪽 쏠림을 방지함
	PanelSlot->SetAlignment(FVector2D((30.f + 140.f * 0.5f) / 174.f, 0.f));
	// Construct에서 상태창 배치가 적용된 뒤 발밑 전용 배치만 덮어쓰도록 함
	Status->TakeWidget();
	ApplyToolOnlyLayout(Status);
}

bool UCWRemotePaintWidget::BindPlayerState(UUserWidget* Status, APlayerState* PlayerState) const
{
	UFunction* Function = Status->FindFunction(TEXT("SetPlayerState"));
	const FObjectPropertyBase* Input = Function ? FindFProperty<FObjectPropertyBase>(Function, TEXT("InPlayerState")) : nullptr;
	if (!Input || !Input->HasAnyPropertyFlags(CPF_Parm) || Input->HasAnyPropertyFlags(CPF_OutParm)
		|| (PlayerState && !PlayerState->IsA(Input->PropertyClass)))
	{
		if (!bWarnedBindFailure)
		{
			bWarnedBindFailure = true;
			UE_LOG(LogCWUI, Error, TEXT("Remote paint: SetPlayerState signature is invalid."));
		}
		return false;
	}
	// BP 파라미터의 실제 레이아웃을 사용해 클래스 변경 시 잘못된 메모리를 읽지 않게 함
	FStructOnScope Parameters(Function);
	Input->SetObjectPropertyValue_InContainer(Parameters.GetStructMemory(), PlayerState);
	Status->ProcessEvent(Function, Parameters.GetStructMemory());
	return true;
}

void UCWRemotePaintWidget::ApplyToolOnlyLayout(UUserWidget* Status) const
{
	for (const TCHAR* Name : { TEXT("Paint_PlayerBackground"), TEXT("Image_CardShadow"),
		TEXT("Image_TemperatureColumn"), TEXT("Image_NameRule"), TEXT("HorizontalBox_StatusEffects"),
		TEXT("ScaleBox_PlayerName"), TEXT("Temperature_Remote") })
	{
		if (UWidget* Extra = Status->GetWidgetFromName(Name))
		{
			Extra->SetRenderOpacity(0.f);
		}
	}
	if (UBorder* Background = Cast<UBorder>(Status->GetWidgetFromName(TEXT("Border_Background"))))
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
		Background->SetBrush(Brush);
		Background->SetPadding(FMargin(0.f));
		Background->SetHorizontalAlignment(HAlign_Fill);
		Background->SetVerticalAlignment(VAlign_Fill);
	}
	if (USizeBox* Root = Cast<USizeBox>(Status->GetWidgetFromName(TEXT("SizeBox_Root"))))
	{
		Root->SetWidthOverride(174.f);
		Root->SetHeightOverride(24.f);
	}
	UWidget* Icon = Status->GetWidgetFromName(TEXT("Image_Portrait"));
	UWidget* Gauge = Status->GetWidgetFromName(TEXT("WBP_ToolPaintGauge_Paint"));
	if (Icon)
	{
		Icon->SetRenderTranslation(FVector2D::ZeroVector);
		if (UCanvasPanelSlot* IconSlot = Cast<UCanvasPanelSlot>(Icon->Slot))
		{
			// 상태창 원본의 앵커와 정렬값이 발밑 표시 중심을 밀지 않도록 초기화함
			IconSlot->SetAnchors(FAnchors(0.f));
			IconSlot->SetAlignment(FVector2D::ZeroVector);
			IconSlot->SetAutoSize(false);
			IconSlot->SetPosition(FVector2D::ZeroVector);
			IconSlot->SetSize(FVector2D(24.f, 24.f));
		}
	}
	if (Gauge)
	{
		Gauge->SetRenderTranslation(FVector2D::ZeroVector);
		if (UUserWidget* GaugeWidget = Cast<UUserWidget>(Gauge))
		{
			if (USizeBox* GaugeRoot = Cast<USizeBox>(GaugeWidget->GetRootWidget()))
			{
				GaugeRoot->SetWidthOverride(140.f);
				GaugeRoot->SetHeightOverride(22.f);
			}
		}
		if (UCanvasPanelSlot* GaugeSlot = Cast<UCanvasPanelSlot>(Gauge->Slot))
		{
			GaugeSlot->SetAnchors(FAnchors(0.f));
			GaugeSlot->SetAlignment(FVector2D::ZeroVector);
			GaugeSlot->SetAutoSize(false);
			GaugeSlot->SetPosition(FVector2D(30.f, 1.f));
			GaugeSlot->SetSize(FVector2D(140.f, 22.f));
		}
	}
}

void UCWRemotePaintWidget::RefreshPlayers()
{
	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		if (!Entries[Index].Character.IsValid())
		{
			Entries[Index].Status->RemoveFromParent();
			Entries.RemoveAt(Index);
		}
	}
	for (TActorIterator<ACWPlayerCharacter> It(GetWorld()); It; ++It)
	{
		ACWPlayerCharacter* Character = *It;
		if (Character->IsLocallyControlled() || !Character->GetPlayerState())
		{
			continue;
		}
		if (!Entries.ContainsByPredicate([Character](const FCWRemotePaintEntry& Entry)
		{
			return Entry.Character.Get() == Character;
		}))
		{
			AddPlayer(Character);
		}
	}
}

void UCWRemotePaintWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!Canvas || !GetWorld())
	{
		return;
	}
	PlayerRefreshRemaining -= InDeltaTime;
	if (PlayerRefreshRemaining <= 0.f)
	{
		RefreshPlayers();
		PlayerRefreshRemaining = 0.25f;
	}
	APlayerController* PC = GetOwningPlayer();
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	const UCWUISubsystem* UI = LocalPlayer ? LocalPlayer->GetSubsystem<UCWUISubsystem>() : nullptr;
	const bool bCanDisplay = IsValid(PC) && PC->IsLocalController() && UI && UI->CanShowInteractionPrompts();
	const FGeometry PlayerGeometry = bCanDisplay ? UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(PC) : FGeometry();
	for (FCWRemotePaintEntry& Entry : Entries)
	{
		ACWPlayerCharacter* Character = Entry.Character.Get();
		UCWToolHolderComponent* Holder = Character ? Character->FindComponentByClass<UCWToolHolderComponent>() : nullptr;
		ACWBaseTool* Tool = Holder ? Holder->GetHeldTool() : nullptr;
		const UCWSinkholeFallComponent* Fall = Character ? Character->FindComponentByClass<UCWSinkholeFallComponent>() : nullptr;
		bool bVisible = bCanDisplay && IsValid(Character) && !Character->IsLocallyControlled()
			&& IsValid(Character->GetPlayerState()) && !Character->IsHidden()
			&& !(Fall && Fall->IsFallLocked()) && IsValid(Tool) && Tool->UsesPaint();
		FVector2D Position = FVector2D::ZeroVector;
		if (bVisible)
		{
			// 손에 붙은 도구 위치 대신 발을 기준으로 삼아 작업 자세가 바뀌어도 흔들리지 않음
			FVector Foot = Character->GetActorLocation();
			Foot.Z -= Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			bVisible = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Foot, Position, true);
			if (bVisible)
			{
				Position = MyGeometry.AbsoluteToLocal(PlayerGeometry.LocalToAbsolute(Position));
				Position.Y += 8.f;
				const FVector2D Size = MyGeometry.GetLocalSize();
				bVisible = Position.X >= 100.f && Position.X <= Size.X - 74.f
					&& Position.Y >= 0.f && Position.Y + 24.f <= Size.Y;
			}
		}
		Entry.Status->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		if (!bVisible)
		{
			continue;
		}
		CastChecked<UCanvasPanelSlot>(Entry.Status->Slot)->SetPosition(Position);
		APlayerState* PlayerState = Character->GetPlayerState();
		if (Entry.BoundPlayerState.Get() != PlayerState || Entry.LastTool.Get() != Tool)
		{
			if (!BindPlayerState(Entry.Status, PlayerState))
			{
				Entry.Status->SetVisibility(ESlateVisibility::Hidden);
				continue;
			}
			Entry.BoundPlayerState = PlayerState;
			Entry.LastTool = Tool;
		}
		ApplyToolOnlyLayout(Entry.Status);
	}
}

void UCWRemotePaintWidget::NativeDestruct()
{
	if (Canvas)
	{
		Canvas->ClearChildren();
	}
	Entries.Reset();
	PlayerRefreshRemaining = 0.f;
	Super::NativeDestruct();
}
