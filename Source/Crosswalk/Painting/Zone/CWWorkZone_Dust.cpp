#include "Painting/Zone/CWWorkZone_Dust.h"

#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Crc.h"
#include "Painting/Components/CWWorkerComponent.h"

namespace
{
	/**
	 * M_DustGround 와의 계약이다. 이름이 어긋나면 값이 조용히 버려지고,
	 * "진행도는 오르는데 겉모습이 안 바뀐다" 가 된다. 패치 머티리얼을 만들 때(CreatePatchMaterials) 한 번 검사한다.
	 */
	const FName DustClearedParameterName(TEXT("ClearedPct"));
	const FName DustPatchFadeParameterName(TEXT("PatchFade"));
	const FName DustTilingParameterName(TEXT("Tiling"));
	const FName DustUVOffsetParameterName(TEXT("UVOffset"));
	const FName DustTintParameterName(TEXT("DustTint"));
}

ACWWorkZone_Dust::ACWWorkZone_Dust()
{
	Area->SetBoxExtent(FVector(100.f, 50.f, 20.f));

	/**
	 * 먼지가 다 걷히는 지점에서 완료로 넘긴다.
	 *
	 * 패치 알파는 이 값을 100% 로 보고 계산한다. (ComputePatchFade 참고) 그래서 진행도가
	 * 여기 닿는 순간 마지막 패치까지 정확히 사라진다. 남은 0.05 를 더 쓸게 하면 이미 깨끗한
	 * 바닥을 계속 쓸어야 하고, "다 쓸었는데 왜 안 끝나지" 로 헤매게 된다.
	 *
	 * 부모 기본값(0.985)은 격자 칸 존을 위한 값이라 그대로 쓰지 않는다.
	 */
	CompleteThreshold = 0.95f;

	// 이 존은 빗자루 전용이다. 기본값으로 넣어 두면 배치할 때 빼먹을 일이 없다.
	AcceptedTools.Add(ECWToolType::Broom);
}

void ACWWorkZone_Dust::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 에디터에서도 돌기 때문에 배치를 눈으로 보고 잡을 수 있다.
	BuildPatches();
}

void ACWWorkZone_Dust::BeginPlay()
{
	Super::BeginPlay();

	/**
	 * 여기서 패치를 다시 만든다. OnConstruction 이 만든 것에 기댈 수 없다.
	 *
	 * 두 가지가 겹친다. 패치 컴포넌트는 RF_Transient 라 PIE 월드로 복제될 때 빠지고,
	 * 레벨에 배치된 액터는 런타임에 OnConstruction 이 다시 돌지 않는다. 그래서 OnConstruction
	 * 에만 맡기면 에디터 뷰포트에는 먼지가 보이는데 실제로 플레이하면 하나도 안 나온다.
	 *
	 * BuildPatches 는 기존 것을 먼저 지우므로 두 번 불려도 결과가 같다.
	 */
	BuildPatches();

	ApplyProgressToPatches();
}

void ACWWorkZone_Dust::ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds)
{
	if (Worker == nullptr || !CanWorkNow(Worker->GetHeldTool()))
	{
		return;
	}

	// 여러 명이 같이 쓸면 각자 이 함수를 부르므로 인원수만큼 빨라진다.
	const float NextProgress = GetProgress() + DeltaSeconds / SecondsToClear;

	/**
	 * 완료 임계값을 넘으면 남은 진행도를 건너뛰고 100% 로 올린다.
	 *
	 * 임계값에서 완료 처리만 하면 진행도가 그 값에 그대로 굳어서 HUD 진행도가 95% 에서
	 * 멈춘 채로 "완료" 가 된다.
	 */
	SetProgress(NextProgress >= CompleteThreshold ? 1.f : NextProgress);
}

void ACWWorkZone_Dust::OnProgressChanged(float NewProgress)
{
	Super::OnProgressChanged(NewProgress);

	// 부모가 서버와 클라이언트 양쪽에서 이걸 불러주므로 겉모습 갱신을 여기 두면 된다.
	ApplyProgressToPatches();
}

