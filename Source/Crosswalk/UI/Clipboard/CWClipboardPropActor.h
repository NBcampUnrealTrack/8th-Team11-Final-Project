// CWClipboardPropActor.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Styling/SlateBrush.h"
#include "CWClipboardPropActor.generated.h"

struct FGeometry;
class APlayerController;
class SRetainerWidget;
class SWidget;
class UCurveFloat;
class UMaterialInterface;
class USpotLightComponent;
class UStaticMeshComponent;
class UUserWidget;
class UWidget;
class UWidgetComponent;
class UCWClipboardWidgetInteractionComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogCWClipboard, Log, All);

// 클립보드 연출의 현재 단계
UENUM(BlueprintType)
enum class ECWClipboardState : uint8
{
	// 화면 밖. 숨김 상태
	Hidden,
	// 올라오는 중
	Raising,
	// 완전히 올라와 멎음
	Raised,
	// 내려가는 중
	Lowering
};

// 연출 완료 통보 (Presenter 내부 배선용)
// UI 는 이걸 쓰지 않고 Subsystem 의 Dynamic 델리게이트를 사용함
DECLARE_MULTICAST_DELEGATE(FCWClipboardMotionFinishedSignature);

// 로컬 카메라 앞에 떠 있는 클립보드 프롭
// 월드에 존재하지만 복제되지 않고, 로컬 플레이어에게만 보임
UCLASS()
class CROSSWALK_API ACWClipboardPropActor : public AActor
{
	GENERATED_BODY()

#pragma region Constructor

public:
	ACWClipboardPropActor();

#pragma endregion

#pragma region ActorOverrides

public:
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	// 떠 있는 채로 맵 이동 등으로 사라지면 해상도 고정이 인게임까지 남으므로 여기서 풂
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#pragma endregion

#pragma region Setup

public:
	// 어느 플레이어의 시점을 따라갈지 지정 (Subsystem 이 스폰 직후 호출)
	void SetOwningPlayerController(APlayerController* InPlayerController);

	// 팝업 인스턴스는 유지하고 내용만 종이 표면에 그림
	void SetPaperContent(UUserWidget* InOwner, UWidget* InContent);
	void ClearPaperContent(const UUserWidget* InOwner);
	bool IsPaperOwner(const UUserWidget* InOwner) const;
	void SetPaperInteractionEnabled(bool bEnabled);
	bool PressPaperPointer(FKey InKey);
	void ReleasePaperPointer(FKey InKey);
	void ReleasePaperPointers();
	void ScrollPaper(float InDelta);
	void FocusPaperInput();
	bool ProjectPaperWidget(const FGeometry& WidgetGeometry, FVector2D& OutPosition, FVector2D& OutSize) const;
	UUserWidget* GetPaperOwner() const { return PaperOwner.Get(); }
	void SetPresentationScale(float InScale, float InHeightRatio = 1.f);
	void SetPaperOnlyPresentation(bool bEnabled);
	void SetCenteredPresentation(bool bEnabled) { bCenteredPresentation = bEnabled; }

	// 이전 종이를 지우면서 새 종이를 그림. 끝나면 OnPaperCrossfadeFinished 를 발행함
	// 올라와 있지 않거나 이전 종이가 없으면 즉시 교체하고 바로 발행함
	void CrossfadePaperContent(UUserWidget* InOwner, UWidget* InContent);
	bool IsPaperCrossfading() const { return bPaperCrossfading; }

#pragma endregion

#pragma region Motion

public:
	// 올리기 시작. 이미 올라와 있거나 올라오는 중이면 무시
	void StartRaise();

	// 내리기 시작. 이미 내려가 있거나 내려가는 중이면 무시
	void StartLower();

	ECWClipboardState GetClipboardState() const
	{
		return ClipboardState;
	}

#pragma endregion

#pragma region PrivateFunctions

private:
	// 커브 에셋이 없으면 오버슈트가 있는 기본 이징으로 대체
	float EvaluateBlend(float InAlpha) const;

	// 경과 시간에서 지연을 뺀 뒤 0~1 로 정규화
	float ComputeAlpha(float InOffsetSeconds) const;

	// 종이만 보이는 연출은 판 중심을 화면 중앙에 맞추므로 올라온 포즈가 설정값과 다름
	FVector GetEffectiveRaisedLocation(FRotator& OutRotation) const;

