#include "Painting/CWTemperatureComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

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
}

void UCWTemperatureComponent::BeginPlay()
{
	Super::BeginPlay();

	CurrentTemperature = NormalTemperature;
	HeatStage = ResolveHeatStage();

	if (const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		MovementComponent = OwnerCharacter->GetCharacterMovement();
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

	CurrentTemperature = FMath::Max(NormalTemperature, CurrentTemperature - CoolAmount * DeltaTime);
}

void UCWTemperatureComponent::AddWorkHeat(float EffectiveWorkSeconds)
{
	const AActor* Owner = GetOwner();
	if (Owner == nullptr || !Owner->HasAuthority() || EffectiveWorkSeconds <= 0.f)
	{
		return;
	}

	// 위에서 자르지 않는다. ReferenceMaxTemperature 는 상한이 아니라 게이지의 끝이다.
	// 값 자체는 넘어갈 수 있고, 넘어가더라도 계수는 GetHeatRatio 에서 1 로 포화된다.
	CurrentTemperature += WorkHeatRate * EffectiveWorkSeconds;
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

float UCWTemperatureComponent::GetMoveSpeedFactor() const
{
	return FMath::Lerp(1.f, MinMoveSpeedFactor, GetHeatRatio());
}

float UCWTemperatureComponent::GetWorkSpeedFactor() const
{
	return FMath::Lerp(1.f, MinWorkSpeedFactor, GetHeatRatio());
}

void UCWTemperatureComponent::ApplyMoveSpeed()
{
	if (MovementComponent == nullptr || BaseWalkSpeed <= 0.f)
	{
		return;
	}

	MovementComponent->MaxWalkSpeed = BaseWalkSpeed * GetMoveSpeedFactor();
}

void UCWTemperatureComponent::BroadcastIfChanged()
{
	if (!FMath::IsNearlyEqual(CurrentTemperature, LastBroadcastTemperature))
	{
		LastBroadcastTemperature = CurrentTemperature;
		OnTemperatureChanged.Broadcast(this, CurrentTemperature, GetHeatRatio());

		// 구간 전이는 색과 달리 "넘어선 순간" 한 번만 알린다.
		const ECWHeatStage NewStage = ResolveHeatStage();
		if (NewStage != HeatStage)
		{
			HeatStage = NewStage;
			OnHeatStageChanged.Broadcast(this, HeatStage);
		}
	}

	if (bResting != bLastBroadcastResting)
	{
		bLastBroadcastResting = bResting;
		OnRestingChanged.Broadcast(this, bResting);
	}
}

ECWHeatStage UCWTemperatureComponent::ResolveHeatStage() const
{
	if (CurrentTemperature >= OverheatTemperature)
	{
		return ECWHeatStage::Overheated;
	}

	if (CurrentTemperature >= CautionTemperature)
	{
		return ECWHeatStage::Caution;
	}

	return ECWHeatStage::Normal;
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

	const FColor LineColor = HeatStage == ECWHeatStage::Overheated ? FColor::Red
		: (HeatStage == ECWHeatStage::Caution ? FColor::Orange : FColor::Yellow);

	// 이동/작업 계수를 같이 찍어야 "느려진 게 체온 때문인지" 가 바로 보인다.
	// 키를 액터별로 고정해야 캐릭터마다 한 줄씩 갱신된다. (매 프레임 새 줄이 쌓이지 않음)
	GEngine->AddOnScreenDebugMessage(
		static_cast<uint64>(Owner->GetUniqueID()),
		2.f,
		LineColor,
		FString::Printf(TEXT("%s%s : 체온 %.1f도 (%.0f%%) | 이동 x%.2f | 작업 x%.2f%s"),
			bIsMine ? TEXT("[나] ") : TEXT(""),
			*PlayerName,
			CurrentTemperature,
			GetHeatRatio() * 100.f,
			GetMoveSpeedFactor(),
			GetWorkSpeedFactor(),
			bResting ? TEXT(" | 휴식중") : TEXT("")));
}

float UCWTemperatureComponent::GetTemperatureSpan() const
{
	return FMath::Max(ReferenceMaxTemperature - NormalTemperature, KINDA_SMALL_NUMBER);
}