void ACWWorkZone_Dust::BuildPatches()
{
	for (const FCWDustPatch& Patch : DustPatches)
	{
		if (Patch.Decal != nullptr)
		{
			Patch.Decal->DestroyComponent();
		}
	}

	DustPatches.Reset();

	/**
	 * 데디케이티드 서버는 렌더링을 하지 않는다. 진행도만 있으면 되므로 데칼을 만들 이유가 없다.
	 *
	 * 다른 곳처럼 GetNetMode() 를 보지 않는 이유: 이 함수는 OnConstruction 에서 돈다.
	 * 레벨 로드 중이라 넷 드라이버가 아직 안 붙었을 수 있고, 그러면 NM_Standalone 이 돌아와
	 * 서버에서도 데칼을 만들어 버린다. 프로세스 자체를 묻는 쪽이 이 시점에 확실하다.
	 */
	if (IsRunningDedicatedServer())
	{
		return;
	}

	const FVector AreaExtent = Area->GetUnscaledBoxExtent();
	if (AreaExtent.X <= 0.f || AreaExtent.Y <= 0.f)
	{
		return;
	}

	const int32 Count = FMath::Max(1, PatchCount);

	/**
	 * 격자를 깔고 칸마다 한 장씩 흔들어 놓는다.
	 *
	 * 완전 난수로 뿌리면 뭉치는 자리와 텅 빈 자리가 같이 생겨서, 존이 고르게 더러워 보이지 않는다.
	 * 칸이 정사각형에 가깝도록 존의 가로세로 비율에서 열 수를 뽑는다.
	 * (200x100 존에 8장이면 4열 2행이 나온다)
	 */
	const float Aspect = AreaExtent.X / AreaExtent.Y;
	const int32 Columns = FMath::Clamp(FMath::RoundToInt(FMath::Sqrt(Count * Aspect)), 1, Count);
	const int32 Rows = FMath::DivideAndRoundUp(Count, Columns);

	const float CellSizeX = (2.f * AreaExtent.X) / Columns;
	const float CellSizeY = (2.f * AreaExtent.Y) / Rows;

	// 머티리얼의 블롭 마스크가 가장자리를 둥글게 깎아내므로, 칸보다 키워야 패치끼리 이어진다.
	const float BaseHalfSize = 0.5f * FMath::Max(CellSizeX, CellSizeY) * PatchOverlap;

	FRandomStream Stream(GetLayoutSeed());

	// UV 배율을 월드 기준으로 맞추는 데 쓴다. 아래 Patch.Tiling 참고.
	const FVector ActorScale = GetActorScale3D();

	DustPatches.Reserve(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 Column = Index % Columns;
		const int32 Row = Index / Columns;

		const float CellCenterX = -AreaExtent.X + (Column + 0.5f) * CellSizeX;
		const float CellCenterY = -AreaExtent.Y + (Row + 0.5f) * CellSizeY;

		const float JitterX = Stream.FRandRange(-0.5f, 0.5f) * CellSizeX * PatchJitter;
		const float JitterY = Stream.FRandRange(-0.5f, 0.5f) * CellSizeY * PatchJitter;

		// 가로세로에 따로 걸어서 블롭이 정원이 아니라 타원이 되게 한다. 모양이 그만큼 더 갈린다.
		const float HalfSizeX = BaseHalfSize * (1.f + Stream.FRandRange(-PatchScaleJitter, PatchScaleJitter));
		const float HalfSizeY = BaseHalfSize * (1.f + Stream.FRandRange(-PatchScaleJitter, PatchScaleJitter));

		UDecalComponent* Decal = NewObject<UDecalComponent>(this, NAME_None, RF_Transient);
		Decal->SetupAttachment(Area);
		Decal->SetRelativeLocation(FVector(CellCenterX + JitterX, CellCenterY + JitterY, 0.f));

		/**
		 * 데칼은 바닥을 향해 투영해야 해서 아래를 보도록 눕힌다. 싱크홀 데칼과 같은 규약이다.
		 * 눕히고 나면 투영 방향이 로컬 X 라, 텍스처를 돌리는 축은 Yaw 가 아니라 Roll 이다.
		 */
		Decal->SetRelativeRotation(FRotator(-90.f, 0.f, Stream.FRandRange(0.f, 360.f)));

		/**
		 * DecalSize 는 반쪽 크기(half extent)라 익스텐트를 그대로 넣으면 된다.
		 *
		 * 축이 한 번 돌아간다. 데칼을 -90 도 눕히면 로컬 X 가 아래(투영 방향)를 보고,
		 * 로컬 Y 는 존의 Y 그대로, 로컬 Z 가 존의 X 를 향한다. 그래서 Y/Z 에 넣는 값이
		 * 존 기준의 Y/X 순서가 된다. 이걸 뒤집으면 패치가 가로세로 바뀐 모양으로 찍힌다.
		 */
		Decal->DecalSize = FVector(DecalProjectionDepth, HalfSizeY, HalfSizeX);
		Decal->SetSortOrder(DecalSortOrder);
		Decal->SetDecalMaterial(DustMaterial);

		/**
		 * 화면에서 작아졌을 때 사라지는 최적화를 끈다.
		 *
		 * 먼지 존은 "여기 아직 할 일이 남았다" 를 알리는 연출이라 멀리서도 보여야 한다.
		 * 기본값(0.01)이면 쿼터뷰에서 화면 구석의 존이 조용히 사라져, 목록에는 남아 있는데
		 * 화면에는 없는 상태가 된다.
		 */
		Decal->SetFadeScreenSize(0.f);
		Decal->RegisterComponent();

		FCWDustPatch Patch;
		Patch.Decal = Decal;
		Patch.UVOffset = FVector2D(Stream.FRand(), Stream.FRand());

		/**
		 * 패치가 월드에서 실제로 차지하는 크기로 UV 배율을 뽑는다.
		 *
		 * 액터 스케일을 반드시 곱해야 한다. Area 익스텐트는 어느 존이나 같은데 배치할 때
		 * 액터 스케일로 크기를 조절해 두기 때문에(0.33 ~ 1.0), 로컬 크기만 보면 모든 존이
		 * 같은 배율을 받고 화면에서는 흙 알갱이가 3배까지 차이 나 보인다.
		 * Stage_01 만 유독 곱고 Stage_02 는 얼룩덜룩했던 원인이 이것이었다.
		 *
		 * 가로세로가 다를 때는 기하평균을 쓴다. 배율이 스칼라 하나라 어느 한 축에 맞추면
		 * 다른 축이 그만큼 늘어나는데, 기하평균이면 늘어나는 양이 양쪽으로 반씩 갈린다.
		 */
		const float WorldSizeX = 2.f * HalfSizeX * FMath::Abs(ActorScale.X);
		const float WorldSizeY = 2.f * HalfSizeY * FMath::Abs(ActorScale.Y);
		Patch.Tiling = FMath::Sqrt(WorldSizeX * WorldSizeY) / FMath::Max(DustTextureWorldSize, 1.f);

		// 밝기만 흔든다. 색조까지 흔들면 먼지가 아니라 얼룩으로 보인다.
		const float Brightness = 1.f + Stream.FRandRange(-TintJitter, TintJitter);
		Patch.Tint = FLinearColor(Brightness, Brightness, Brightness, 1.f);

		Patch.ClearSlot = Index;

		DustPatches.Add(Patch);
	}

	/**
	 * 걷히는 순서를 섞는다. (Fisher-Yates)
	 *
	 * 배열 순서 그대로 두면 격자 왼쪽 위부터 차례로 사라져서 "쓸어냈다" 가 아니라
	 * "한 줄씩 지워나갔다" 로 보인다. 배치와 같은 시드를 쓰므로 모든 기계에서 순서가 같다.
	 */
	for (int32 Index = DustPatches.Num() - 1; Index > 0; --Index)
	{
		const int32 SwapIndex = Stream.RandRange(0, Index);
		Swap(DustPatches[Index].ClearSlot, DustPatches[SwapIndex].ClearSlot);
	}

	/**
	 * 머티리얼까지 여기서 붙인다. 에디터에서도 돌아야 하기 때문이다.
	 *
	 * 예전에는 BeginPlay 에서만 만들었는데, 그러면 에디터 뷰포트의 패치는 MID 없이
	 * 머티리얼 기본값(Tiling=1, UVOffset=0)으로 그려진다. 배치하는 사람이 보는 알갱이 굵기와
	 * 실제 게임 화면이 달라져서, 에디터에서 괜찮아 보이는 값을 잡아도 플레이하면 딴판이 된다.
	 */
	CreatePatchMaterials();
}