	// 지금 움직임의 길이. 내리기는 더 멀리 가므로 거리 비율만큼 늘려 속도를 올리기와 맞춤
	float GetActiveMotionDuration() const;

	// 판과 종이의 경계 상자가 전부 화면 아래로 벗어났는지
	bool IsPropOffScreen() const;

	// 종이 글씨는 3D 월드에 그려져 해상도 품질이 낮으면 뭉개지므로 떠 있는 동안만 100% 를 요청함
	void SetHoldFullWorldResolution(bool bHold);

	// 최종 카메라 시점 기준으로 이번 프레임 트랜스폼을 설정함
	void UpdateTransformFromCamera();

	// 프롭과 전용 라이트를 월드 조명에서 격리
	void ApplyLightingChannels();

	// 연출 종료 처리와 통보
	void FinishMotion();

	// 게임 월드 충돌과 무관하게 종이 평면만 마우스로 선택함
	void UpdatePaperPointer();

	void UpdatePaperCrossfade(float DeltaSeconds);

	// 겹침 패널을 걷어내고 새 종이만 남김. 중간에 끊겨도 두 페이지의 투명도를 원래대로 돌려놓음
	void EndPaperCrossfade();

	// 페이지를 렌더 타깃에 그린 뒤 붓 마스크로 지우거나 써 내려감
	TSharedRef<SRetainerWidget> MakeInkLayer(const TSharedRef<SWidget>& InContent, bool bReveal);

	// 붓 레이어를 프로세스에서 처음 그리는 프레임에 렌더 준비가 겹쳐 첫 전환만 종이가 깜빡임
	// 올라오는 동안 보이지 않는 작은 붓 레이어를 같은 경로로 미리 그려 첫 전환 전에 준비를 끝냄
	void BeginPaperWarmup();

	// 미리 그리던 레이어를 걷고 페이지만 남김. 페이지를 옮기기 전에 불러야 부모가 둘이 되지 않음
	void EndPaperWarmup();

#pragma endregion

#pragma region Components

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CW|Clipboard", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> PropRoot;

	// 임시 플레이스홀더는 엔진 큐브를 납작하게 누른 판때기
	// 정식 메시가 오면 BP 에서 StaticMesh 만 교체하면 됨
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CW|Clipboard", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> ClipboardMesh;

	// 손 메시는 BP 에서 지정함 (Characters/Player/hand/ 의 Lefthand·Righthand). 비어 있어도 동작함
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CW|Clipboard", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> HandMesh;

	// 루트에 붙어 클립보드와 함께 회전함. 그래서 밝기가 항상 같음
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CW|Clipboard", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpotLightComponent> PropLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CW|Clipboard", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWidgetComponent> PaperWidget;

	UPROPERTY(VisibleAnywhere, Category = "CW|Clipboard")
	TObjectPtr<UCWClipboardWidgetInteractionComponent> PaperInteraction;

#pragma endregion

#pragma region Tuning

private:
	// 카메라 기준 숨김 포즈 (화면 아래, 눕혀진 상태)
	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Pose")
	FVector HiddenRelativeLocation = FVector(50.f, 5.f, -60.f);

	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Pose")
	FRotator HiddenRelativeRotation = FRotator(10.f, 0.f, -12.f);

	// 카메라 기준 최종 포즈 (읽을 수 있게 세워진 상태)
	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Pose")
	FVector RaisedRelativeLocation = FVector(60.f, 0.f, -12.f);

	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Pose")
	FRotator RaisedRelativeRotation = FRotator(70.f, 0.f, 0.f);

	// 내릴 때만 쓰는 도착 포즈. 숨김 포즈는 올라오는 출발점이라 판 윗부분이 화면에 남아 있음
	// 내릴 때는 화면 밖까지 빼야 마지막에 툭 사라지지 않음
	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Pose")
	FVector LoweredRelativeLocation = FVector(50.f, 5.f, -130.f);

	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Pose")
	FRotator LoweredRelativeRotation = FRotator(10.f, 0.f, -12.f);

	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Motion", meta = (ClampMin = "0.05"))
	float MotionDuration = 0.45f;

	// 회전이 위치보다 늦게 따라옴. 무게감은 거의 전부 이 값에서 나옴
	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Motion", meta = (ClampMin = "0.0"))
	float RotationLagSeconds = 0.08f;

