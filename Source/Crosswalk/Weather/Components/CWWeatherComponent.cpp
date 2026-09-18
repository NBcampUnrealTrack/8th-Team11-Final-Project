// Copyright Crosswalk Team. All Rights Reserved.
// CWWeatherComponent.cpp

#include "Weather/Components/CWWeatherComponent.h"

#include "Data/Structs/CWWeatherStateRow.h"
#include "Engine/DataTable.h"
#include "Net/UnrealNetwork.h"

UCWWeatherComponent::UCWWeatherComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCWWeatherComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	// 한 틱 미루는 이유: 클라이언트 쪽 UCWWeatherControlComponent 같은 구독자가 BeginPlay에서
	// OnWeatherStateChanged를 구독하는데, 여기서 바로 브로드캐스트하면 컴포넌트 초기화 순서에
	// 따라 구독이 아직 안 걸려 있을 수 있다. 한 틱 미루면 그 사이 구독이 끝난다.
	GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UCWWeatherComponent::AdvanceWeatherState));
}

void UCWWeatherComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCWWeatherComponent, CurrentWeatherState);
}

void UCWWeatherComponent::AdvanceWeatherState()
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	constexpr uint8 StateCount = static_cast<uint8>(ECWWeatherState::Decrease) + 1;
	const uint8 NextStateIndex = (static_cast<uint8>(CurrentWeatherState) + 1) % StateCount;
	CurrentWeatherState = static_cast<ECWWeatherState>(NextStateIndex);

	const FCWWeatherStateRow* Row = FindStateRow(CurrentWeatherState);

	// 행을 못 찾으면(DataTable 미설정 등) 기본값으로 계속 순환은 하되, 랜덤 편차 없이 진행한다 -
	// 완전히 멈추기보다는 티가 나게 밋밋한 채로라도 도는 편이 디버깅하기 쉽다.
	const float BaseDuration = Row ? Row->BaseDuration : 20.f;
	const float DurationRandomScale = Row ? Row->DurationRandomScale : 0.f;
	const float BaseWindSpeed = Row ? Row->BaseWindSpeed : 0.f;
	const float WindRandomScale = Row ? Row->WindRandomScale : 0.f;
	const float BlendTime = Row ? Row->BlendTime : 3.f;
	const float BaseIntensity = Row ? Row->WeatherIntensity : 0.5f;
	const float IntensityRandomScale = Row ? Row->IntensityRandomScale : 0.f;

	const float RolledDuration = FMath::Max(0.1f, BaseDuration * FMath::FRandRange(1.f - DurationRandomScale, 1.f + DurationRandomScale));
	const float RolledWindSpeed = FMath::Max(0.f, BaseWindSpeed * FMath::FRandRange(1.f - WindRandomScale, 1.f + WindRandomScale));
	const float RolledIntensity = FMath::Clamp(BaseIntensity * FMath::FRandRange(1.f - IntensityRandomScale, 1.f + IntensityRandomScale), 0.f, 1.f);

	// 서버 자신은 OnRep이 자동으로 안 불리므로 직접 호출한다 (표준 UE 복제 패턴)
	BroadcastWeatherState(RolledWindSpeed, RolledIntensity, BlendTime);

	GetWorld()->GetTimerManager().SetTimer(WeatherAdvanceTimerHandle, this, &UCWWeatherComponent::AdvanceWeatherState, RolledDuration, false);
}

const FCWWeatherStateRow* UCWWeatherComponent::FindStateRow(ECWWeatherState State) const
{
	if (WeatherDataTable == nullptr)
	{
		return nullptr;
	}

	const UEnum* WeatherEnum = StaticEnum<ECWWeatherState>();
	const FName RowName(*WeatherEnum->GetNameStringByValue(static_cast<int64>(State)));

	return WeatherDataTable->FindRow<FCWWeatherStateRow>(RowName, TEXT("UCWWeatherComponent::FindStateRow"));
}

void UCWWeatherComponent::OnRep_CurrentWeatherState()
{
	// 클라이언트는 실제로 서버가 굴린 랜덤값을 모른다 - DataTable 기본값(편차 없이)으로
	// "얼추 비슷한" 값을 스스로 재현한다 (클래스 주석 참고, 의도된 동작).
	const FCWWeatherStateRow* Row = FindStateRow(CurrentWeatherState);

	const float ApproxWindSpeed = Row ? Row->BaseWindSpeed : 0.f;
	const float ApproxIntensity = Row ? Row->WeatherIntensity : 0.5f;
	const float BlendTime = Row ? Row->BlendTime : 3.f;

	BroadcastWeatherState(ApproxWindSpeed, ApproxIntensity, BlendTime);
}

void UCWWeatherComponent::BroadcastWeatherState(float RolledWindSpeed, float RolledIntensity, float BlendTime)
{
	FCWWeatherEventPayload Payload;
	Payload.WeatherState = CurrentWeatherState;
	Payload.WindSpeed = RolledWindSpeed;
	Payload.WeatherIntensity = RolledIntensity;
	Payload.BlendTime = BlendTime;

	OnWeatherStateChanged.Broadcast(Payload);
}
