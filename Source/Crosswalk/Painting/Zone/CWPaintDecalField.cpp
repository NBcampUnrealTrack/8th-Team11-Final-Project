// Copyright Crosswalk Team. All Rights Reserved.
// CWPaintDecalField.cpp

#include "Painting/Zone/CWPaintDecalField.h"

#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "Net/UnrealNetwork.h"
#include "Painting/CWPaintingSubsystem.h"
#include "Painting/Components/CWWorkerComponent.h"

ACWPaintDecalField::ACWPaintDecalField()
{
	PrimaryActorTick.bCanEverTick = false;

	// 자국이 생길 수 있는 범위. 레벨에서 오염 레이어와 비슷하게 잡는다.
	Area->SetBoxExtent(FVector(1000.f, 1000.f, 100.f));

	// 자국을 남기는 것은 사고다. 앞 단계를 기다릴 이유가 없다.
	// WorkOrder 가 0 이면 자기보다 앞선 존이 없어서 잠기지 않는다.
	WorkOrder = 0;

	// 자국이 하나라도 남아 있으면 완료가 아니다. 칸 존의 기본값(0.98)은
	// 격자 구석까지 칠하기 어려운 도색 작업을 위한 여유라 여기와는 맞지 않는다.
	CompleteThreshold = 1.f;

	// 배치할 때 빼먹지 않도록 기본값으로 넣어 둔다.
	CleanupTools.Add(ECWToolType::BlackRoller);

	// 자국이 하나도 없는 상태가 곧 완료다. 시작부터 깨끗하다.
	InitialProgress = 1.f;
}

void ACWPaintDecalField::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACWPaintDecalField, Decals);
}

void ACWPaintDecalField::BeginPlay()
{
	Super::BeginPlay();

	// 도중에 들어온 플레이어라면 목록이 이미 채워져 있다. 여기서 한 번 그려준다.
	RefreshDecalComponents();
}

bool ACWPaintDecalField::IsCleanupTool(ECWToolType ToolType) const
{
	return ToolType != ECWToolType::None && CleanupTools.Contains(ToolType);
}

bool ACWPaintDecalField::CanWorkNow(ECWToolType ToolType) const
{
	// 자국을 지우는 도구만 받는다. 흰 롤러로 자국을 덧칠할 수는 없다.
	if (!IsCleanupTool(ToolType))
	{
		return false;
	}

	// 완료 여부를 보지 않는다. 다 치운 뒤에 또 사고가 나면 다시 열려야 한다.
	// 대신 지울 게 없으면 대상에서 빠진다. 안 그러면 빈 필드가 계속 붓을 붙잡는다.
	return !IsLocked() && Decals.Num() > 0;
}

bool ACWPaintDecalField::IsWorkerInRange(const UCWWorkerComponent* Worker, const FVector& WorkLocation) const
{
	// Area 박스 거리로 재지 않는다. 필드는 도로 전체를 덮을 만큼 넓어서
	// 반대편 끝에 서 있어도 거리가 0 이라 제약이 되지 않는다.
	//
	// 칸 존과 같은 기준을 쓴다. 붓이 닿는 곳에 지울 것이 있으면 대상이다.
	// (붓 중심은 이미 UCWWorkerComponent::MaxBrushDistance 가 캐릭터 주변으로 묶어서 넘겨준다)
	return Worker != nullptr && HasDecalInRadius(WorkLocation, Worker->GetMaxWorkDistance());
}

bool ACWPaintDecalField::HasWorkAt(const UCWWorkerComponent* Worker, const FVector& WorldLocation) const
{
	return HasDecalInRadius(WorldLocation, DecalRadius);
}

bool ACWPaintDecalField::HasDecalInRadius(const FVector& WorldCenter, float Radius) const
{
	for (const FCWPaintDecal& Decal : Decals)
	{
		// 높이는 보지 않는다. 자국은 바닥에 깔리는 것이라 평면으로만 판단한다.
		if (FVector::DistSquared2D(Decal.Location, WorldCenter) <= FMath::Square(Radius + Decal.Radius))
		{
			return true;
		}
	}

	return false;
}

void ACWPaintDecalField::ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds)
{
	// 지우는 판정은 서버만 한다. 클라이언트가 멋대로 지우면 자국이 화면마다 달라진다.
	// 클라이언트의 커서 좌표는 작업 컴포넌트가 이미 올려주고 있어서 추가 RPC 가 필요 없다.
	if (!HasAuthority() || Worker == nullptr || !CanWorkNow(Worker->GetHeldTool()))
	{
		return;
	}

	// 넘어온 시간에는 체온 계수가 이미 곱해져 있다. 뜨거우면 그만큼 느리게 지워진다.
	const float ScrubAmount = DeltaSeconds / FMath::Max(SecondsToScrub, KINDA_SMALL_NUMBER);
	if (ScrubAmount <= 0.f)
	{
		return;
	}

	float TotalScrubbed = 0.f;
	bool bAnyRemoved = false;

	// 뒤에서부터 도는 이유: 다 지워진 자국을 도는 중에 바로 빼내기 위해서다.
	for (int32 Index = Decals.Num() - 1; Index >= 0; --Index)
	{
		FCWPaintDecal& Decal = Decals[Index];

		if (FVector::DistSquared2D(Decal.Location, WorldLocation) > FMath::Square(DecalRadius + Decal.Radius))
		{
			continue;
		}

		// 남은 양보다 많이 문질러도 남은 만큼만 소모된다.
		const float Applied = FMath::Min(ScrubAmount, Decal.Remaining);

		Decal.Remaining -= Applied;
		TotalScrubbed += Applied;

		if (Decal.Remaining <= KINDA_SMALL_NUMBER)
		{
			Decals.RemoveAt(Index);
			bAnyRemoved = true;
		}
	}

	if (TotalScrubbed <= 0.f)
	{
		return;
	}

	// 지운 만큼 페인트를 쓴다. (기획서 5-2 "4단계 정리도 페인트를 소모")
	// 자국 하나를 다 지우면 정확히 PaintCostPerDecal 이 든다.
	Worker->ConsumePaint(TotalScrubbed * PaintCostPerDecal);

	// 남은 양만 줄어든 경우에도 화면은 갱신해야 한다. 자국이 옅어지는 것이 진척 표시다.
	RefreshDecalComponents();

	if (bAnyRemoved)
	{
		RefreshProgress();
	}
}

