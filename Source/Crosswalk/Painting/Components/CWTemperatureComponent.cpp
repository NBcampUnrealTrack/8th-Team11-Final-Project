#include "Painting/Components/CWTemperatureComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Painting/Components/CWSupplyCarrierComponent.h"

UCWTemperatureComponent::UCWTemperatureComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	SetIsReplicatedByDefault(true);
}

void UCWTemperatureComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCWTemperatureComponent, CurrentTemperature);
	DOREPLIFETIME(UCWTemperatureComponent, bResting);
	DOREPLIFETIME(UCWTemperatureComponent, bExternallySlowed);
	DOREPLIFETIME(UCWTemperatureComponent, ExternalSlowFactor);
}

void UCWTemperatureComponent::BeginPlay()
{
	Super::BeginPlay();

	CurrentTemperature = NormalTemperature;

	if (const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		MovementComponent = OwnerCharacter->GetCharacterMovement();

		// 운반 체온에 쓴다. 없는 캐릭터도 있을 수 있으므로 여기서 없다고 문제 삼지 않는다.
		SupplyCarrierComponent = OwnerCharacter->FindComponentByClass<UCWSupplyCarrierComponent>();
	}

	if (MovementComponent != nullptr)
	{
		// 캐릭터 BP 가 다른 값을 넣어 뒀을 수 있으므로 생성자 기본값이 아니라 지금 값을 읽는다.
		BaseWalkSpeed = MovementComponent->MaxWalkSpeed;
	}
}

void UCWTemperatureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}

	// 체온을 실제로 굴리는 것은 서버뿐이다. 클라이언트는 리플리케이트된 값을 받아 쓴다.
	if (Owner->HasAuthority())
	{
		SimulateTemperature(DeltaTime);
	}

	// 이동속도와 연출은 모든 머신이 각자 계산한다. 이유는 헤더의 네트워크 주석 참고.
	ApplyMoveSpeed();
	BroadcastIfChanged();
	ShowDebugTemperature();
}

void UCWTemperatureComponent::SimulateTemperature(float DeltaTime)
{
	// 하락은 언제나 일정하다. 작업 중에도 계속 빠지고, 차에 치여 쓰러져 있는 동안에도 빠진다.
	//
	// 래그돌 중에 따로 멈추지 않는 이유: 다운 중에는 작업을 할 수 없어 상승원이 없고,
	// 차에 치이는 것 자체가 이미 무거운 페널티라 체온까지 얼리면 삼중 처벌이 된다.
	float CoolAmount = CoolRate;

	if (bResting)
	{
		CoolAmount += RestCoolRateBonus;
	}

	// 평상 체온 아래로는 휴식 중에만 내려간다. 그 아래가 버프 구간이다.
	//
	// 일어난 뒤에는 지금 값이 바닥이 되어 그 자리에서 유지된다. 여기서 NormalTemperature 를
	// 그냥 바닥으로 쓰면, 일어나는 순간 체온이 36.5 로 튀어올라 버프가 즉시 사라진다.
	// 버프는 타이머가 아니라 작업·운반 상승으로 소모된다.
	const float CoolFloor = bResting ? MinTemperature : FMath::Min(NormalTemperature, CurrentTemperature);

	CurrentTemperature = FMath::Max(CoolFloor, CurrentTemperature - CoolAmount * DeltaTime);

	AddCarryHeat(DeltaTime);
}

void UCWTemperatureComponent::AddCarryHeat(float DeltaTime)
{
	if (SupplyCarrierComponent == nullptr || MovementComponent == nullptr || BaseWalkSpeed <= 0.f)
	{
		return;
	}

	const int32 CarriedCount = SupplyCarrierComponent->GetCarriedCount();
	if (CarriedCount <= 0)
	{
		return;
	}

	// 수직 성분을 빼는 이유: 낙하나 점프로 생긴 속도는 보급 왕복의 노동량이 아니다.
	const float Speed2D = MovementComponent->Velocity.Size2D();
	if (Speed2D <= CarryHeatMinSpeed)
	{
		return;
	}

	// 전속력 대비 얼마나 빨리 움직였는가. 살금살금 가면 그만큼 덜 오른다.
	//
	// 상한을 MaxMoveSpeedFactor 로 여는 이유: 휴식 버프로 빨라진 만큼은 실제로 더 많이
	// 왕복한 것이라 체온도 그만큼 올라야 한다. 작업 버프가 AddWorkHeat 을 키우는 것과 같다.
	// 그래도 상한 자체는 남긴다 — 차에 치여 날아가는 동안의 속도가 그대로 들어오면
	// 사고 한 번에 체온이 치솟는다.
	//
	// 이 비율이 균형점을 만든다. 체온이 오르면 ApplyMoveSpeed 가 실제 속도를 깎으므로
	// 다음 틱의 Speed2D 가 작아지고, 상승량도 같이 준다. 헤더의 균형 비율 식이 이것이다.
	//
	// 외부 슬로우(아스팔트)로 느려진 동안에도 같은 식으로 덜 오른다. 밟혀서 기어가는 중에
	// 체온까지 평소대로 오르면 한 사고에 벌이 두 번 붙는다.
	const float SpeedRatio = FMath::Min(Speed2D / BaseWalkSpeed, MaxMoveSpeedFactor);

	AddHeat(CarryHeatRatePerCan * CarriedCount * SpeedRatio * DeltaTime);
}

