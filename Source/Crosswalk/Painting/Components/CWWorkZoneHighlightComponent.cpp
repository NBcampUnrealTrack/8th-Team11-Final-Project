#include "Painting/Components/CWWorkZoneHighlightComponent.h"

#include "Components/BoxComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Painting/CWPaintingSubsystem.h"
#include "Painting/Components/CWToolHolderComponent.h"
#include "Painting/Zone/CWBaseWorkZone.h"
#include "TimerManager.h"

UCWWorkZoneHighlightComponent::UCWWorkZoneHighlightComponent()
{
	// 틱이 아니라 타이머로 돈다. 매 프레임 다시 볼 이유가 없는 작업이다.
	PrimaryComponentTick.bCanEverTick = false;
}

void UCWWorkZoneHighlightComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr)
	{
		return;
	}

	// 갱신할 때마다 찾으면 그때마다 컴포넌트 배열을 훑는다. 한 번만 찾아 둔다.
	ToolHolderComponent = OwnerActor->FindComponentByClass<UCWToolHolderComponent>();

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	/**
	 * 도구가 바뀌는 순간을 듣지 않고 주기적으로 다시 본다.
	 *
	 * UCWToolHolderComponent::OnHeldToolChanged 만 구독하면 도구를 든 채로 팀원이 앞 단계를
	 * 끝내는 순간을 놓친다. ACWBaseWorkZone::bLocked 가 ReplicatedUsing 이 아니라 Replicated 라
	 * 클라이언트에서 잠금이 풀릴 때 아무 이벤트도 오지 않기 때문이다.
	 *
	 * 복제 구조를 바꾸면 클라이언트 2대 검증이 다시 붙는다. 같은 결과를 타이머로 얻을 수 있어서
	 * 그쪽을 택했다. 즉시 반응이 필요해지면 그때 델리게이트를 얹고 타이머는 안전망으로 남긴다.
	 */
	World->GetTimerManager().SetTimer(
		RefreshTimerHandle,
		this,
		&UCWWorkZoneHighlightComponent::RefreshHighlights,
		RefreshInterval,
		true);
}

void UCWWorkZoneHighlightComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 월드가 먼저 정리되는 경우가 있어서 확인하고 지운다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimerHandle);
	}

	// 켜둔 표시를 내리고 나간다. 상태는 존이 들고 있어서 이 컴포넌트가 사라져도 안 꺼진다.
	ClearAllHighlights();

	Super::EndPlay(EndPlayReason);
}

bool UCWWorkZoneHighlightComponent::IsLocallyControlled() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());

	return OwnerPawn != nullptr && OwnerPawn->IsLocallyControlled();
}

void UCWWorkZoneHighlightComponent::ClearAllHighlights()
{
	/**
	 * 내가 켠 적이 없으면 끄지도 않는다.
	 *
	 * 표시 상태는 존이 들고 있는 값이라 이 월드의 모든 컴포넌트가 같은 것을 본다.
	 * 남의 캐릭터에 붙은 컴포넌트까지 끄기에 나서면 내가 켜둔 표시를 지운다.
	 */
	if (!bHasAppliedHighlights)
	{
		return;
	}

	bHasAppliedHighlights = false;

	const UCWPaintingSubsystem* PaintingSubsystem = UCWPaintingSubsystem::Get(this);
	if (PaintingSubsystem == nullptr)
	{
		return;
	}

	for (const TWeakObjectPtr<ACWBaseWorkZone>& WeakZone : PaintingSubsystem->GetZones())
	{
		if (ACWBaseWorkZone* Zone = WeakZone.Get())
		{
			// 이미 None 이면 SetHighlightState 가 조기 반환한다. 매번 불러도 연출이 다시 돌지 않는다.
			Zone->SetHighlightState(ECWZoneHighlightState::None);
		}
	}
}

