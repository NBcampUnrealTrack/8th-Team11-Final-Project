#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Subsystems/WorldSubsystem.h"
#include "CWCinematicCaptureSubsystem.generated.h"

class ASceneCapture2D;
class SWindow;
class UTextureRenderTarget2D;
class APlayerController;
class APawn;

/** Local, opt-in filming surface. Never possesses a pawn or changes the gameplay view. */
UCLASS()
class CROSSWALK_API UCWCinematicCaptureSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	void StartCapture(int32 Width = 1920, int32 Height = 1080, int32 FrameRate = 60);
	void StopCapture();
	void SetShot(int32 Shot);
	void SetFollow(bool bEnabled);
	void PlaceCamera(const FVector& Location, const FRotator& Rotation, float FieldOfView);
	void SetFocus(const FVector& Location);
	void SetOrbitSpeed(float DegreesPerSecond);
	void PrintStatus() const;
	void MatchGameCamera();
	void SetPlayerTarget(int32 PlayerNumber);
	void SetFreeCamera(bool bEnabled);
	void SetFreeCameraSpeed(float CentimetersPerSecond);
	bool IsCapturing() const { return bCapturing; }
	bool IsFreeCamera() const { return bFreeCamera; }
	bool IsFollowingPlayers() const { return bFollowPlayers; }
	float GetOrbitSpeed() const { return OrbitSpeed; }
	float GetFreeCameraSpeed() const { return FreeCameraSpeed; }

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	FVector FindPlayerCenter() const;
	void UpdateCamera(float DeltaTime);
	void RefreshHiddenComponents();
	void SyncRenderingSettings();
	void UpdateFreeCamera(float DeltaTime);
	void HandleWindowClosed(const TSharedRef<SWindow>& Window);

private:
	UPROPERTY(Transient)
	TObjectPtr<ASceneCapture2D> CaptureActor;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	// Slate does not own the UObject; RenderTarget's UPROPERTY keeps it alive.
	FSlateBrush PreviewBrush;
	TSharedPtr<SWindow> CaptureWindow;
	FVector FocusLocation = FVector::ZeroVector;
	float CaptureAccumulator = 0.f;
	float HiddenRefreshAccumulator = 0.f;
	float CaptureInterval = 1.f / 60.f;
	float OrbitSpeed = 0.f;
	float OrbitAngle = 0.f;
	int32 ActiveShot = 1;
	bool bFollowPlayers = true;
	bool bManualCamera = false;
	bool bCapturing = false;
	int32 TrackedPlayerId = INDEX_NONE;
	float FreeCameraSpeed = 1200.f;
	bool bFreeCamera = false;
	bool bSavedMouseCursor = true;
	TWeakObjectPtr<APlayerController> FreeCameraController;
	TWeakObjectPtr<APawn> DisabledInputPawn;
};
