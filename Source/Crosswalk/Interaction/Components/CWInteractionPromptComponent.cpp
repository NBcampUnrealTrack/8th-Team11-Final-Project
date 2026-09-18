// Copyright Crosswalk Team. All Rights Reserved.
// CWInteractionPromptComponent.cpp

#include "Interaction/Components/CWInteractionPromptComponent.h"

#include "Characters/CWPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Interaction/CWInteractionLog.h"

namespace
{
	/**
	 * 진단 로그 스위치. 기본값이 켜짐인 이유는 LogCWTraffic 쪽과 같다.
	 *
	 * 이 로그는 테스터와 붙어서 확인할 때 쓰는데, 받는 사람이 설정을 먼저 해야 하면
	 * 그 단계에서 테스트가 끊긴다. 켜는 쪽이 아니라 끄는 쪽이 한 줄이어야 한다.
	 *
	 * 측정이 끝나면 0 으로 두거나 이 코드를 걷어낸다.
	 */
	TAutoConsoleVariable<bool> CVarPromptLogDiagnostics(
		TEXT("cw.Prompt.LogDiagnostics"),
		true,
		TEXT("상호작용 프롬프트 진단 로그를 찍는다. 0 이면 끈다."),
		ECVF_Default);

	/** 이 줄이 어느 기계에서 나온 것인가. 여러 대의 로그를 나란히 놓고 읽을 때 기준이 된다. */
	FString DescribeMachine(const AActor& Owner)
	{
		const TCHAR* NetModeText = TEXT("Unknown");
		if (const UWorld* World = Owner.GetWorld())
		{
			switch (World->GetNetMode())
			{
			case NM_Standalone:			NetModeText = TEXT("Standalone");		break;
			case NM_ListenServer:		NetModeText = TEXT("ListenServer");		break;
			case NM_DedicatedServer:	NetModeText = TEXT("DedicatedServer");	break;
			case NM_Client:				NetModeText = TEXT("Client");			break;
			default:															break;
			}
		}

		const TCHAR* RoleText = TEXT("Unknown");
		switch (Owner.GetLocalRole())
		{
		case ROLE_Authority:		RoleText = TEXT("Authority");		break;
		case ROLE_AutonomousProxy:	RoleText = TEXT("AutonomousProxy");	break;
		case ROLE_SimulatedProxy:	RoleText = TEXT("SimulatedProxy");	break;
		default:														break;
		}

		return FString::Printf(TEXT("%s/%s"), NetModeText, RoleText);
	}

	/** 누구의 화면인가. 4인 테스트에서 로그 주인을 가리려면 닉네임이 있어야 한다. */
	FString DescribePlayer(const APawn& OwnerPawn)
	{
		if (const APlayerState* OwnerPlayerState = OwnerPawn.GetPlayerState())
		{
			return OwnerPlayerState->GetPlayerName();
		}

		return TEXT("(PlayerState 없음)");
	}

	FString DescribePrompts(const TArray<FCWInteractionPrompt>& Prompts)
	{
		if (Prompts.Num() == 0)
		{
			return TEXT("개수=0 (없음)");
		}

		const UEnum* ActionEnum = StaticEnum<ECWInteractionPromptAction>();

		FString Result = FString::Printf(TEXT("개수=%d"), Prompts.Num());
		for (const FCWInteractionPrompt& Prompt : Prompts)
		{
			const FString ActionText = ActionEnum != nullptr
				? ActionEnum->GetNameStringByValue(static_cast<int64>(Prompt.Action))
				: TEXT("?");

			Result += FString::Printf(TEXT(" [%s/%s -> %s]"),
				*Prompt.Key.ToString(),
				*ActionText,
				Prompt.TargetActor != nullptr ? *Prompt.TargetActor->GetName() : TEXT("null"));
		}

		return Result;
	}
}

UCWInteractionPromptComponent::UCWInteractionPromptComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UCWInteractionPromptComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<ACWPlayerCharacter>(GetOwner());
	PrimaryComponentTick.TickInterval = RefreshInterval;
}

void UCWInteractionPromptComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 조종 중인 본인인지를 BeginPlay 가 아니라 여기서 본다.
	//
	// 클라이언트에서는 폰이 먼저 복제되고 Controller 가 나중에 붙는 경우가 있다.
	// BeginPlay 에서 한 번 보고 틱을 꺼 버리면 컨트롤러가 나중에 와도 다시 켜지지 않아,
	// 그 플레이어는 프롬프트가 영영 안 뜬다. 호스트는 둘이 같이 있어서 멀쩡히 돌기 때문에
	// 단독 실행으로는 안 걸린다. (UCWToolHolderComponent 가 같은 이유로 틱 안에서 본다)
	//
	// 남의 캐릭터에서 도는 비용은 아래 두 줄이 전부다. 내 화면에 그릴 프롬프트가 아니다.
	if (OwnerCharacter == nullptr || !IsLocallyControlled())
	{
		return;
	}

	const bool bPromptsChanged = RefreshPrompts();

	LogDiagnostics(bPromptsChanged);
}

bool UCWInteractionPromptComponent::IsLocallyControlled() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn != nullptr && OwnerPawn->IsLocallyControlled();
}

