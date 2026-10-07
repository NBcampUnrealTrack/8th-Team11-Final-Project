// CWBaseToolPaintGauge.cpp

#include "UI/Widget/CWBaseToolPaintGauge.h"

#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Internationalization/Text.h"
#include "Painting/Tools/CWBaseTool.h"
#include "TimerManager.h"
#include "UI/CWUILog.h"
#include "UI/Data/CWToolUIDataAsset.h"

FText FCWToolPaintDisplayBuilder::MakePercentText(float Ratio)
{
	// 소수점 없이 반올림해 표시 - 기존 BP 의 AsPercent 설정과 같음
	FNumberFormattingOptions Options;
	Options.MinimumFractionalDigits = 0;
	Options.MaximumFractionalDigits = 0;
	return FText::AsPercent(Ratio, &Options);
}

FCWToolPaintDisplay FCWToolPaintDisplayBuilder::Build(const FCWToolPaintInput& Input)
{
	FCWToolPaintDisplay Display;

	// 도구 없음과 None 종류는 데이터 조회 없이 같은 비활성으로 처리
	const bool bHasKnownTool = Input.bHasTool && Input.ToolType != ECWToolType::None;
	if (bHasKnownTool && Input.bShowsFill)
	{
		Display.bActive = true;
		Display.bDark = Input.bDarkPaint;
		Display.Fill = FMath::Clamp(Input.Ratio, 0.f, 1.f);
	}

	Display.Icon = Input.ToolIcon;
	Display.bIconActive = Display.bActive;
	if (Display.bActive)
	{
		Display.PercentText = MakePercentText(Display.Fill);
	}

	// 통을 들고 있으면 도구 표시보다 통 표시가 우선 - 한 곳에서 정해 글자를 덮어쓰는 순서 문제를 없앰
	if (Input.CarriedCount > 0)
	{
		Display.PercentText = Input.CarriedCount >= 2 ? Input.CarriedTextFull : Input.CarriedTextOne;
		Display.Icon = Input.PaintCanIcon;
		Display.bIconActive = false;
	}

	return Display;
}

void UCWBaseToolPaintGauge::NativeConstruct()
{
	Super::NativeConstruct();

	// BP Construct 가 끝난 뒤 위젯이 준비된 상태에서 첫 값을 즉시 그림
	FlushNow();
}

void UCWBaseToolPaintGauge::NativeDestruct()
{
	UnbindTool();
	bDirty = false;
	Super::NativeDestruct();
}

void UCWBaseToolPaintGauge::SetGaugeTool(ACWBaseTool* InTool)
{
	// 이전 도구의 구독을 먼저 풀어 두 도구가 동시에 살아 있는 순간을 만들지 않음
	UnbindTool();
	TargetTool = InTool;

	if (IsValid(InTool))
	{
		// 페인트를 쓰지 않는 도구는 잔량이 바뀌지 않으므로 구독하지 않음
		if (InTool->UsesPaint())
		{
			InTool->OnPaintChanged.AddUniqueDynamic(this, &UCWBaseToolPaintGauge::HandleToolPaintChanged);
		}
		InTool->OnDestroyed.AddUniqueDynamic(this, &UCWBaseToolPaintGauge::HandleToolDestroyed);
	}

	FlushNow();
}

void UCWBaseToolPaintGauge::SetGaugeCarriedCount(int32 InCount)
{
	const int32 NewCount = FMath::Max(InCount, 0);
	if (CarriedCount == NewCount)
	{
		return;
	}

	CarriedCount = NewCount;
	MarkDirty();
}

void UCWBaseToolPaintGauge::UnbindTool()
{
	if (ACWBaseTool* Tool = TargetTool.Get())
	{
		Tool->OnPaintChanged.RemoveDynamic(this, &UCWBaseToolPaintGauge::HandleToolPaintChanged);
		Tool->OnDestroyed.RemoveDynamic(this, &UCWBaseToolPaintGauge::HandleToolDestroyed);
	}
	TargetTool.Reset();
}

