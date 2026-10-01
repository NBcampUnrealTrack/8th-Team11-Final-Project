// CWPlayerState.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "CWPlayerState.generated.h"

class UCWWearableComponent;

UCLASS()
class CROSSWALK_API ACWPlayerState : public APlayerState
{
	GENERATED_BODY()

#pragma region PlayerStateLifecycle

public:
	ACWPlayerState();
	virtual void CopyProperties(APlayerState* PlayerState) override;

	UFUNCTION(BlueprintPure, Category = "CW|Wearable")
	UCWWearableComponent* GetWearableComponent() const { return WearableComponent; }

	// 준비 상태를 모든 클라이언트에 복제하도록 등록
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps
	) const override;

	// 플레이어 이름 복제 완료 후 현재 화면의 플레이어 목록 갱신을 알림
	virtual void OnRep_PlayerName() override;

#pragma endregion

#pragma region ReadyState

public:
	// 현재 방 준비 상태 반환
	UFUNCTION(BlueprintPure, Category = "CW|Room")
	bool IsReady() const;

	// 소유 클라이언트가 서버에 준비 상태 변경 요청
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "CW|Room")
	void ServerSetReady(bool bNewReady);

protected:
	// 준비 상태가 클라이언트에 복제된 후 플레이어 목록 갱신을 알림
	UFUNCTION()
	void OnRep_IsReady();

private:
	// 이름 또는 상태 변경 시 현재 맵의 GameState에 플레이어 목록 갱신을 알림
	void NotifyPlayersChanged() const;

#pragma endregion

#pragma region PlayerStateData

private:
	// 컴포넌트 내부의 기존 WornItems만 복제한다. 이 참조는 새 복제 변수가 아니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CW|Wearable", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCWWearableComponent> WearableComponent;

	// 방에서 해당 플레이어가 게임 시작 준비를 마쳤는지 나타냄
	UPROPERTY(
		ReplicatedUsing = OnRep_IsReady,
		BlueprintReadOnly,
		Category = "CW|Room",
		meta = (AllowPrivateAccess = "true")
	)
	bool bIsReady = false;

#pragma endregion
};