void ACWWorkZone_Dust::CreatePatchMaterials()
{
	if (DustPatches.Num() == 0)
	{
		return;
	}

	if (DustMaterial == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] 먼지 데칼 머티리얼이 없습니다. Dust Material 프로퍼티를 확인하세요. "
				 "패치는 런타임에 만들어지므로 컴포넌트에 미리 지정해 두는 방법은 통하지 않습니다."),
			*GetName());
		return;
	}

	for (FCWDustPatch& Patch : DustPatches)
	{
		if (Patch.Decal == nullptr)
		{
			continue;
		}

		Patch.Material = Patch.Decal->CreateDynamicMaterialInstance();
		if (Patch.Material == nullptr)
		{
			continue;
		}

		// 패치마다 텍스처의 다른 자리를 다른 밝기로 본다. 같은 먼지가 두 번 나오지 않게 하는 것이 전부다.
		Patch.Material->SetScalarParameterValue(DustTilingParameterName, Patch.Tiling);
		Patch.Material->SetVectorParameterValue(DustUVOffsetParameterName, FLinearColor(Patch.UVOffset.X, Patch.UVOffset.Y, 0.f, 0.f));
		Patch.Material->SetVectorParameterValue(DustTintParameterName, Patch.Tint);
	}

	// 파라미터 이름이 안 맞으면 값만 조용히 버려진다. 한 장만 봐도 계약이 맞는지는 알 수 있다.
	UMaterialInstanceDynamic* FirstMaterial = DustPatches[0].Material;
	float ExistingValue = 0.f;
	if (FirstMaterial != nullptr && !FirstMaterial->GetScalarParameterValue(DustPatchFadeParameterName, ExistingValue))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] 머티리얼에 '%s' 스칼라 파라미터가 없어 청소 진행도가 겉모습에 반영되지 않습니다."),
			*GetName(), *DustPatchFadeParameterName.ToString());
	}
}