void UCWBaseToolPaintGauge::HandleToolPaintChanged(ACWBaseTool* Tool, float PaintAmount, float PaintRatio)
{
	if (Tool == TargetTool.Get())
	{
		MarkDirty();
	}
}

void UCWBaseToolPaintGauge::HandleToolDestroyed(AActor* DestroyedActor)
{
	// 파괴된 도구는 값이 없는 표시로 되돌림
	if (DestroyedActor == TargetTool.Get())
	{
		TargetTool.Reset();
		MarkDirty();
	}
}

void UCWBaseToolPaintGauge::MarkDirty()
{
	if (bDirty)
	{
		return;
	}

	bDirty = true;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 위젯 Tick 은 Collapsed 일 때 멈추므로 월드 타이머로 다음 틱에 모아 그림
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { Flush(); }));
}

void UCWBaseToolPaintGauge::Flush()
{
	if (!bDirty)
	{
		return;
	}

	bDirty = false;
	Apply();
}

void UCWBaseToolPaintGauge::FlushNow()
{
	bDirty = true;
	Flush();
}

void UCWBaseToolPaintGauge::Apply()
{
	FCWToolPaintInput Input;
	Input.CarriedCount = CarriedCount;
	Input.CarriedTextOne = CarriedTextOne;
	Input.CarriedTextFull = CarriedTextFull;

	const ACWBaseTool* Tool = TargetTool.Get();
	Input.bHasTool = IsValid(Tool);
	if (Input.bHasTool)
	{
		Input.ToolType = Tool->GetToolType();
		Input.Ratio = Tool->GetPaintRatio();
	}

	if (IsValid(ToolUIData))
	{
		FCWToolUIData Data;
		// 도구가 없을 때도 None 항목의 아이콘을 씀
		if (ToolUIData->FindToolUIData(Input.ToolType, Data))
		{
			Input.bShowsFill = Data.bShowsFill;
			Input.bDarkPaint = Data.bDarkPaint;
			Input.ToolIcon = Data.Icon;
		}
		Input.PaintCanIcon = ToolUIData->GetPaintCanIcon();
	}
	else
	{
		UE_LOG(LogCWUI, Warning, TEXT("[ToolPaintGauge] ToolUIData is not assigned. Widget=%s"), *GetName());
	}

	const FCWToolPaintDisplay Display = FCWToolPaintDisplayBuilder::Build(Input);

	// 크기는 원형과 도구 아이콘 게이지에서만 정함 - 선형은 호출한 쪽(원격 발밑 표시)이 정한 크기를 유지
	const bool bSizesRoot = bCircularStyle || bUseToolIconGauge;
	const float RootHeight = bSizesRoot ? (bCircularStyle ? CircularSize : LinearIconSize) : 0.f;
	const float IconSize = bUseToolIconGauge ? (bCircularStyle ? CircularSize : LinearIconSize) : 0.f;
	const int32 FontSize = bCircularStyle ? CircularFontSize : 0;

	ApplyPaintDisplay(
		bCircularStyle,
		bUseToolIconGauge,
		Display.bActive,
		Display.bDark,
		Display.Fill,
		Display.bDark ? DarkPaintColor : LightPaintColor,
		Display.PercentText,
		Display.Icon,
		Display.bIconActive,
		RootHeight,
		IconSize,
		FontSize
	);

	RefreshAvailability();
}

void UCWBaseToolPaintGauge::RefreshAvailability()
{
	if (!bAvailableOnlyWithTool)
	{
		return;
	}

	// 도구를 들면 BP_Tool 이 대상을 비우므로 대상이 없거나 들린 상태면 숨김
	const ACWBaseTool* Tool = TargetTool.Get();
	SetGaugeAvailable(IsValid(Tool) && !Tool->IsHeld());
}
