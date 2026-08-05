// CWDustPatch.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CWDustPatch.generated.h"

class ACWCharacter;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;

// A patch of road debris that must be swept clear with the broom before the crosswalk
// lines can be painted over it.
//
// ClearedPct is server-authoritative and is the thing gameplay logic should check
// (IsFullyCleared) to decide whether this patch still blocks the outline/fill steps.
// The render-target paint mask is a purely local visual: every client (and a listen
// server) repaints it independently from the same replicated character positions/tool
// state, so the mask itself never needs to go over the network.
UCLASS()
class CROSSWALK_API ACWDustPatch : public AActor
{
	GENERATED_BODY()

public:
	ACWDustPatch();

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintPure, Category = "Crosswalk|Dust")
	float GetClearedPct() const { return ClearedPct; }

	UFUNCTION(BlueprintPure, Category = "Crosswalk|Dust")
	bool IsFullyCleared() const { return ClearedPct >= 1.f; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, Category = "Crosswalk|Dust")
	TObjectPtr<UStaticMeshComponent> PatchMesh;

	// Footprint on the ground plane, in the patch's own local X/Y (before rotation).
	UPROPERTY(EditAnywhere, Category = "Crosswalk|Dust")
	FVector2D PatchExtent = FVector2D(50.f, 100.f);

	// Seconds of continuous sweeping (by any one character) needed to fully clear this patch.
	UPROPERTY(EditAnywhere, Category = "Crosswalk|Dust")
	float SecondsToClear = 2.f;

	// How close a sweeping character needs to be, in addition to being within PatchExtent,
	// before Tick bothers checking it in detail. Keep generous; ProjectToLocalUV is the
	// precise test.
	UPROPERTY(EditAnywhere, Category = "Crosswalk|Dust")
	float SweepDetectionRadius = 250.f;

	// Ground material driving the dusty -> clean look. Must expose a scalar parameter
	// named "ClearedPct" (required, drives the coarse fallback fade). Optionally also a
	// texture parameter named "CoverageMask" if BrushStampMaterial below is set too.
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Dust|Visual")
	TObjectPtr<UMaterialInterface> DustMaterial;

	// Soft round alpha stamp used to paint the coverage mask at a sweeping character's
	// position each frame. Must expose scalar parameters "CenterU", "CenterV", "Radius".
	// Optional: if unset, the patch still works correctly from ClearedPct alone, just
	// without the precise per-position paint trail.
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Dust|Visual")
	TObjectPtr<UMaterialInterface> BrushStampMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Dust|Visual")
	FIntPoint CoverageMaskSize = FIntPoint(128, 256);

	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Dust|Visual")
	float BrushRadiusUV = 0.12f;

private:
	void ServerTick(float DeltaSeconds);
	void ClientTick(float DeltaSeconds);
	void PaintAt(const ACWCharacter* Sweeper);
	bool ProjectToLocalUV(const FVector& WorldLocation, FVector2D& OutUV) const;
	void ApplyClearedPctToMaterial();

	UFUNCTION()
	void OnRep_ClearedPct();

	UPROPERTY(ReplicatedUsing = OnRep_ClearedPct, VisibleAnywhere, Category = "Crosswalk|Dust")
	float ClearedPct = 0.f;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BrushDynMaterial;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> CoverageMask;
};