void ACWWorkZone_Dust::ApplyProgressToPatches()
{
	const float CurrentProgress = GetProgress();

	for (int32 Index = 0; Index < DustPatches.Num(); ++Index)
	{
		FCWDustPatch& Patch = DustPatches[Index];
		if (Patch.Decal == nullptr)
		{
			continue;
		}

		Patch.Fade = ComputePatchFade(Patch.ClearSlot, CurrentProgress);

		if (Patch.Material != nullptr)
		{
			Patch.Material->SetScalarParameterValue(DustPatchFadeParameterName, Patch.Fade);
			Patch.Material->SetScalarParameterValue(DustClearedParameterName, CurrentProgress);
		}

		// 다 걷힌 패치는 렌더에서 아예 뺀다. 청소가 진행될수록 그릴 데칼이 줄어드는 셈이다.
		const bool bVisible = Patch.Fade > KINDA_SMALL_NUMBER;
		if (Patch.Decal->IsVisible() != bVisible)
		{
			Patch.Decal->SetVisibility(bVisible);
		}
	}
}

float ACWWorkZone_Dust::ComputePatchFade(int32 ClearSlot, float InProgress) const
{
	const int32 Count = FMath::Max(1, DustPatches.Num());

	/**
	 * 완료 임계값을 100% 로 보고 늘려 잡는다.
	 *
	 * 임계값을 넘으면 ApplyWork 가 진행도를 1 로 밀어버리므로, 진행도 1 을 기준으로 계산하면
	 * 마지막 패치가 걷히기 전에 존이 완료돼 버린다. 그러면 "완료라는데 먼지가 남아 있다" 가 된다.
	 */
	const float Threshold = FMath::Max(CompleteThreshold, KINDA_SMALL_NUMBER);
	const float Normalized = FMath::Clamp(InProgress / Threshold, 0.f, 1.f);

	/**
	 * 패치마다 자기 차례가 오는 구간을 잡고, 그 안에서 걷힌다.
	 *
	 * 순번 간격이 Span, 한 장이 실제로 걷히는 데 쓰는 폭이 Span * PatchFadeWindow 다.
	 * 그래서 PatchFadeWindow 가 창구다.
	 *   1 보다 작으면 : 패치 사이에 아무것도 안 움직이는 틈이 생긴다 (단계감이 세다)
	 *   1 이면        : 틈 없이 한 장씩 이어서 걷힌다
	 *   1 보다 크면   : 여러 장이 동시에 옅어진다 (부드럽지만 단계감이 흐려진다)
	 * 기본값 0.7 은 틈이 생기는 쪽이지만, 그 틈은 8장 기준 전체 청소 시간의 4% 정도라
	 * 손이 멈춘 것처럼 느껴지지는 않는다. 그동안에도 ClearedPct 가 전체를 계속 옅게 한다.
	 *
	 * 마지막 패치가 정확히 1 에서 끝나도록 Span 을 역산한다.
	 * 패치가 하나뿐이어도 (Span * PatchFadeWindow) 가 1 이 되어 그대로 성립한다.
	 */
	const float Span = 1.f / (Count - 1 + PatchFadeWindow);
	const float Start = ClearSlot * Span;

	return 1.f - FMath::Clamp((Normalized - Start) / (Span * PatchFadeWindow), 0.f, 1.f);
}

int32 ACWWorkZone_Dust::GetLayoutSeed() const
{
	/**
	 * 레벨에 배치된 액터의 이름은 모든 기계에서 같고 세션이 바뀌어도 유지된다.
	 * FName 의 해시를 쓰면 안 된다. 이름 테이블 등록 순서에 따라 값이 달라져서
	 * 서버와 클라이언트가 서로 다른 배치를 만든다. 문자열 CRC 는 내용만 본다.
	 */
	const uint32 NameHash = FCrc::StrCrc32(*GetName());

	// 곱하는 상수는 Knuth 의 황금비 해시다. LayoutSeed 를 1 만 올려도 배치가 완전히 달라진다.
	return static_cast<int32>(NameHash ^ (static_cast<uint32>(LayoutSeed) * 2654435761u));
}
