// CWClipboardPresenterSubsystem.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UI/Clipboard/CWClipboardPropActor.h"
#include "CWClipboardPresenterSubsystem.generated.h"

class ULocalPlayer;

// 클립보드가 완전히 올라와 멎은 순간
// 계약: PlayRaise 호출당 반드시 한 번 발행된다
// 연출에 실패한 경우도, 이미 올라와 있어 건너뛴 경우도 예외가 아니다
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCWClipboardRaiseFinishedSignature);

// 클립보드가 완전히 내려간 순간
// 계약: PlayLower 호출당 반드시 한 번 발행된다
// 연출에 실패한 경우도, 이미 내려가 있어 건너뛴 경우도 예외가 아니다
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCWClipboardLowerFinishedSignature);

// 스테이지 선택 화면의 클립보드 연출을 담당
// UI 는 PlayRaise / PlayLower 만 부르고 완료는 델리게이트로 받는다
UCLASS(Config = Game)
class CROSSWALK_API UCWClipboardPresenterSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

#pragma region SubsystemOverrides

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

#pragma endregion

#pragma region PublicFunctions

public:
	// 클립보드를 화면 안으로 올린다
	// 이미 올라와 있거나 연출이 불가능하면 건너뛰고 즉시 OnRaiseFinished 를 발행한다
	// 올라오는 중이면 진행 중인 연출의 완료 통보에 맡기고 아무것도 하지 않는다
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Clipboard")
	void PlayRaise();

	// 클립보드를 내린다
	// 이미 내려가 있거나 연출이 불가능하면 건너뛰고 즉시 OnLowerFinished 를 발행한다
	// 내려가는 중이면 진행 중인 연출의 완료 통보에 맡기고 아무것도 하지 않는다
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Clipboard")
	void PlayLower();

	// 방어 분기와 디버그용. 계약에 포함되지 않는다
	// 상태 전이는 반드시 델리게이트로 받을 것 (매 프레임 폴링 금지)
	UFUNCTION(BlueprintPure, Category = "CW|UI|Clipboard")
	ECWClipboardState GetClipboardState() const
	{
		return ClipboardState;
	}

#pragma endregion

#pragma region PrivateFunctions

private:
	// 프롭이 없으면 스폰한다. 실패하면 false
	bool EnsurePropSpawned();

	// 맵 이동 등으로 프롭이 사라졌으면 상태를 되돌린다
	void ReconcileLostProp();

	ULocalPlayer* ResolveLocalPlayer() const;

	void HandleRaiseFinished();

	void HandleLowerFinished();

#pragma endregion

#pragma region Variables

private:
	// 비워두면 C++ 기본 클래스를 스폰한다
	// 정식 메시가 들어오면 BP 를 만들어 DefaultGame.ini 에서 지정하면 된다
	UPROPERTY(Config, EditAnywhere, Category = "CW|UI|Clipboard")
	TSoftClassPtr<ACWClipboardPropActor> PropActorClass;

	UPROPERTY(Transient)
	TObjectPtr<ACWClipboardPropActor> PropActor;

	// 프롭이 없을 때도 계약을 지켜야 하므로 상태는 여기서 들고 있는다
	ECWClipboardState ClipboardState = ECWClipboardState::Hidden;

#pragma endregion

#pragma region Delegates

public:
	UPROPERTY(BlueprintAssignable, Category = "CW|UI|Clipboard")
	FCWClipboardRaiseFinishedSignature OnRaiseFinished;

	UPROPERTY(BlueprintAssignable, Category = "CW|UI|Clipboard")
	FCWClipboardLowerFinishedSignature OnLowerFinished;

#pragma endregion
};
