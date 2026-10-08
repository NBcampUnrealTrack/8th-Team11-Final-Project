#include "UI/Widget/CWParryStatusWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Characters/CWPlayerCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Painting/Components/CWWorkerComponent.h"
#include "Rendering/DrawElements.h"
#include "Sinkhole/Components/CWSinkholeFallComponent.h"
#include "Traffic/Components/CWParryComponent.h"
#include "UI/Core/CWUISubsystem.h"
#include "UObject/ConstructorHelpers.h"

UCWParryStatusWidget::UCWParryStatusWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	static ConstructorHelpers::FObjectFinder<UTexture2D> Bat(
		TEXT("/Game/CrossWalk/UI/HUD/Parry/T_UI_ParryBat"));
	BatTexture = Bat.Object;
	BatBrush.SetResourceObject(BatTexture);
	BatBrush.ImageSize = FVector2D(40.f, 40.f);
}

void UCWParryStatusWidget::ConfigureDisplay(bool bShow, FVector2D Offset)
{
	bShowIcon = bShow;
	ScreenOffset = Offset;
}

void UCWParryStatusWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	bCanDisplay = false;
	ReadyPopRemaining = FMath::Max(0.f, ReadyPopRemaining - InDeltaTime);
	APlayerController* PC = GetOwningPlayer();
	ACWPlayerCharacter* Character = PC ? Cast<ACWPlayerCharacter>(PC->GetPawn()) : nullptr;
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	const UCWUISubsystem* UI = LocalPlayer ? LocalPlayer->GetSubsystem<UCWUISubsystem>() : nullptr;
	if (!IsValid(Character) || !Character->IsLocallyControlled() || !UI || !UI->CanShowInteractionPrompts())
	{
		return;
	}
	if (LastPawn.Get() != Character)
	{
		LastPawn = Character;
		bHadCooldown = false;
		ReadyPopRemaining = 0.f;
	}
	const UCWParryComponent* Parry = Character->FindComponentByClass<UCWParryComponent>();
	const UCWWorkerComponent* Worker = Character->FindComponentByClass<UCWWorkerComponent>();
	const UCWSinkholeFallComponent* Fall = Character->FindComponentByClass<UCWSinkholeFallComponent>();
	if (!Parry || Character->IsHidden() || (Fall && Fall->IsFallLocked()))
	{
		return;
	}
	// 고정 1초 타이머 대신 실제 준비 비율을 읽어 환불과 서버 거절에도 표시가 일치하도록 함
	const bool bCooldownReady = Parry->GetCooldownReadyRatio() >= 0.999f;
	if (!bCooldownReady)
	{
		bHadCooldown = true;
	}
	else if (bHadCooldown)
	{
		ReadyPopRemaining = 0.22f;
		bHadCooldown = false;
	}
	bReady = bCooldownReady && !(Worker && Worker->IsParryBlocked());
	FVector2D Projected;
	if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
		PC, Character->GetActorLocation() + FVector(0.f, 0.f, 20.f), Projected, true))
	{
		return;
	}
	const FGeometry PlayerGeometry = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(PC);
	IconPosition = MyGeometry.AbsoluteToLocal(PlayerGeometry.LocalToAbsolute(Projected)) + ScreenOffset;
	const FVector2D Size = MyGeometry.GetLocalSize();
	bCanDisplay = BatTexture && IconPosition.X >= 0.f && IconPosition.Y >= 0.f
		&& IconPosition.X + 48.f <= Size.X && IconPosition.Y + 48.f <= Size.Y;
	bCanDisplay = bCanDisplay && bShowIcon;
}

int32 UCWParryStatusWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!bCanDisplay)
	{
		return BaseLayer;
	}
	const float Pop = bReady ? FMath::Sin((ReadyPopRemaining / 0.22f) * PI) : 0.f;
	const float IconSize = 40.f + 4.f * Pop;
	const FVector2D Origin = IconPosition - FVector2D(2.f * Pop);
	const FLinearColor Tint = bReady ? FLinearColor::White : FLinearColor(0.32f, 0.32f, 0.32f, 1.f);
	FSlateDrawElement::MakeBox(OutDrawElements, BaseLayer + 1,
		AllottedGeometry.ToPaintGeometry(FVector2D(IconSize), FSlateLayoutTransform(Origin)),
		&BatBrush, ESlateDrawEffect::None, Tint * InWidgetStyle.GetColorAndOpacityTint());
	if (bReady)
	{
		const FLinearColor Cream = FLinearColor(1.f, 0.94f, 0.75f, 1.f) * InWidgetStyle.GetColorAndOpacityTint();
		for (int32 Index = 0; Index < 2; ++Index)
		{
			TArray<FVector2D> Points;
			const FVector2D Start = IconPosition + FVector2D(35.f + Index * 3.f, 2.f + Index * 7.f);
			Points.Add(Start);
			Points.Add(Start + FVector2D(3.f + Pop * 3.f, -4.f + Index * 4.f));
			FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 2,
				AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Cream, true, 2.f);
		}
	}
	return BaseLayer + 2;
}
