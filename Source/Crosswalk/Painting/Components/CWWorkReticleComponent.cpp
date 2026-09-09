#include "Painting/Components/CWWorkReticleComponent.h"

#include "Components/DecalComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Painting/CWPaintingSubsystem.h"
#include "Painting/Components/CWWorkerComponent.h"
#include "Painting/Zone/CWBaseWorkZone.h"
#include "Painting/Zone/CWWorkZone_Cells.h"

UCWWorkReticleComponent::UCWWorkReticleComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// 로컬 연출이라 복제할 것이 없다.
	SetIsReplicatedByDefault(false);
}

void UCWWorkReticleComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const AActor* Owner = GetOwner())
	{
		WorkerComponent = Owner->FindComponentByClass<UCWWorkerComponent>();
	}
}

void UCWWorkReticleComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 원을 못 그리는 이유. nullptr 이면 그릴 수 있다는 뜻이다.
	// 조기 반환을 이유별로 나눠 두면 디버그 표시가 "왜 안 뜨는가" 를 그대로 답할 수 있다.
	const TCHAR* HideReason = nullptr;

	if (!bEnabled)
	{
		HideReason = TEXT("bEnabled 꺼짐");
	}
	else if (WorkerComponent == nullptr)
	{
		HideReason = TEXT("WorkerComponent 를 못 찾음");
	}
	else if (!WorkerComponent->IsLocalWorker())
	{
		// 내가 조종하는 캐릭터에서만 그린다. 서버도, 남의 캐릭터도 아니다.
		HideReason = TEXT("내가 조종하는 캐릭터가 아님");
	}

	ACWWorkZone_Cells* Zone = nullptr;
	if (HideReason == nullptr)
	{
		Zone = FindReticleZone();
		if (Zone == nullptr)
		{
			HideReason = WorkerComponent->IsWorkBlocked()
				? TEXT("다운 상태")
				: TEXT("붓이 닿는 곳에 칸 존이 없음 (도구·거리 확인)");
		}
	}

	if (HideReason != nullptr)
	{
		HideReticle();
		PrintDebugState(HideReason, nullptr, 0.f);
		return;
	}

	// 붓 중심은 작업 컴포넌트가 이미 계산해 둔 값을 그대로 쓴다. 다시 트레이스하지 않는다. (기획서 8)
	const FVector BrushCenter = WorkerComponent->GetWorkLocation();

	// 실제로 칠해지는 월드 반경. 판정(초록/빨강)은 이 값 기준으로 돈다.
	const float PaintedRadius = Zone->GetPaintedWorldRadius(WorkerComponent);

	// 화면에 그리는 반경은 여기서 실제 반경과 갈라진다.
	//
	// 붓이 작을 때 원이 점처럼 보여 조준을 도와주지 못하기 때문에 최소 크기를 보장한다.
	// 그 대신 "원 안쪽이 곧 칠해질 범위" 라는 보장은 포기했다. 원은 위치 표지에 가깝다.
	// (MinReticleRadius 주석 참고)
	const float DrawRadius = FMath::Max(PaintedRadius * ReticleRadiusScale, MinReticleRadius);

	/**
	 * 초록이냐 빨강이냐. 두 가지뿐이다. (기획서 7-2)
	 *
	 * HasWorkAt 은 "이 붓질이 뒤집을 칸이 하나라도 있는가" 를 ApplyWork 와 똑같은 기준으로 답한다.
	 * 그래서 덧칠 판정을 여기서 새로 만들 필요가 없고, 원이 초록인데 안 칠해지는 일도 없다.
	 *
	 * 페인트는 IsPaintDepleted 가 아니라 HasPaintForWork 를 본다. 전자는 "작업 키를 눌렀는데
	 * 페인트가 없었다" 라서 키를 누르기 전에는 항상 false 다. 조준 표시기는 누르기 전에
	 * 알려주는 것이 목적이라 잔량 자체를 봐야 한다.
	 */
	const bool bWorkable = Zone->HasWorkAt(WorkerComponent, BrushCenter) && WorkerComponent->HasPaintForWork();

	ShowReticle(BrushCenter, DrawRadius, bWorkable ? WorkableColor : BlockedColor, DeltaTime);

	PrintDebugState(bWorkable ? TEXT("표시 중 (초록)") : TEXT("표시 중 (빨강)"), Zone, DrawRadius);
}

