// CWBaseToolPaintGauge.h

#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/CWToolTypes.h"
#include "UI/Widget/CWBaseHUDGauge.h"
#include "CWBaseToolPaintGauge.generated.h"

class ACWBaseTool;
class UCWToolUIDataAsset;
class UTexture2D;

// 도구 게이지가 그릴 값 - 도구 종류별 분기와 통 개수 우선순위를 한 곳에서 정하기 위한 입력
struct CROSSWALK_API FCWToolPaintInput
{
	// 표시할 도구가 있는지 (약참조가 사라졌거나 지정하지 않았으면 false)
	bool bHasTool = false;
	ECWToolType ToolType = ECWToolType::None;
	float Ratio = 0.f;

	// 들고 있는 페인트 통 개수 - 0 보다 크면 글자와 아이콘을 통 표시가 이김
	int32 CarriedCount = 0;

	// 도구 종류 데이터 (DA_ToolUIData)
	bool bShowsFill = false;
	bool bDarkPaint = false;
	UTexture2D* ToolIcon = nullptr;

	UTexture2D* PaintCanIcon = nullptr;
	FText CarriedTextOne;
	FText CarriedTextFull;
};

// 도구 게이지가 그릴 최종 값 - 위젯에 쓰는 곳은 BP 이벤트 하나뿐
struct CROSSWALK_API FCWToolPaintDisplay
{
	bool bActive = false;
	bool bDark = false;
	float Fill = 0.f;
	FText PercentText;
	UTexture2D* Icon = nullptr;

	// 도구 아이콘 머티리얼의 Active 값 - 통을 들고 있으면 꺼짐
	bool bIconActive = false;
};

// 입력을 표시 값으로 바꾸는 순수 규칙 - 월드 없이 시험할 수 있도록 분리
struct CROSSWALK_API FCWToolPaintDisplayBuilder
{
	static FCWToolPaintDisplay Build(const FCWToolPaintInput& Input);

	// 잔량 비율을 소수점 없는 퍼센트 글자로
	static FText MakePercentText(float Ratio);
};

// 페인트 잔량 게이지(로컬 HUD 원형·원격 상태창 선형·도구 위 월드 표시)의 공통 부모
// 이벤트(OnPaintChanged)와 호출자의 값 지정으로만 갱신되며 Tick 으로 읽지 않음
// 같은 프레임의 변화는 모아 다음 틱에 한 번만 그리고, 위젯에는 ApplyPaintDisplay 한 곳에서만 씀
UCLASS(Abstract)
class CROSSWALK_API UCWBaseToolPaintGauge : public UCWBaseHUDGauge
{
	GENERATED_BODY()

public:
	// 표시할 도구를 지정 - null 이면 도구 없음. 이전 도구의 구독은 먼저 풀고 새 도구를 구독함
	UFUNCTION(BlueprintCallable, Category = "CW|UI|HUD|ToolPaint")
	void SetGaugeTool(ACWBaseTool* InTool);

	// 들고 있는 페인트 통 개수를 지정 - 같은 값이면 다시 그리지 않음
	UFUNCTION(BlueprintCallable, Category = "CW|UI|HUD|ToolPaint")
	void SetGaugeCarriedCount(int32 InCount);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 계산이 끝난 표시 값을 위젯에 씀 - 글자·가시성·머티리얼을 쓰는 유일한 곳
	// RootHeight/IconSize/FontSize 가 0 이하이면 그 항목은 건드리지 않음 (원격 발밑 표시가 크기를 직접 덮어쓰므로)
	UFUNCTION(BlueprintImplementableEvent, Category = "CW|UI|HUD|ToolPaint")
	void ApplyPaintDisplay(
		bool bCircular,
		bool bToolIconGauge,
		bool bActive,
		bool bDark,
		float Fill,
		FLinearColor FillColor,
		const FText& PercentText,
		UTexture2D* ToolIcon,
		bool bIconActive,
		float RootHeight,
		float IconSize,
		int32 FontSize
	);

private:
	// 값이 바뀌었음을 표시 - 같은 프레임의 여러 변화를 다음 틱의 한 번으로 모음
	void MarkDirty();

	// 더티일 때만 그림. 생성·대상 교체 때는 기다리지 않도록 바로 부름
	void Flush();
	void FlushNow();
	void Apply();
	void RefreshAvailability();

	UFUNCTION()
	void HandleToolPaintChanged(ACWBaseTool* Tool, float PaintAmount, float PaintRatio);

	UFUNCTION()
	void HandleToolDestroyed(AActor* DestroyedActor);

	void UnbindTool();

public:
	// 도구 이름·아이콘·표시 방식 데이터
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|HUD|ToolPaint")
	TObjectPtr<UCWToolUIDataAsset> ToolUIData;

	// 원형 게이지로 그림 (로컬 HUD)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|HUD|ToolPaint")
	bool bCircularStyle = false;

	// 도구 아이콘 머티리얼 위에 잔량을 채움
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|HUD|ToolPaint")
	bool bUseToolIconGauge = false;

	// 퍼센트 글자를 막대 위로 올림 (도구 위 월드 표시)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|HUD|ToolPaint")
	bool bPercentAbove = false;

	// 퍼센트 글자를 올리는 거리
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|HUD|ToolPaint", meta = (EditCondition = "bPercentAbove"))
	float PercentAboveOffsetY = -18.f;

	// 도구가 있을 때만 게이지를 보임 (도구 위 월드 표시는 도구를 들면 숨김)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|HUD|ToolPaint")
	bool bAvailableOnlyWithTool = false;

	// 원형 게이지와 도구 아이콘 게이지의 한 변
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|HUD|ToolPaint", meta = (ClampMin = "1"))
	float CircularSize = 160.f;

	// 선형 도구 아이콘 게이지의 한 변
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|HUD|ToolPaint", meta = (ClampMin = "1"))
	float LinearIconSize = 40.f;

	// 원형 게이지의 퍼센트 글자 크기
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|HUD|ToolPaint", meta = (ClampMin = "1"))
	int32 CircularFontSize = 32;

	// 밝은·어두운 페인트의 막대 색
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|HUD|ToolPaint")
	FLinearColor LightPaintColor = FLinearColor(0.78f, 0.76f, 0.69f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|HUD|ToolPaint")
	FLinearColor DarkPaintColor = FLinearColor(0.028f, 0.035f, 0.045f, 1.f);

	// 통을 1개·가득 들었을 때의 글자 - 통 개수가 2 이상이면 가득으로 봄
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|HUD|ToolPaint")
	FText CarriedTextOne = NSLOCTEXT("CWToolPaintGauge", "CarriedOne", "1/2");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CW|UI|HUD|ToolPaint")
	FText CarriedTextFull = NSLOCTEXT("CWToolPaintGauge", "CarriedFull", "2/2");

private:
	// 도구가 맵 이동 중 사라질 수 있어 약참조로 보관
	TWeakObjectPtr<ACWBaseTool> TargetTool;

	int32 CarriedCount = 0;
	bool bDirty = false;
};