void UCWTemperatureComponent::AddWorkHeat(float EffectiveWorkSeconds)
{
	const AActor* Owner = GetOwner();
	if (Owner == nullptr || !Owner->HasAuthority() || EffectiveWorkSeconds <= 0.f)
	{
		return;
	}

	AddHeat(WorkHeatRate * EffectiveWorkSeconds);
}

void UCWTemperatureComponent::AddHeat(float Amount)
{
	if (Amount <= 0.f)
	{
		return;
	}

	// 상한은 여기 한 곳에서만 건다. 헤더의 MaxTemperature 주석 참고.
	CurrentTemperature = FMath::Min(CurrentTemperature + Amount, MaxTemperature);
}

void UCWTemperatureComponent::SetResting(bool bNewResting)
{
	const AActor* Owner = GetOwner();
	if (Owner == nullptr || !Owner->HasAuthority() || bResting == bNewResting)
	{
		return;
	}

	bResting = bNewResting;
}

float UCWTemperatureComponent::GetHeatRatio() const
{
	return FMath::Clamp((CurrentTemperature - NormalTemperature) / GetTemperatureSpan(), 0.f, 1.f);
}

float UCWTemperatureComponent::GetChillRatio() const
{
	return FMath::Clamp((NormalTemperature - CurrentTemperature) / GetChillSpan(), 0.f, 1.f);
}

float UCWTemperatureComponent::GetMoveSpeedFactor() const
{
	// 평상 체온을 기준으로 위아래가 갈린다. 정확히 평상 체온이면 양쪽 다 1.0 이라 이어진다.
	if (CurrentTemperature < NormalTemperature)
	{
		return FMath::Lerp(1.f, MaxMoveSpeedFactor, GetChillRatio());
	}

	return FMath::Lerp(1.f, MinMoveSpeedFactor, GetHeatRatio());
}

float UCWTemperatureComponent::GetWorkSpeedFactor() const
{
	if (CurrentTemperature < NormalTemperature)
	{
		return FMath::Lerp(1.f, MaxWorkSpeedFactor, GetChillRatio());
	}

	return FMath::Lerp(1.f, MinWorkSpeedFactor, GetHeatRatio());
}

void UCWTemperatureComponent::ApplyMoveSpeed()
{
	if (MovementComponent == nullptr || BaseWalkSpeed <= 0.f)
	{
		return;
	}

	// 슬로우가 걸려 있을 때만 두 계수 중 느린 쪽을 쓴다.
	//
	// Min 을 조건 없이 걸면 안 되는 이유: ExternalSlowFactor 는 슬로우가 없을 때 1.0 이라
	// Min(1.2, 1.0) = 1.0 이 되어 휴식 버프가 통째로 먹힌다. 값은 다 들어가 있는데
	// 이속만 안 빨라지는 그림이 나오고, 원인이 이 한 줄에 있어서 찾기 어렵다.
	//
	// 슬로우 중에 Min 을 유지하는 이유는 전과 같다. 이미 과열로 최저 이속인 플레이어에게
	// 슬로우가 중첩되지 않는다. ("최저 이속" 정의상 그 아래는 없다)
	const float HeatFactor = GetMoveSpeedFactor();

	MovementComponent->MaxWalkSpeed = BaseWalkSpeed *
		(bExternallySlowed ? FMath::Min(HeatFactor, ExternalSlowFactor) : HeatFactor);
}

void UCWTemperatureComponent::ApplyExternalSlow(float SlowFactor, float Duration)
{
	const AActor* Owner = GetOwner();
	if (Owner == nullptr || !Owner->HasAuthority())
	{
		return;
	}

	// 이미 걸려 있는 동안의 재요청은 무시한다. 밟고 서 있는 내내 갱신되면
	// 해제 시점을 예측할 수 없어서, 한 번 걸린 구간은 끝까지 가게 둔다.
	if (bExternallySlowed)
	{
		return;
	}

	if (Duration <= 0.f)
	{
		return;
	}

	bExternallySlowed = true;
	ExternalSlowFactor = FMath::Clamp(SlowFactor, 0.f, 1.f);

	GetWorld()->GetTimerManager().SetTimer(
		ExternalSlowTimerHandle,
		this,
		&UCWTemperatureComponent::ClearExternalSlow,
		Duration,
		false);
}