ACWWorkZone_Cells* UCWWorkReticleComponent::FindReticleZone() const
{
	// 다운되어 있으면 어차피 작업이 안 된다.
	if (WorkerComponent->IsWorkBlocked())
	{
		return nullptr;
	}

	/**
	 * 1순위 — 작업 컴포넌트가 고른 존.
	 *
	 * 칸을 칠하는 존만 대상이다. 게이지 존과 테두리 미니게임은 조준할 "지점" 자체가 없다.
	 * 도구는 따로 보지 않는다. TargetZone 은 FindTargetZone 이 CanWorkNow(HeldTool) 로 이미 걸렀다.
	 */
	if (ACWWorkZone_Cells* Zone = Cast<ACWWorkZone_Cells>(WorkerComponent->GetTargetZone()))
	{
		return Zone;
	}

	/**
	 * 2순위 — "할 일이 없어서" 대상에서 빠진 존을 다시 찾는다.
	 *
	 * 정리 도구(검은색 롤러)는 CanWorkNow 가 GetCleanableCellCount() > 0 을 보기 때문에,
	 * 다 지우고 나면 존이 통째로 대상에서 빠진다. 그러면 원이 빨강으로 바뀌는 게 아니라
	 * 그냥 사라진다. 발밑에 존이 멀쩡히 있는데 아무 표시가 없으니 "고장" 으로 보인다.
	 *
	 * 페인트가 떨어졌을 때 숨기지 않고 색만 바꾸는 것과 같은 이유다. (기획서 5)
	 * 지울 게 없다는 것도 "눌러도 진행되지 않는다" 이고, 그건 빨강이 답할 몫이다.
	 *
	 * 판정은 전부 존에게 맡긴다. 여기서 새로 만드는 기준은 없다.
	 *   CanUseTool      — 이 도구가 이 존에 들어올 수 있는가 (CanWorkNow 에서 할 일 유무만 뺀 것)
	 *   IsWorkerInRange — 붓이 닿는 곳에 이 존의 칸이 있는가 (칸 상태는 보지 않는다)
	 */
	const UCWPaintingSubsystem* PaintingSubsystem = UCWPaintingSubsystem::Get(this);
	if (PaintingSubsystem == nullptr)
	{
		return nullptr;
	}

	const FVector WorkLocation = WorkerComponent->GetWorkLocation();
	const ECWToolType HeldTool = WorkerComponent->GetHeldTool();

	ACWWorkZone_Cells* NearestZone = nullptr;
	float NearestDistanceSquared = TNumericLimits<float>::Max();

	for (const TWeakObjectPtr<ACWBaseWorkZone>& WeakZone : PaintingSubsystem->GetZones())
	{
		ACWWorkZone_Cells* Zone = Cast<ACWWorkZone_Cells>(WeakZone.Get());
		if (Zone == nullptr || Zone->IsLocked() || !Zone->CanUseTool(HeldTool))
		{
			continue;
		}

		if (!Zone->IsWorkerInRange(WorkerComponent, WorkLocation))
		{
			continue;
		}

		const float DistanceSquared = Zone->GetSquaredDistanceToPoint(WorkLocation);
		if (DistanceSquared >= NearestDistanceSquared)
		{
			continue;
		}

		NearestDistanceSquared = DistanceSquared;
		NearestZone = Zone;
	}

	return NearestZone;
}