	// 지정하면 기본 이징 대신 이 커브를 사용함 (리컴파일 없이 감각 조절)
	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Motion")
	TObjectPtr<UCurveFloat> MotionCurve;

	// 안착 후 미세 흔들림. 얼어붙은 프롭 느낌을 줄임
	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Sway")
	float SwayAmplitudeDegrees = 0.3f;

	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Sway")
	float SwayFrequency = 0.6f;

	// 끄면 월드 조명을 그대로 받음 (디버그용)
	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Lighting")
	bool bIsolateLighting = true;

	// 이전 글씨가 지워지는 데 걸리는 시간
	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|PaperFade", meta = (ClampMin = "0.05", Units = "s"))
	float PaperEraseDuration = 0.6f;

	// 전환 시작부터 새 글씨를 쓰기 시작할 때까지. 지우기보다 짧으면 두 페이지가 잠깐 겹쳐 끊김 없이 이어짐
	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|PaperFade", meta = (ClampMin = "0.0", Units = "s"))
	float PaperDrawDelay = 0.4f;

	// 새 글씨가 써지는 데 걸리는 시간
	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|PaperFade", meta = (ClampMin = "0.05", Units = "s"))
	float PaperDrawDuration = 0.6f;

	// 사선 지그재그 붓 마스크 (M_UI_PaperInkWipe). 비우면 두 페이지를 투명도로만 교차함
	// 줄 수·기울기·붓 끝 거칠기는 머티리얼 파라미터라 머티리얼 인스턴스로 바꿔 끼우면 됨
	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|PaperFade")
	TObjectPtr<UMaterialInterface> PaperInkWipeMaterial;

#pragma endregion

#pragma region State

private:
	ECWClipboardState ClipboardState = ECWClipboardState::Hidden;

	float MotionElapsed = 0.f;

	float LowerMotionDuration = 0.f;

	// 내리기 완료는 원래 숨김 포즈에 닿을 시각에 미리 알림. 끝에서 한 번 더 알리지 않도록 기록함
	bool bLowerNotified = false;

	// 해상도 고정 요청은 설정 객체가 횟수로 세므로 액터마다 한 번만 요청하도록 기록함
	bool bHoldsFullWorldResolution = false;

	float SwayElapsed = 0.f;

	TWeakObjectPtr<APlayerController> OwningPlayerController;

	TWeakObjectPtr<UUserWidget> PaperOwner;
	TWeakObjectPtr<UWidget> PaperContent;
	TWeakObjectPtr<UWidget> FadingOutContent;
	float PaperContentOpacity = 1.f;
	float FadingOutOpacity = 1.f;
	bool bPaperCrossfading = false;
	float PaperFadeElapsed = 0.f;
	TSharedPtr<SRetainerWidget> EraseInkLayer;
	TSharedPtr<SRetainerWidget> WriteInkLayer;

	// 바닥 종이 이미지는 브러시 포인터만 받으므로 전환이 끝날 때까지 여기서 들고 있음
	UPROPERTY(Transient)
	FSlateBrush PaperFadeBrush;

	TSharedPtr<SRetainerWidget> WarmupInkLayer;

	// 미리 그리기 레이어도 브러시 포인터만 받으므로 걷을 때까지 여기서 들고 있음
	UPROPERTY(Transient)
	FSlateBrush PaperWarmupImageBrush;
	UPROPERTY(Transient)
	FSlateBrush PaperWarmupBoxBrush;
	UPROPERTY(Transient)
	FSlateBrush PaperWarmupRoundedBrush;
	TSet<FKey> PressedPaperKeys;
	bool bPaperInteractionEnabled = false;
	bool bPaperOnlyPresentation = false;
	bool bCenteredPresentation = false;
	float PresentationScale = 1.f;
	FVector2D OriginalPaperDrawSize = FVector2D::ZeroVector;
	FTransform OriginalClipboardTransform;
	FVector OriginalPaperLocation = FVector::ZeroVector;
	FVector OriginalHandLocation = FVector::ZeroVector;

#pragma endregion

#pragma region Delegates

public:
	FCWClipboardMotionFinishedSignature OnRaiseFinished;

	FCWClipboardMotionFinishedSignature OnLowerFinished;

	FCWClipboardMotionFinishedSignature OnPaperCrossfadeFinished;

#pragma endregion
};
