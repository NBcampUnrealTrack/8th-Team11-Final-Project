// CWDustPatch.cpp

#include "CWDustPatch.h"
#include "Gameplay/CWToolTypes.h"
#include "Character/CWCharacter.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"

ACWDustPatch::ACWDustPatch()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	PatchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PatchMesh"));
	SetRootComponent(PatchMesh);
	PatchMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACWDustPatch::BeginPlay()
{
	Super::BeginPlay();

	if (DustMaterial)
	{
		DynMaterial = UMaterialInstanceDynamic::Create(DustMaterial, this);
		if (PatchMesh)
		{
			PatchMesh->SetMaterial(0, DynMaterial);
		}
		DynMaterial->SetScalarParameterValue(TEXT("ClearedPct"), ClearedPct);
	}

	// Dedicated servers don't render, so only clients (and a listen server's own view)
	// need the paint mask. The dedicated server only ever needs ClearedPct.
	const bool bCanRender = GetNetMode() != NM_DedicatedServer;
	if (bCanRender && BrushStampMaterial)
	{
		CoverageMask = UKismetRenderingLibrary::CreateRenderTarget2D(
			this, CoverageMaskSize.X, CoverageMaskSize.Y, RTF_RGBA8, FLinearColor::White);

		if (CoverageMask)
		{
			// White = still dusty. Brush stamps paint towards black (cleared) as characters sweep.
			UKismetRenderingLibrary::ClearRenderTarget2D(this, CoverageMask, FLinearColor::White);

			if (DynMaterial)
			{
				DynMaterial->SetTextureParameterValue(TEXT("CoverageMask"), CoverageMask);
			}
		}

		BrushDynMaterial = UMaterialInstanceDynamic::Create(BrushStampMaterial, this);
		if (BrushDynMaterial)
		{
			BrushDynMaterial->SetScalarParameterValue(TEXT("Radius"), BrushRadiusUV);
		}
	}
}

void ACWDustPatch::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		ServerTick(DeltaSeconds);
	}

	if (GetNetMode() != NM_DedicatedServer)
	{
		ClientTick(DeltaSeconds);
	}
}

void ACWDustPatch::ServerTick(float DeltaSeconds)
{
	if (ClearedPct >= 1.f)
	{
		return;
	}

	// Tools are singletons (see ACWToolPickup), so at most one character can be holding
	// the broom at all - this loop only ever finds that one sweeper, if they're in range.
	bool bAnySweeping = false;
	const float RangeSq = SweepDetectionRadius * SweepDetectionRadius;

	// TEMP DEBUG - remove once Use Tool is confirmed working end to end.
	bool bFoundAnyBroomHolder = false;

	for (TActorIterator<ACWCharacter> It(GetWorld()); It; ++It)
	{
		ACWCharacter* Character = *It;
		if (!Character || Character->GetEquippedToolType() != ECWToolType::Broom)
		{
			continue;
		}
		bFoundAnyBroomHolder = true;

		const float DistSq = FVector::DistSquared(Character->GetActorLocation(), GetActorLocation());
		FVector2D UV;
		const bool bInsidePatch = ProjectToLocalUV(Character->GetActorLocation(), UV);

		GEngine->AddOnScreenDebugMessage(202, 3.f, FColor::Green, FString::Printf(
			TEXT("%s: Using=%s  DistOK=%s (%.0f/%.0f)  InsidePatch=%s (UV=%.2f,%.2f)"),
			*GetName(),
			Character->IsUsingTool() ? TEXT("true") : TEXT("false"),
			DistSq <= RangeSq ? TEXT("true") : TEXT("false"), FMath::Sqrt(DistSq), SweepDetectionRadius,
			bInsidePatch ? TEXT("true") : TEXT("false"), UV.X, UV.Y));

		if (!Character->IsUsingTool() || DistSq > RangeSq || !bInsidePatch)
		{
			continue;
		}

		bAnySweeping = true;
		break;
	}

	if (!bFoundAnyBroomHolder)
	{
		GEngine->AddOnScreenDebugMessage(202, 3.f, FColor::Red, FString::Printf(
			TEXT("%s: no character is currently holding the Broom"), *GetName()));
	}

	if (bAnySweeping)
	{
		ClearedPct = FMath::Clamp(ClearedPct + DeltaSeconds / FMath::Max(SecondsToClear, KINDA_SMALL_NUMBER), 0.f, 1.f);

		// OnRep_ClearedPct only fires on remote clients receiving a replicated update - the
		// authority (server, or this same process in Standalone/Listen Server) never gets that
		// callback for its own writes, so it has to apply the material update itself.
		ApplyClearedPctToMaterial();
	}
}

void ACWDustPatch::ClientTick(float DeltaSeconds)
{
	if (ClearedPct >= 1.f || !CoverageMask || !BrushDynMaterial)
	{
		return;
	}

	const float RangeSq = SweepDetectionRadius * SweepDetectionRadius;

	for (TActorIterator<ACWCharacter> It(GetWorld()); It; ++It)
	{
		ACWCharacter* Character = *It;
		if (!Character || Character->GetEquippedToolType() != ECWToolType::Broom || !Character->IsUsingTool())
		{
			continue;
		}
		if (FVector::DistSquared(Character->GetActorLocation(), GetActorLocation()) > RangeSq)
		{
			continue;
		}

		PaintAt(Character);
	}
}

void ACWDustPatch::PaintAt(const ACWCharacter* Sweeper)
{
	FVector2D UV;
	if (!Sweeper || !ProjectToLocalUV(Sweeper->GetActorLocation(), UV))
	{
		return;
	}

	BrushDynMaterial->SetScalarParameterValue(TEXT("CenterU"), UV.X);
	BrushDynMaterial->SetScalarParameterValue(TEXT("CenterV"), UV.Y);
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, CoverageMask, BrushDynMaterial);
}

bool ACWDustPatch::ProjectToLocalUV(const FVector& WorldLocation, FVector2D& OutUV) const
{
	const FVector LocalOffset = GetActorRotation().UnrotateVector(WorldLocation - GetActorLocation());

	const float U = 0.5f + LocalOffset.X / (2.f * PatchExtent.X);
	const float V = 0.5f + LocalOffset.Y / (2.f * PatchExtent.Y);
	OutUV = FVector2D(U, V);

	return U >= 0.f && U <= 1.f && V >= 0.f && V <= 1.f;
}

void ACWDustPatch::OnRep_ClearedPct()
{
	ApplyClearedPctToMaterial();
}

void ACWDustPatch::ApplyClearedPctToMaterial()
{
	if (DynMaterial)
	{
		DynMaterial->SetScalarParameterValue(TEXT("ClearedPct"), ClearedPct);
	}
}

void ACWDustPatch::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACWDustPatch, ClearedPct);
}