bool UCWWorkReticleComponent::EnsureDecal()
{
	if (ReticleDecal != nullptr)
	{
		return true;
	}

	if (bDecalSetupFailed)
	{
		return false;
	}

	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return false;
	}

	if (ReticleMaterial == nullptr)
	{
		// 조용히 안 그리면 "기능이 없는 것" 과 구분이 안 된다. 한 번은 이유를 남긴다.
		bDecalSetupFailed = true;
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] 조준 표시기 머티리얼이 없습니다. Reticle Material 프로퍼티를 확인하세요."),
			*Owner->GetName());
		return false;
	}

	ReticleDecal = NewObject<UDecalComponent>(Owner, NAME_None, RF_Transient);
	ReticleDecal->SetupAttachment(Owner->GetRootComponent());
	ReticleDecal->SetDecalMaterial(ReticleMaterial);
	ReticleDecal->SetSortOrder(DecalSortOrder);

	/**
	 * 화면에서 작아졌을 때 사라지는 최적화를 끈다.
	 *
	 * 붓 반경은 20cm 정도인데 쿼터뷰 카메라는 멀리 있다. 기본값(0.01)이면 체온이 올라 붓이
	 * 작아졌을 때 원이 조용히 사라진다. 하필 붓이 작아진 것을 확인해야 할 때 안 보이게 된다.
	 */
	ReticleDecal->SetFadeScreenSize(0.f);

	// 붙자마자 보이지 않게 한다. 위치를 잡기 전 한 프레임이 엉뚱한 자리에 찍히는 것을 막는다.
	ReticleDecal->SetVisibility(false);
	ReticleDecal->RegisterComponent();

	ReticleMaterialInstance = ReticleDecal->CreateDynamicMaterialInstance();
	if (ReticleMaterialInstance == nullptr)
	{
		bDecalSetupFailed = true;
		UE_LOG(LogTemp, Warning, TEXT("[%s] 조준 표시기 머티리얼 인스턴스를 만들지 못했습니다."), *Owner->GetName());
		return false;
	}

	// 파라미터 이름이 안 맞으면 값만 조용히 버려지고 원은 머티리얼 기본값으로 그려진다.
	// 하나만 확인해도 계약이 맞는지는 알 수 있다.
	float ProbeValue = 0.f;
	if (!ReticleMaterialInstance->GetScalarParameterValue(FillAlphaParameterName, ProbeValue))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] 조준 표시기 머티리얼에 '%s' 파라미터가 없습니다. 색과 두께가 반영되지 않습니다."),
			*Owner->GetName(), *FillAlphaParameterName.ToString());
	}

	return true;
}

void UCWWorkReticleComponent::ShowReticle(const FVector& Location, float Radius, const FLinearColor& TargetColor, float DeltaTime)
{
	if (!EnsureDecal())
	{
		return;
	}

	const bool bWasVisible = ReticleDecal->IsVisible();

	// 방금 뜬 원은 목표 색에서 시작한다. 이어서 그리는 중일 때만 색을 섞는다.
	// 안 그러면 존에 들어설 때마다 지난번 색에서 번져 나온다.
	const float BlendAlpha = (bWasVisible && ColorBlendTime > 0.f)
		? FMath::Clamp(DeltaTime / ColorBlendTime, 0.f, 1.f)
		: 1.f;
	CurrentColor = FMath::Lerp(CurrentColor, TargetColor, BlendAlpha);

	/**
	 * 데칼은 컴포넌트의 +X 방향으로 투영된다. Pitch -90 으로 X 를 아래로 돌려야 바닥에 찍힌다.
	 *
	 * 상대 회전이 아니라 월드 회전이어야 한다. 부모가 캐릭터라, 상대 회전으로 두면 캐릭터가
	 * 도는 대로 원이 같이 기울어져 비스듬히 투영된다.
	 * (DecalSize 의 X 가 투영 깊이, Y/Z 가 평면 반쪽 크기다)
	 */
	ReticleDecal->SetWorldLocationAndRotation(Location, FRotator(-90.f, 0.f, 0.f));
	ReticleDecal->DecalSize = FVector(DecalProjectionDepth, Radius, Radius);
	ReticleDecal->MarkRenderStateDirty();

	if (ReticleMaterialInstance != nullptr)
	{
		ReticleMaterialInstance->SetVectorParameterValue(ColorParameterName, CurrentColor);
		ReticleMaterialInstance->SetScalarParameterValue(FillAlphaParameterName, FillAlpha);

		// 두께는 반경 대비 비율로 넘긴다. 머티리얼은 원의 크기를 모르고 UV 만 보기 때문이다.
		// 붓이 작아져도 MinEdgeThickness 만큼은 남기고, 반대로 선이 원을 다 먹지 않게 절반에서 자른다.
		const float SafeRadius = FMath::Max(Radius, KINDA_SMALL_NUMBER);
		const float ThicknessRatio = FMath::Max(EdgeThicknessRatio, MinEdgeThickness / SafeRadius);
		ReticleMaterialInstance->SetScalarParameterValue(EdgeThicknessParameterName, FMath::Min(ThicknessRatio, 0.5f));
	}

	if (!bWasVisible)
	{
		ReticleDecal->SetVisibility(true);
	}
}