void UCWWorkZoneHighlightComponent::RefreshHighlights()
{
	const UCWPaintingSubsystem* PaintingSubsystem = UCWPaintingSubsystem::Get(this);
	if (PaintingSubsystem == nullptr)
	{
		return;
	}

	/**
	 * 표시할 수 없는 상황이면 조기 반환하지 않고 내가 켜둔 것을 내린다.
	 *
	 * 존이 상태를 들고 있기 때문이다. 그냥 돌아가면 마지막으로 켜둔 표시가 화면에 남는다.
	 * 도구를 내려놓았는데도 안 꺼지는 버그가 정확히 여기서 나온다.
	 *
	 * 끄는 것은 ClearAllHighlights 가 "내가 켠 적이 있을 때만" 하도록 막아 둔다.
	 * 이 경로에는 남의 캐릭터에 붙은 컴포넌트도 매번 들어오기 때문이다. 클라이언트 월드에는
	 * 다른 플레이어의 폰이 전부 존재하고, 그 폰들은 IsLocallyControlled 가 false 다.
	 *
	 * "내가 조종하는 캐릭터인가" 를 BeginPlay 가 아니라 여기서 보는 이유도 있다.
	 * 그 시점에는 컨트롤러가 아직 안 붙어 있을 수 있어 판단을 믿을 수 없다.
	 * 0.2초에 한 번이라 매번 확인해도 비용이 없다. 이걸 빼면 리슨 서버에서 남의 도구 때문에
	 * 내 화면이 켜진다. 서버는 모든 폰의 컴포넌트를 들고 있기 때문이다.
	 */
	if (!bEnabled || !IsLocallyControlled() || ToolHolderComponent == nullptr)
	{
		ClearAllHighlights();
		return;
	}

	// 맨손이면 아무것도 안내하지 않는다. 어떤 존도 맨손으로는 작업할 수 없다.
	// 조기 반환하지 않고 아래 루프를 그대로 타서 전부 None 으로 내린다.
	const ECWToolType HeldTool = ToolHolderComponent->GetHeldToolType();
	const bool bHasTool = (HeldTool != ECWToolType::None);

	const UWorld* World = GetWorld();

	// 다음 갱신까지만 살려 둔다. 살짝 여유를 주는 이유는 정확히 같은 값이면
	// 한 프레임 비어서 깜빡이기 때문이다.
	const float BoxLifeTime = RefreshInterval * 1.1f;

	for (const TWeakObjectPtr<ACWBaseWorkZone>& WeakZone : PaintingSubsystem->GetZones())
	{
		ACWBaseWorkZone* Zone = WeakZone.Get();
		if (Zone == nullptr)
		{
			continue;
		}

		// 판정은 존이 한다. 여기서 새로 만드는 기준은 없다.
		const ECWZoneHighlightState State = bHasTool
			? Zone->GetHighlightStateFor(HeldTool)
			: ECWZoneHighlightState::None;

		Zone->SetHighlightState(State);

		// 하나라도 켰으면 끌 책임이 생긴다. (ClearAllHighlights 주석 참고)
		if (State != ECWZoneHighlightState::None)
		{
			bHasAppliedHighlights = true;
		}

#if ENABLE_DRAW_DEBUG
		if (!bDrawDebugBoxes || State == ECWZoneHighlightState::None || World == nullptr || Zone->Area == nullptr)
		{
			continue;
		}

		const FColor BoxColor = (State == ECWZoneHighlightState::Active) ? ActiveColor : PendingColor;

		// 회전을 반영하는 오버로드를 쓴다. 횡단보도 존은 대부분 돌아가 있어서,
		// 축 정렬 박스로 그리면 실제 영역보다 크게 잡혀 어디까지가 존인지 어긋난다.
		DrawDebugBox(
			World,
			Zone->Area->GetComponentLocation(),
			Zone->Area->GetScaledBoxExtent(),
			Zone->Area->GetComponentQuat(),
			BoxColor,
			false,
			BoxLifeTime,
			0,
			DebugLineThickness);
#endif
	}
}