bool UCWInteractionPromptComponent::RefreshPrompts()
{
	if (OwnerCharacter == nullptr)
	{
		return false;
	}

	// 판정은 캐릭터 쪽 한 곳에서만 한다. 여기서 다시 재면 실제 실행과 갈릴 수 있다.
	TArray<FCWInteractionPrompt> NewPrompts;

	const FCWInteractionPrompt InteractPrompt = OwnerCharacter->ResolveInteractPrompt();
	if (InteractPrompt.IsValidPrompt())
	{
		NewPrompts.Add(InteractPrompt);
	}

	const FCWInteractionPrompt ChargePrompt = OwnerCharacter->ResolveChargePrompt();
	if (ChargePrompt.IsValidPrompt())
	{
		NewPrompts.Add(ChargePrompt);
	}

	// 둘이 같은 액터를 가리키는 일은 흔하다. 통을 들고 박스 앞에 서면 E(더 꺼내기)와
	// R(반납)이 같이 유효해지는데, 그게 보급 담당의 기본 자세다. 그래서 합치지 않고
	// 둘 다 내보낸다. 한 액터에 두 줄을 쌓는 건 위젯이 맡는다.
	if (NewPrompts == ActivePrompts)
	{
		return false;
	}

	ActivePrompts = MoveTemp(NewPrompts);
	OnInteractionPromptsChanged.Broadcast(this, ActivePrompts);
	return true;
}

void UCWInteractionPromptComponent::LogDiagnostics(bool bPromptsChanged)
{
	if (!CVarPromptLogDiagnostics.GetValueOnGameThread())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr || OwnerCharacter == nullptr)
	{
		return;
	}

	const FString MachineText = DescribeMachine(*OwnerCharacter);
	const FString PlayerText = DescribePlayer(*OwnerCharacter);

	// 이 줄이 각 머신의 로그에 있는지가 "판정이 그 기계에서 도는가" 의 답이다.
	// 클라이언트 로그에 없으면 틱이 안 도는 것이고, 실제로 한 번 그렇게 만들 뻔했다.
	if (!bDiagnosticStartLogged)
	{
		bDiagnosticStartLogged = true;
		DiagnosticWindowStartSeconds = World->GetTimeSeconds();

		UE_LOG(LogCWInteraction, Log,
			TEXT("[Prompt] 판정 시작 | 머신=%s | 플레이어=%s | 폰=%s | 갱신주기=%.3fs"),
			*MachineText,
			*PlayerText,
			*OwnerCharacter->GetName(),
			RefreshInterval);
	}

	++DiagnosticRefreshCount;

	const double QuerySeconds = OwnerCharacter->GetLastInteractQuerySeconds();
	DiagnosticQueryTotalSeconds += QuerySeconds;
	DiagnosticQueryMaxSeconds = FMath::Max(DiagnosticQueryMaxSeconds, QuerySeconds);

	const int32 OverlapCount = OwnerCharacter->GetLastInteractQueryOverlapCount();
	DiagnosticOverlapTotal += OverlapCount;
	DiagnosticOverlapMax = FMath::Max(DiagnosticOverlapMax, OverlapCount);
	DiagnosticStaticOverlapTotal += OwnerCharacter->GetLastInteractQueryStaticOverlapCount();

	// 변경은 사건이라 그때그때 찍는다. 플레이어가 가만히 서 있으면 한 줄도 안 나간다.
	if (bPromptsChanged)
	{
		++DiagnosticChangeCount;

		UE_LOG(LogCWInteraction, Log,
			TEXT("[Prompt] 변경 | 머신=%s | 플레이어=%s | %s"),
			*MachineText,
			*PlayerText,
			*DescribePrompts(ActivePrompts));
	}

	const double ElapsedSeconds = World->GetTimeSeconds() - DiagnosticWindowStartSeconds;
	if (ElapsedSeconds < DiagnosticSummaryInterval || DiagnosticRefreshCount <= 0)
	{
		return;
	}

	// 요약은 상태라 계속 흐른다. 그래도 남기는 것은, 아무 일도 없었다는 사실 자체가
	// 산출물이기 때문이다. 갱신 횟수가 0 으로 끊기면 그 순간 틱이 멈춘 것이고,
	// 변경이 한 번도 없으면 판정은 도는데 대상을 못 찾고 있는 것이다. 둘은 원인이 다르다.
	UE_LOG(LogCWInteraction, Log,
		TEXT("[PromptPerf] %.0f초 | 머신=%s | 플레이어=%s | 갱신=%d회 변경=%d회 | 후보탐색 평균=%.4fms 최대=%.4fms | 오버랩 평균=%.1f개(정적 %.1f) 최대=%d개"),
		ElapsedSeconds,
		*MachineText,
		*PlayerText,
		DiagnosticRefreshCount,
		DiagnosticChangeCount,
		(DiagnosticQueryTotalSeconds / DiagnosticRefreshCount) * 1000.0,
		DiagnosticQueryMaxSeconds * 1000.0,
		static_cast<double>(DiagnosticOverlapTotal) / DiagnosticRefreshCount,
		static_cast<double>(DiagnosticStaticOverlapTotal) / DiagnosticRefreshCount,
		DiagnosticOverlapMax);

	DiagnosticWindowStartSeconds = World->GetTimeSeconds();
	DiagnosticRefreshCount = 0;
	DiagnosticChangeCount = 0;
	DiagnosticQueryTotalSeconds = 0.0;
	DiagnosticQueryMaxSeconds = 0.0;
	DiagnosticOverlapTotal = 0;
	DiagnosticOverlapMax = 0;
	DiagnosticStaticOverlapTotal = 0;
}