void ACWPaintDecalField::AddBurst(const FVector& BurstLocation, int32 BrokenCount, const AActor* IgnoredActor)
{
	if (!HasAuthority() || BrokenCount <= 0)
	{
		return;
	}

	// 자국이 생길 수 있는 범위 밖이면 아무것도 남기지 않는다.
	// 인도나 맵 밖에서 터진 사고까지 정리 대상이 되면 치울 수 없는 오염이 생긴다.
	if (!ContainsAreaPoint(BurstLocation))
	{
		return;
	}

	UCWPaintingSubsystem* PaintingSubsystem = UCWPaintingSubsystem::Get(this);
	if (PaintingSubsystem == nullptr)
	{
		return;
	}

	const int32 NewDecalCount = DecalsPerCan * BrokenCount;
	const float ScatterRadius = ScatterRadiusPerCan * BrokenCount;

	for (int32 Index = 0; Index < NewDecalCount; ++Index)
	{
		// 흩어지는 위치는 서버만 정하고 결과를 전파한다. 각자 뽑으면 자국 자리가 화면마다 다르다.
		const FVector2D Offset = FMath::RandPointInCircle(ScatterRadius);
		const FVector ScatteredLocation = BurstLocation + FVector(Offset.X, Offset.Y, 0.f);

		// 범위를 벗어난 자국은 만들지 않는다. 흩어지다 인도로 넘어간 것들이 여기서 걸러진다.
		if (!ContainsAreaPoint(ScatteredLocation))
		{
			continue;
		}

		// 자국마다 따로 지면을 찾는다. 터진 지점의 높이를 그대로 쓰면
		// 인도 턱이나 경사면에 흩어진 자국이 바닥에서 떠 보인다.
		FCWPaintDecal& NewDecal = Decals.AddDefaulted_GetRef();
		NewDecal.Location = PaintingSubsystem->ProjectToGround(ScatteredLocation, IgnoredActor);
		NewDecal.Radius = DecalRadius;
		NewDecal.Remaining = 1.f;
	}

	RefreshDecalComponents();
	RefreshProgress();
}

void ACWPaintDecalField::RefreshProgress()
{
	PeakDecalCount = FMath::Max(PeakDecalCount, Decals.Num());

	if (PeakDecalCount <= 0)
	{
		SetProgress(1.f);
		return;
	}

	// 한 번에 가장 많이 쌓였던 수를 분모로 쓴다. 사고가 나면 진행도가 뒤로 가고,
	// 지우면 다시 앞으로 간다. (기획서 4-1)
	SetProgress(1.f - static_cast<float>(Decals.Num()) / static_cast<float>(PeakDecalCount));
}

void ACWPaintDecalField::OnRep_Decals()
{
	RefreshDecalComponents();
}

void ACWPaintDecalField::RefreshDecalComponents()
{
	// 데디케이티드 서버는 화면을 그리지 않는다. 판정은 Decals 배열이 이미 갖고 있다.
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 모자라면 만들고, 남으면 지운다. 매번 전부 다시 만들지 않는다.
	while (DecalComponents.Num() < Decals.Num())
	{
		UDecalComponent* NewDecal = NewObject<UDecalComponent>(this);
		NewDecal->SetupAttachment(GetRootComponent());
		NewDecal->RegisterComponent();

		// 데칼은 컴포넌트의 +X 방향으로 투영된다. Pitch -90 으로 X 를 아래로 돌려야 바닥에 찍힌다.
		//
		// 상대 회전이 아니라 월드 회전으로 넣는다. 부모가 Area 박스라, 레벨에서 존을 돌려놓으면
		// 상대 회전은 그만큼 같이 기울어져 자국이 비스듬히 투영된다.
		NewDecal->SetWorldRotation(FRotator(-90.f, 0.f, 0.f));

		if (DecalMaterial != nullptr)
		{
			NewDecal->SetDecalMaterial(DecalMaterial);
		}

		DecalComponents.Add(NewDecal);
	}

	while (DecalComponents.Num() > Decals.Num())
	{
		if (UDecalComponent* Extra = DecalComponents.Pop())
		{
			Extra->DestroyComponent();
		}
	}

	for (int32 Index = 0; Index < Decals.Num(); ++Index)
	{
		UDecalComponent* Component = DecalComponents[Index];
		if (Component == nullptr)
		{
			continue;
		}

		const FCWPaintDecal& Decal = Decals[Index];

		Component->SetWorldLocation(Decal.Location);

		// 문지를수록 작아진다. 전용 머티리얼이 없어도 지워지는 것이 눈에 보이게 하기 위함이다.
		// (DecalSize 의 X 가 투영 깊이, Y/Z 가 평면 크기다)
		const float VisibleRadius = Decal.Radius * Decal.Remaining;
		Component->DecalSize = FVector(DecalProjectionDepth, VisibleRadius, VisibleRadius);
		Component->MarkRenderStateDirty();
	}
}

FString ACWPaintDecalField::GetDebugDetailText() const
{
	return FString::Printf(TEXT(" 자국 %d개 (최대 %d)"), Decals.Num(), PeakDecalCount);
}
