// Copyright Crosswalk Team. All Rights Reserved.
// CWPaintDecalField.cpp

#include "Painting/Zone/CWPaintDecalField.h"

#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Painting/CWPaintingSubsystem.h"
#include "Painting/Components/CWWorkerComponent.h"

ACWPaintDecalField::ACWPaintDecalField()
{
	// 자국이 생길 수 있는 범위. 레벨에서 오염 레이어와 비슷하게 잡는다.
	Area->SetBoxExtent(FVector(1000.f, 1000.f, 100.f));

	// 자국이 하나라도 남아 있으면 완료가 아니다. 부모 기본값(0.985)은
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
	//
	// 그 전에 이미 있던 자국의 "처음 본 시각" 을 과거로 적어 둔다. 안 그러면 접속하자마자
	// 예전에 난 사고의 자국들이 한꺼번에 퍼지기 시작한다. 지금 막 생긴 것처럼 보이기 때문이다.
	if (const UWorld* World = GetWorld())
	{
		const float PastTime = World->GetTimeSeconds() - GrowDuration;
		for (const FCWPaintDecal& Decal : Decals)
		{
			LocalSpawnTimes.Add(Decal.Id, PastTime - Decal.SpawnDelay);
		}
	}

	RefreshDecalComponents();
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

		// 완전히 0 이 될 때까지 기다리지 않고 임계치에서 끊는다.
		// 끝까지 기다리면 눈에 안 보일 만큼 작아진 자국이 남아, 다 치운 것처럼 보이는데
		// 진행도는 오르지 않는 상태가 생긴다.
		if (Decal.Remaining <= FMath::Max(RemoveRemainingThreshold, KINDA_SMALL_NUMBER))
		{
			// 남은 몫도 문지른 것으로 센다. 자국 하나를 지우는 비용을
			// PaintCostPerDecal 그대로 유지하기 위함이다. (임계치에서 끊어도 총액은 같다)
			TotalScrubbed += Decal.Remaining;

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

	// 이번 사고로 실제로 만든 자국 수. 시차의 기준이라 걸러지거나 합쳐진 것은 세지 않는다.
	int32 SpawnedCount = 0;

	for (int32 Index = 0; Index < NewDecalCount; ++Index)
	{
		// 상한에 닿으면 더 만들지 않는다. 자국 하나가 데칼 컴포넌트 하나라 개수가 곧 비용이다.
		if (Decals.Num() >= MaxDecals)
		{
			break;
		}

		// 흩어지는 위치는 서버만 정하고 결과를 전파한다. 각자 뽑으면 자국 자리가 화면마다 다르다.
		const FVector2D Offset = FMath::RandPointInCircle(ScatterRadius);
		const FVector ScatteredLocation = BurstLocation + FVector(Offset.X, Offset.Y, 0.f);

		// 범위를 벗어난 자국은 만들지 않는다. 흩어지다 인도로 넘어간 것들이 여기서 걸러진다.
		if (!ContainsAreaPoint(ScatteredLocation))
		{
			continue;
		}

		// 합쳐진 자국도 이번 사고의 일부라 같은 시차를 받는다.
		const float SpawnDelay = SpawnedCount * SpawnDelayPerDecal;

		// 이미 자국이 있는 자리면 그것을 되살리고 넘어간다.
		// 지면 투영 전에 판단한다. 투영은 높이만 바꾸고 겹침은 평면 거리로만 보기 때문에
		// 합쳐질 자국까지 지면을 찾는 것은 헛일이다.
		if (TryMergeIntoExisting(ScatteredLocation, SpawnDelay))
		{
			++SpawnedCount;
			continue;
		}

		// 자국마다 따로 지면을 찾는다. 터진 지점의 높이를 그대로 쓰면
		// 인도 턱이나 경사면에 흩어진 자국이 바닥에서 떠 보인다.
		FCWPaintDecal& NewDecal = Decals.AddDefaulted_GetRef();
		NewDecal.Id = NextDecalId++;
		NewDecal.Location = PaintingSubsystem->ProjectToGround(ScatteredLocation, IgnoredActor);
		NewDecal.Radius = DecalRadius;
		NewDecal.Remaining = 1.f;
		NewDecal.SpawnDelay = SpawnDelay;

		++SpawnedCount;
	}

	RefreshDecalComponents();
	RefreshProgress();
}

void ACWPaintDecalField::RefreshProgress()
{
	// 개수가 아니라 남은 양의 합계로 센다. 겹친 사고는 새 자국을 만들지 않고 기존 자국을
	// 되살리기 때문에, 개수로 세면 같은 자리에 거듭난 사고가 진행도에 잡히지 않는다.
	float RemainingTotal = 0.f;
	for (const FCWPaintDecal& Decal : Decals)
	{
		RemainingTotal += Decal.Remaining;
	}

	PeakRemainingTotal = FMath::Max(PeakRemainingTotal, RemainingTotal);

	if (PeakRemainingTotal <= KINDA_SMALL_NUMBER)
	{
		SetProgress(1.f);
		return;
	}

	// 한 번에 가장 많이 쌓였던 양을 분모로 쓴다. 사고가 나면 진행도가 뒤로 가고,
	// 지우면 다시 앞으로 간다. (기획서 4-1)
	SetProgress(1.f - RemainingTotal / PeakRemainingTotal);
}

bool ACWPaintDecalField::TryMergeIntoExisting(const FVector& Location, float SpawnDelay)
{
	// 0 이면 합치지 않는다. 예전처럼 겹쳐도 계속 쌓인다.
	if (MergeRadiusScale <= 0.f)
	{
		return false;
	}

	const float MergeRadius = DecalRadius * MergeRadiusScale;

	for (FCWPaintDecal& Decal : Decals)
	{
		// 높이는 보지 않는다. 자국은 바닥에 깔리는 것이라 평면으로만 판단한다.
		if (FVector::DistSquared2D(Decal.Location, Location) > FMath::Square(MergeRadius))
		{
			continue;
		}

		// 남은 양을 되돌린다. 개수는 그대로지만 지울 거리가 늘어나서
		// 겨우 지워놓은 자리가 다시 더러워진 것으로 읽힌다.
		Decal.Remaining = 1.f;

		// 번호를 새로 매겨 퍼짐을 다시 태운다. 번호를 그대로 두면 각 머신이 이미 아는
		// 자국이라 판단해서, 남은 양만 조용히 늘고 화면에는 아무 일도 일어나지 않는다.
		Decal.Id = NextDecalId++;
		Decal.SpawnDelay = SpawnDelay;
		return true;
	}

	return false;
}

float ACWPaintDecalField::GetLocalSpawnTime(int32 DecalId)
{
	if (const float* Found = LocalSpawnTimes.Find(DecalId))
	{
		return *Found;
	}

	// 처음 보는 자국이다. 지금부터 퍼지기 시작한다.
	const UWorld* World = GetWorld();
	const float Now = World != nullptr ? World->GetTimeSeconds() : 0.f;

	LocalSpawnTimes.Add(DecalId, Now);
	return Now;
}

void ACWPaintDecalField::PruneLocalSpawnTimes()
{
	// 살아 있는 자국보다 기록이 많을 때만 훑는다. 대개는 여기서 끝난다.
	if (LocalSpawnTimes.Num() <= Decals.Num())
	{
		return;
	}

	TSet<int32> AliveIds;
	AliveIds.Reserve(Decals.Num());
	for (const FCWPaintDecal& Decal : Decals)
	{
		AliveIds.Add(Decal.Id);
	}

	for (TMap<int32, float>::TIterator It = LocalSpawnTimes.CreateIterator(); It; ++It)
	{
		if (!AliveIds.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
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
			// 자국마다 MID 를 따로 둔다. 퍼지기 시작하는 시각이 자국마다 다르기 때문에
			// 머티리얼을 공유하면 한 자국의 시각이 나머지 전부를 덮어쓴다.
			NewDecal->SetDecalMaterial(UMaterialInstanceDynamic::Create(DecalMaterial, this));
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
		// 다만 MinVisibleScale 밑으로는 줄이지 않는다. 0 까지 줄이면 사라지기 직전의 자국이
		// 보이지 않아, 남아 있는데도 다 지웠다고 착각하게 된다.
		// (DecalSize 의 X 가 투영 깊이, Y/Z 가 평면 크기다)
		const float VisibleScale = FMath::Lerp(MinVisibleScale, 1.f, FMath::Clamp(Decal.Remaining, 0.f, 1.f));
		const float VisibleRadius = Decal.Radius * VisibleScale;
		Component->DecalSize = FVector(DecalProjectionDepth, VisibleRadius, VisibleRadius);
		Component->MarkRenderStateDirty();

		// 퍼짐은 머티리얼이 Time 노드로 스스로 돈다. 여기서는 언제부터 언제까지인지만 넘긴다.
		// 그래서 퍼지는 동안 Tick 이 필요 없다.
		//
		// 자국 Id 로 시각을 찾는 것이 중요하다. 가운데 자국이 지워지면 인덱스가 밀려서
		// 이 컴포넌트가 담당하는 자국이 바뀌는데, 매번 다시 찾으므로 영향을 받지 않는다.
		if (UMaterialInstanceDynamic* DecalMID = Cast<UMaterialInstanceDynamic>(Component->GetDecalMaterial()))
		{
			DecalMID->SetScalarParameterValue(TEXT("GrowStartTime"), GetLocalSpawnTime(Decal.Id) + Decal.SpawnDelay);

			// 0 을 그대로 넘기면 머티리얼에서 0 으로 나눈다. 아주 짧은 시간으로 바꿔
			// 사실상 즉시 나타나게 한다.
			DecalMID->SetScalarParameterValue(TEXT("GrowDuration"), FMath::Max(GrowDuration, KINDA_SMALL_NUMBER));

			// 문지른 정도. 머티리얼이 색이나 거칠기를 바꾸는 데 쓸 수 있다.
			DecalMID->SetScalarParameterValue(TEXT("Remaining"), Decal.Remaining);
		}
	}

	// 지워진 자국의 시각 기록을 버린다. 판이 길어져도 맵이 쌓이지 않는다.
	PruneLocalSpawnTimes();
}

FString ACWPaintDecalField::GetDebugDetailText() const
{
	return FString::Printf(TEXT(" 자국 %d개 (남은 양 최대 %.1f)"), Decals.Num(), PeakRemainingTotal);
}
