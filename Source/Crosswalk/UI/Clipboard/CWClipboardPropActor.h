// CWClipboardPropActor.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CWClipboardPropActor.generated.h"

class APlayerController;
class UCurveFloat;
class USpotLightComponent;
class UStaticMeshComponent;

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
// UI 는 이걸 쓰지 않고 Subsystem 의 Dynamic 델리게이트를 쓴다
DECLARE_MULTICAST_DELEGATE(FCWClipboardMotionFinishedSignature);

// 로컬 카메라 앞에 떠 있는 클립보드 프롭
// 월드에 존재하지만 복제되지 않고, 로컬 플레이어에게만 보인다
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

#pragma endregion

#pragma region Setup

public:
	// 어느 플레이어의 시점을 따라갈지 지정 (Subsystem 이 스폰 직후 호출)
	void SetOwningPlayerController(APlayerController* InPlayerController);

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

	// 최종 카메라 시점 기준으로 이번 프레임 트랜스폼을 세운다
	void UpdateTransformFromCamera();

	// 프롭과 전용 라이트를 월드 조명에서 격리
	void ApplyLightingChannels();

	// 연출 종료 처리와 통보
	void FinishMotion();

#pragma endregion

#pragma region Components

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CW|Clipboard", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> PropRoot;

	// 임시 플레이스홀더는 엔진 큐브를 납작하게 누른 판때기
	// 정식 메시가 오면 BP 에서 StaticMesh 만 교체하면 된다
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CW|Clipboard", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> ClipboardMesh;

	// 손 메시는 BP 에서 꽂는다 (Characters/Player/hand/ 의 Lefthand·Righthand). 비어 있어도 동작한다
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CW|Clipboard", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> HandMesh;

	// 루트에 붙어 클립보드와 함께 회전한다. 그래서 밝기가 항상 같다
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CW|Clipboard", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpotLightComponent> PropLight;

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

	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Motion", meta = (ClampMin = "0.05"))
	float MotionDuration = 0.45f;

	// 회전이 위치보다 늦게 따라온다. 무게감은 거의 전부 이 값에서 나온다
	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Motion", meta = (ClampMin = "0.0"))
	float RotationLagSeconds = 0.08f;

	// 지정하면 기본 이징 대신 이 커브를 쓴다 (리컴파일 없이 감각 조절)
	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Motion")
	TObjectPtr<UCurveFloat> MotionCurve;

	// 안착 후 미세 흔들림. 얼어붙은 프롭 느낌을 없앤다
	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Sway")
	float SwayAmplitudeDegrees = 0.3f;

	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Sway")
	float SwayFrequency = 0.6f;

	// 끄면 월드 조명을 그대로 받는다 (디버그용)
	UPROPERTY(EditDefaultsOnly, Category = "CW|Clipboard|Lighting")
	bool bIsolateLighting = true;

#pragma endregion

#pragma region State

private:
	ECWClipboardState ClipboardState = ECWClipboardState::Hidden;

	float MotionElapsed = 0.f;

	float SwayElapsed = 0.f;

	TWeakObjectPtr<APlayerController> OwningPlayerController;

#pragma endregion

#pragma region Delegates

public:
	FCWClipboardMotionFinishedSignature OnRaiseFinished;

	FCWClipboardMotionFinishedSignature OnLowerFinished;

#pragma endregion
};