void UCWTemperatureComponent::ClearExternalSlow()
{
	bExternallySlowed = false;
	ExternalSlowFactor = 1.f;
}

void UCWTemperatureComponent::BroadcastIfChanged()
{
	if (!FMath::IsNearlyEqual(CurrentTemperature, LastBroadcastTemperature))
	{
		LastBroadcastTemperature = CurrentTemperature;

		OnTemperatureChanged.Broadcast(this, CurrentTemperature, GetHeatRatio());
	}
}

void UCWTemperatureComponent::ShowDebugTemperature() const
{
	if (!bShowDebugTemperature || GEngine == nullptr)
	{
		return;
	}

	const AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}

	// 4인 테스트에서 화면마다 4줄이 뜨므로 누구 것인지 알 수 있어야 한다.
	//
	// 액터 이름(GetName)은 쓰지 않는다. 서버 월드와 클라이언트 월드는 스폰 순서가 달라서
	// 같은 플레이어가 머신마다 다른 이름을 갖는다. PlayerState 이름은 서버가 정해 복제되므로
	// 어느 화면에서 보든 같은 사람을 가리킨다.
	const APawn* OwnerPawn = Cast<APawn>(Owner);
	const bool bIsMine = OwnerPawn != nullptr && OwnerPawn->IsLocallyControlled();

	FString PlayerName;
	if (OwnerPawn != nullptr)
	{
		if (const APlayerState* PlayerState = OwnerPawn->GetPlayerState())
		{
			PlayerName = PlayerState->GetPlayerName();
		}
	}

	if (PlayerName.IsEmpty())
	{
		PlayerName = TEXT("이름없음");
	}

	// 버프 구간은 게이지가 0 에서 클램프돼 화면에 안 나타난다. 색으로라도 갈라둔다.
	const bool bChilled = CurrentTemperature < NormalTemperature;

	// 체온이 오를수록 노랑에서 빨강으로 간다.
	const FColor LineColor = bChilled ? FColor::Cyan
		: FLinearColor::LerpUsingHSV(FLinearColor::Yellow, FLinearColor::Red, GetHeatRatio()).ToFColor(true);

	// 화면 가운데 위쪽에 있는 것처럼 보이도록 앞에 붙이는 공백 개수.
	//
	// AddOnScreenDebugMessage 는 좌표를 받지 않아 엔진이 좌상단에 고정으로 그린다.
	// 그리는 방식을 바꾸지 않고 위치만 옮기려면 이 방법뿐이다.
	// 해상도가 다르면 가운데에서 어긋나는데, 디버그 표시라 그대로 둔다. 숫자만 고치면 된다.
	static constexpr int32 DebugIndentSpaces = 120;

	// 이동/작업 계수를 같이 찍어야 "느려진 게 체온 때문인지" 가 바로 보인다.
	// 키를 액터별로 고정해야 캐릭터마다 한 줄씩 갱신된다. (매 프레임 새 줄이 쌓이지 않음)
	//
	// 상한 표시를 같이 띄우는 이유: 상한이 생기면서 "균형점이 사라졌다" 는 사실이 화면에서
	// 안 보이게 됐다. 예전에는 70도를 넘어가는 것으로 알아챌 수 있었는데, 이제는 조용히
	// 상한에 붙어 있어서 값이 깨진 채로 튜닝을 계속하게 된다.
	GEngine->AddOnScreenDebugMessage(
		static_cast<uint64>(Owner->GetUniqueID()),
		2.f,
		LineColor,
		FString::Printf(TEXT("%s%s%s : 체온 %.1f도 (%s) | 이동 x%.2f | 작업 x%.2f%s%s"),
			*FString::ChrN(DebugIndentSpaces, TEXT(' ')),
			bIsMine ? TEXT("[나] ") : TEXT(""),
			*PlayerName,
			CurrentTemperature,
			bChilled ? *FString::Printf(TEXT("버프 %.0f%%"), GetChillRatio() * 100.f)
				: *FString::Printf(TEXT("%.0f%%"), GetHeatRatio() * 100.f),
			GetMoveSpeedFactor(),
			GetWorkSpeedFactor(),
			bResting ? TEXT(" | 휴식중") : TEXT(""),
			IsAtMaxTemperature() ? TEXT(" | [상한] 균형점 없음") : TEXT("")));
}

float UCWTemperatureComponent::GetTemperatureSpan() const
{
	return FMath::Max(ReferenceMaxTemperature - NormalTemperature, KINDA_SMALL_NUMBER);
}

float UCWTemperatureComponent::GetChillSpan() const
{
	return FMath::Max(NormalTemperature - MinTemperature, KINDA_SMALL_NUMBER);
}
