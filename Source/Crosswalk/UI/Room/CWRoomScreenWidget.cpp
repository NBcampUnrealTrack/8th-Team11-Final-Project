#include "UI/Room/CWRoomScreenWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SceneComponent.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "UObject/ConstructorHelpers.h"
#include "Core/CWPlayerState.h"
#include "Core/Room/CWRoomGameState.h"
#include "EngineUtils.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "UObject/UnrealType.h"

UCWRoomScreenWidget::UCWRoomScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 기존 종이를 하드 참조하여 패키징에서도 이름표 질감이 포함되도록 함
	static ConstructorHelpers::FObjectFinder<UTexture2D> Paper(TEXT("/Game/CrossWalk/UI/Textures/ClipboardPaintV1/T_UI_ClipboardPaper_V1.T_UI_ClipboardPaper_V1"));
	NameplatePaper = Paper.Object;
}

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
		UBorder* Stamp = WidgetTree->ConstructWidget<UBorder>();
		FSlateBrush PaperBrush;
		PaperBrush.SetResourceObject(NameplatePaper);
		PaperBrush.DrawAs = ESlateBrushDrawType::Box;
		PaperBrush.Margin = FMargin(0.12f);
		Label->SetBrush(PaperBrush);
		Label->SetBrushColor(FLinearColor(1.f, 0.97f, 0.87f, 1.f));
		Label->SetPadding(FMargin(14.f, 5.f));
		Label->SetContent(Name);
		Name->SetFont(Font);
		Name->SetColorAndOpacity(FSlateColor(FLinearColor(0.035f, 0.03f, 0.025f)));
		Name->SetJustification(ETextJustify::Center);
		Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		Name->SetClipping(EWidgetClipping::ClipToBounds);
		FSlateFontInfo CheckFont = Font;
		CheckFont.Size = FMath::Clamp(ReadyCheckFontSize * 2 / 5 - 1, 13, 23);
		Check->SetFont(CheckFont);
		Check->SetText(FText::FromString(TEXT("v 준비 완료")));
		const FLinearColor StampGreen(0.12f, 0.25f, 0.13f, 1.f);
		Check->SetColorAndOpacity(FSlateColor(StampGreen));
		Check->SetJustification(ETextJustify::Center);
		// 손글씨 폰트의 하단 기준선 때문에 도장 아래로 치우쳐 보이는 부분을 보정함
		FWidgetTransform CheckTransform;
		CheckTransform.Translation = FVector2D(0.f, -3.f);
		Check->SetRenderTransform(CheckTransform);
		Stamp->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.91f, 0.88f, 0.70f, 0.95f), 5.f, StampGreen, 1.5f));
		Stamp->SetPadding(FMargin(8.f, 1.f));
		Stamp->SetHorizontalAlignment(HAlign_Fill);
		Stamp->SetVerticalAlignment(VAlign_Center);
		Stamp->SetContent(Check);
		UCanvasPanelSlot* LabelSlot = SeatCanvas->AddChildToCanvas(Label);
		LabelSlot->SetSize(FVector2D(230.f, 40.f));
		LabelSlot->SetAlignment(FVector2D(0.5f, 1.f));
		LabelSlot->SetZOrder(5);
		UCanvasPanelSlot* CheckSlot = SeatCanvas->AddChildToCanvas(Stamp);
		CheckSlot->SetSize(FVector2D(150.f, 32.f));
		CheckSlot->SetAlignment(FVector2D(0.5f, 0.f));
		CheckSlot->SetZOrder(6);
		Label->SetVisibility(ESlateVisibility::Hidden);
		Check->SetVisibility(ESlateVisibility::Hidden);
		Stamp->SetVisibility(ESlateVisibility::Hidden);
		SeatLabels.Add(Label);
		SeatNames.Add(Name);
		SeatChecks.Add(Check);
		SeatReadyStamps.Add(Stamp);
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
	struct FSeatPlacement
	{
		int32 Index;
		FVector2D Position;
		float Width;
	};
	TArray<FSeatPlacement> Placements;
	float MinSeatY = TNumericLimits<float>::Max();
	float MaxSeatY = TNumericLimits<float>::Lowest();
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
		SeatReadyStamps[Index]->SetVisibility(ESlateVisibility::Hidden);
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
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(), Mesh->GetComponentLocation() + FVector(0.f, 0.f, 105.f), Position, false))
		{
			continue;
		}
		// DPI와 화면 루트의 위치를 함께 보정해야 창 크기 변경에도 머리 위를 유지함
		Position = MyGeometry.AbsoluteToLocal(UWidgetLayoutLibrary::GetViewportWidgetGeometry(this).LocalToAbsolute(Position));
		// 빈 뒷좌석도 행 구분에 포함하여 접속 인원이 바뀌어도 앞좌석 높이가 유지되도록 함
		MinSeatY = FMath::Min(MinSeatY, static_cast<float>(Position.Y));
		MaxSeatY = FMath::Max(MaxSeatY, static_cast<float>(Position.Y));
		if (!Player)
		{
			continue;
		}
		FString DisplayName = Player->GetPlayerName();
		if (DisplayName.IsEmpty())
		{
			DisplayName = FString::Printf(TEXT("플레이어 %d"), Index + 1);
		}
		// 글자 수 대신 실제 너비로 제한하여 한글과 영문이 섞여도 일정한 판독성을 유지함
		const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FSlateFontInfo NameFont = SeatNames[Index]->GetFont();
		const float TextWidth = FontMeasure->Measure(DisplayName, NameFont).X;
		const float Width = FMath::Clamp(TextWidth + 28.f, 120.f, 230.f);
		// 가운데 정렬된 긴 문자열이 앞부분부터 잘리지 않도록 표시 문자열 자체를 맞춤
		if (TextWidth > Width - 28.f)
		{
			while (!DisplayName.IsEmpty() && FontMeasure->Measure(DisplayName + TEXT("..."), NameFont).X > Width - 28.f)
			{
				DisplayName.LeftChopInline(1);
			}
			DisplayName += TEXT("...");
		}
		SeatNames[Index]->SetText(FText::FromString(DisplayName));
		CastChecked<UCanvasPanelSlot>(SeatLabels[Index]->Slot)->SetSize(FVector2D(Width, 42.f));
		Placements.Add({Index, Position, Width});
		SeatLabels[Index]->SetVisibility(ESlateVisibility::HitTestInvisible);
		// 온라인 ID가 없는 로컬 테스트에서는 호스트가 먼저 배정되는 서버 0번 좌석을 사용함
		const bool bIsHost = HostId.IsValid() && Player->GetUniqueId().IsValid()
			? *HostId == *Player->GetUniqueId().GetUniqueNetId() : Index == 0;
		SeatChecks[Index]->SetVisibility(Player->IsReady() && !bIsHost ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		SeatReadyStamps[Index]->SetVisibility(Player->IsReady() && !bIsHost ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	// 앞좌석의 이름표와 도장을 함께 올려 얼굴을 가리는 영역을 줄이기 위함
	if (MaxSeatY > MinSeatY + 40.f)
	{
		const float RowMidpoint = (MinSeatY + MaxSeatY) * 0.5f;
		for (FSeatPlacement& Placement : Placements)
		{
			if (Placement.Position.Y > RowMidpoint)
			{
				Placement.Position.Y -= 36.f;
			}
		}
	}
	// 화면에서 위에 있는 뒷좌석부터 배치하여 앞좌석 이름표가 아래로 분리되도록 함
	Placements.Sort([](const FSeatPlacement& A, const FSeatPlacement& B)
	{
		return A.Position.Y == B.Position.Y ? A.Index < B.Index : A.Position.Y < B.Position.Y;
	});
	TArray<FSlateRect> Occupied;
	for (FSeatPlacement& Placement : Placements)
	{
		const float HalfWidth = FMath::Max(Placement.Width, 150.f) * 0.5f;
		const float CenterX = MyGeometry.GetLocalSize().X * 0.5f;
		// 뒷좌석 좌우 이름표가 서로를 아래로 밀지 않도록 화면 중앙에 간격을 확보함
		if (Placement.Position.X < CenterX)
		{
			Placement.Position.X = FMath::Clamp(Placement.Position.X, HalfWidth + 12.f, FMath::Max(HalfWidth + 12.f, CenterX - HalfWidth - 12.f));
		}
		else
		{
			Placement.Position.X = FMath::Clamp(Placement.Position.X, CenterX + HalfWidth + 12.f, FMath::Max(CenterX + HalfWidth + 12.f, MyGeometry.GetLocalSize().X - HalfWidth - 12.f));
		}
		float Top = FMath::Max(12.f, Placement.Position.Y - 42.f);
		// 준비 해제 때도 도장 공간을 예약하여 다른 이름표가 갑자기 움직이지 않도록 함
		for (int32 Pass = 0; Pass < Placements.Num(); ++Pass)
		{
			for (const FSlateRect& Rect : Occupied)
			{
				if (Placement.Position.X + HalfWidth + 8.f > Rect.Left && Placement.Position.X - HalfWidth - 8.f < Rect.Right &&
					Top + 78.f + 8.f > Rect.Top && Top < Rect.Bottom + 8.f)
				{
					Top = Rect.Bottom + 8.f;
				}
			}
		}
		Occupied.Add(FSlateRect(Placement.Position.X - HalfWidth, Top, Placement.Position.X + HalfWidth, Top + 78.f));
		CastChecked<UCanvasPanelSlot>(SeatLabels[Placement.Index]->Slot)->SetPosition(FVector2D(Placement.Position.X, Top + 42.f));
		CastChecked<UCanvasPanelSlot>(SeatReadyStamps[Placement.Index]->Slot)->SetPosition(FVector2D(Placement.Position.X, Top + 46.f));
	}
}

void UCWRoomScreenWidget::NativeDestruct()
{
	for (UBorder* Label : SeatLabels)
	{
		Label->RemoveFromParent();
	}
	for (UBorder* Stamp : SeatReadyStamps)
	{
		Stamp->RemoveFromParent();
	}
	SeatLabels.Reset();
	SeatNames.Reset();
	SeatChecks.Reset();
	SeatReadyStamps.Reset();
	SeatCanvas = nullptr;
	Presentation.Reset();
	SearchCooldown = 0.f;
	Super::NativeDestruct();
}