void UCWWorkReticleComponent::HideReticle()
{
	if (ReticleDecal != nullptr && ReticleDecal->IsVisible())
	{
		ReticleDecal->SetVisibility(false);
	}
}

namespace
{
	// 디버그 구를 지면에서 띄우는 높이(cm). 조준 원을 가리지 않을 만큼만 띄운다.
	constexpr float DebugSphereHeight = 60.f;
}

void UCWWorkReticleComponent::PrintDebugState(const TCHAR* State, const ACWWorkZone_Cells* Zone, float Radius) const
{
	if (!bShowDebugReticle || GEngine == nullptr)
	{
		return;
	}

	// 남의 캐릭터 것까지 겹쳐 찍히지 않게 막는다. WorkerComponent 가 없으면 그것부터 알아야 하므로 통과시킨다.
	if (WorkerComponent != nullptr && !WorkerComponent->IsLocalWorker())
	{
		return;
	}

	const AActor* Owner = GetOwner();
	const ACWBaseWorkZone* TargetZone = WorkerComponent != nullptr ? WorkerComponent->GetTargetZone() : nullptr;

	// 키를 고정해야 매 프레임 새 줄이 쌓이지 않고 한 줄이 갱신된다.
	GEngine->AddOnScreenDebugMessage(
		(uint64)GetUniqueID(), 0.f, FColor::Yellow,
		FString::Printf(TEXT("[조준 표시기] %s | 존=%s | 반경=%.1f | 데칼=%s | 머티리얼=%s"),
			State,
			TargetZone != nullptr ? *TargetZone->GetClass()->GetName() : TEXT("없음"),
			Radius,
			ReticleDecal != nullptr ? (ReticleDecal->IsVisible() ? TEXT("보임") : TEXT("숨김")) : TEXT("없음"),
			ReticleMaterialInstance != nullptr ? TEXT("OK") : (ReticleMaterial != nullptr ? TEXT("MID 실패") : TEXT("미지정"))));

	// 두 번째 줄 — 그리는 원과 실제 칠해지는 범위가 얼마나 벌어져 있는지.
	//
	// 표시 크기를 일부러 붓 범위에서 떼어냈기 때문에(MinReticleRadius) 둘이 다른 것이 정상이다.
	// 다만 얼마나 다른지는 보이는 편이 낫다. 밸런스를 볼 때 붓 실측값이 필요하다.
	if (Zone != nullptr)
	{
		const FVector ZoneScale = Zone->GetActorScale3D();
		GEngine->AddOnScreenDebugMessage(
			(uint64)GetUniqueID() + 1, 0.f, FColor::Cyan,
			FString::Printf(TEXT("[조준 표시기] 그리는 원 반경=%.1f (지름 %.1f) | 실제 칠해지는 반경=%.1f | 붓(로컬)=%.1f | 존 스케일=(%.2f, %.2f)"),
				Radius,
				Radius * 2.f,
				Zone->GetPaintedWorldRadius(WorkerComponent),
				Zone->GetEffectiveBrushRadius(WorkerComponent),
				ZoneScale.X, ZoneScale.Y));
	}

	// 붓 중심이 실제로 어디인지. 원이 안 보일 때 "위치가 엉뚱한가" 를 가른다.
	//
	// 지면에서 띄워서 그린다. 바닥에 딱 붙여 그렸더니 조준 원과 같은 자리·같은 크기라
	// 원을 통째로 덮어버렸다. 확인하려고 켠 표시가 확인 대상을 가리면 안 된다.
	if (Owner != nullptr && WorkerComponent != nullptr && Radius > 0.f)
	{
		const FVector DebugCenter = WorkerComponent->GetWorkLocation() + FVector(0.f, 0.f, DebugSphereHeight);
		DrawDebugSphere(GetWorld(), DebugCenter, Radius, 16, FColor::Yellow, false, -1.f, 0, 0.5f);
		DrawDebugLine(GetWorld(), WorkerComponent->GetWorkLocation(), DebugCenter, FColor::Yellow, false, -1.f, 0, 0.5f);
	}
}
