#include "UI/Room/CWRoomScreenWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SceneComponent.h"
#include "Components/TextBlock.h"
#include "Core/CWPlayerState.h"
#include "Core/Room/CWRoomGameState.h"
#include "EngineUtils.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "UObject/UnrealType.h"

void UCWRoomScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	CreateSeatLabels();
}

void UCWRoomScreenWidget::CreateSeatLabels()
{
	if (!WidgetTree || SeatLabels.Num() != 0)
	{
		return;
	}
	SeatCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (!SeatCanvas)
	{
		return;
	}
	const UTextBlock* Title = Cast<UTextBlock>(GetWidgetFromName(TEXT("Text_RoomTitle")));
	FSlateFontInfo Font = Title ? Title->GetFont() : FSlateFontInfo();
	Font.Size = 22;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UBorder* Label = WidgetTree->ConstructWidget<UBorder>();
		UTextBlock* Name = WidgetTree->ConstructWidget<UTextBlock>();
		UTextBlock* Check = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetBrushColor(FLinearColor(0.76f, 0.69f, 0.48f, 0.90f));
		Label->SetPadding(FMargin(12.f, 3.f));
		Label->SetContent(Name);
		Name->SetFont(Font);
		Name->SetColorAndOpacity(FSlateColor(FLinearColor(0.035f, 0.03f, 0.025f)));
		Name->SetJustification(ETextJustify::Center);
		FSlateFontInfo CheckFont = Font;
		CheckFont.Size = FMath::Clamp(ReadyCheckFontSize, 8, 100);
		Check->SetFont(CheckFont);
		Check->SetText(FText::FromString(TEXT("v")));
		Check->SetColorAndOpacity(FSlateColor(FLinearColor(0.04f, 0.28f, 0.16f)));
		Check->SetJustification(ETextJustify::Center);
		UCanvasPanelSlot* LabelSlot = SeatCanvas->AddChildToCanvas(Label);
		LabelSlot->SetSize(FVector2D(230.f, 40.f));
		LabelSlot->SetAlignment(FVector2D(0.5f, 1.f));
		LabelSlot->SetZOrder(5);
		UCanvasPanelSlot* CheckSlot = SeatCanvas->AddChildToCanvas(Check);
		CheckSlot->SetSize(FVector2D(CheckFont.Size + 6.f, CheckFont.Size + 18.f));
		CheckSlot->SetAlignment(FVector2D(0.5f, 1.f));
		CheckSlot->SetZOrder(6);
		Label->SetVisibility(ESlateVisibility::Hidden);
		Check->SetVisibility(ESlateVisibility::Hidden);
		SeatLabels.Add(Label);
		SeatNames.Add(Name);
		SeatChecks.Add(Check);
	}
}

void UCWRoomScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	SearchCooldown -= InDeltaTime;
	if (!Presentation.IsValid() && SearchCooldown <= 0.f)
	{
		SearchCooldown = 0.5f;
		// 연출 BP를 수정하지 않고 기존 복제 좌석 계약만 읽기 위한 조회임
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->GetClass()->GetPathName() == TEXT("/Game/CrossWalk/Maps/Title/Lobby/BP_LobbyPresentation.BP_LobbyPresentation_C"))
			{
				Presentation = *It;
				break;
			}
		}
	}
	UpdateSeatLabels(MyGeometry);
}

void UCWRoomScreenWidget::UpdateSeatLabels(const FGeometry& MyGeometry)
{
	AActor* Actor = Presentation.Get();
	const ACWRoomGameState* Room = GetWorld() ? GetWorld()->GetGameState<ACWRoomGameState>() : nullptr;
	FUniqueNetIdPtr HostId;
	if (IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get())
	{
		const IOnlineSessionPtr Sessions = OnlineSubsystem->GetSessionInterface();
		const FNamedOnlineSession* Session = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
		if (Session)
		{
			HostId = Session->OwningUserId;
		}
	}
	for (int32 Index = 0; Index < SeatLabels.Num(); ++Index)
	{
		SeatLabels[Index]->SetVisibility(ESlateVisibility::Hidden);
		SeatChecks[Index]->SetVisibility(ESlateVisibility::Hidden);
		if (!Actor || !Room || !GetOwningPlayer())
		{
			continue;
		}
		const FIntProperty* OwnerProperty = FindFProperty<FIntProperty>(Actor->GetClass(), *FString::Printf(TEXT("SeatOwnerId%d"), Index));
		const FObjectPropertyBase* MeshProperty = FindFProperty<FObjectPropertyBase>(Actor->GetClass(), *FString::Printf(TEXT("Passenger%d"), Index));
		const USceneComponent* Mesh = MeshProperty ? Cast<USceneComponent>(MeshProperty->GetObjectPropertyValue_InContainer(Actor)) : nullptr;
		if (!OwnerProperty || !Mesh)
		{
			continue;
		}
		const int32 PlayerId = OwnerProperty->GetPropertyValue_InContainer(Actor);
		const ACWPlayerState* Player = nullptr;
		for (const APlayerState* Candidate : Room->PlayerArray)
		{
			if (Candidate && Candidate->GetPlayerId() == PlayerId)
			{
				Player = Cast<ACWPlayerState>(Candidate);
				break;
			}
		}
		FVector2D Position;
		if (!Player || !UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(), Mesh->GetComponentLocation() + FVector(0.f, 0.f, 105.f), Position, false))
		{
			continue;
		}
		// DPI와 화면 루트의 위치를 함께 보정해야 창 크기 변경에도 머리 위를 유지함
		Position = MyGeometry.AbsoluteToLocal(UWidgetLayoutLibrary::GetViewportWidgetGeometry(this).LocalToAbsolute(Position));
		FString DisplayName = Player->GetPlayerName();
		if (DisplayName.IsEmpty())
		{
			DisplayName = FString::Printf(TEXT("플레이어 %d"), Index + 1);
		}
		if (DisplayName.Len() > 15)
		{
			DisplayName = DisplayName.Left(13) + TEXT("...");
		}
		SeatNames[Index]->SetText(FText::FromString(DisplayName));
		CastChecked<UCanvasPanelSlot>(SeatLabels[Index]->Slot)->SetPosition(Position);
		const float CheckOffset = Position.X < MyGeometry.GetLocalSize().X * 0.5f ? -135.f : 135.f;
		CastChecked<UCanvasPanelSlot>(SeatChecks[Index]->Slot)->SetPosition(Position + FVector2D(CheckOffset, 0.f));
		SeatLabels[Index]->SetVisibility(ESlateVisibility::HitTestInvisible);
		// 온라인 ID가 없는 로컬 테스트에서는 호스트가 먼저 배정되는 서버 0번 좌석을 사용함
		const bool bIsHost = HostId.IsValid() && Player->GetUniqueId().IsValid()
			? *HostId == *Player->GetUniqueId().GetUniqueNetId() : Index == 0;
		SeatChecks[Index]->SetVisibility(Player->IsReady() && !bIsHost ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}

void UCWRoomScreenWidget::NativeDestruct()
{
	for (UBorder* Label : SeatLabels)
	{
		Label->RemoveFromParent();
	}
	for (UTextBlock* Check : SeatChecks)
	{
		Check->RemoveFromParent();
	}
	SeatLabels.Reset();
	SeatNames.Reset();
	SeatChecks.Reset();
	SeatCanvas = nullptr;
	Presentation.Reset();
	SearchCooldown = 0.f;
	Super::NativeDestruct();
}
