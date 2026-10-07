// CWParryPropCatalog.cpp

#include "Data/Assets/CWParryPropCatalog.h"

#include "Engine/StaticMesh.h"
#include "Misc/DataValidation.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"

// LOCTEXT 매크로가 쓸 네임스페이스. 번역 키가 파일 간에 충돌하지 않게 한다.
#define LOCTEXT_NAMESPACE "CWParryPropCatalog"

#if WITH_EDITOR
EDataValidationResult UCWParryPropCatalog::IsDataValid(FDataValidationContext& Context) const
{
	// 부모가 먼저 검사한 결과를 이어받는다. 부모가 아무 규칙도 없으면 NotValidated 가 온다.
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Props.Num() == 0)
	{
		/**
		 * 경고에 그친다. 소품 없이 맨손 스윙만 쓰는 것도 유효한 상태다.
		 * 서버는 INDEX_NONE 을 보내고 받는 쪽은 메시를 비운 채 몬타주만 재생한다.
		 */
		Context.AddWarning(LOCTEXT("NoProps",
			"Props 가 비어 있습니다. 패링은 맨손 스윙으로 나갑니다."));
		return Result;
	}

	for (int32 Index = 0; Index < Props.Num(); ++Index)
	{
		const FCWParryProp& Prop = Props[Index];

		if (Prop.Mesh == nullptr)
		{
			Context.AddError(FText::Format(
				LOCTEXT("MissingMesh", "[{0}] Mesh 가 비어 있습니다. 이 소품이 뽑히면 손이 빈 채로 스윙합니다."),
				Index));
			Result = EDataValidationResult::Invalid;
		}

		/**
		 * 사운드는 에러로 잡는다.
		 *
		 * 이 소품이 뽑힌 패링만 무음이 되는데, 차는 날아가고 소리만 없어서 바쁜 화면에서
		 * 놓치기 쉽다. 게다가 패링 사운드는 연출이 아니라 "내가 누른 키가 먹혔다" 는
		 * 조작 피드백이라, 빠지면 감각이 무너진다.
		 */
		if (Prop.SwingSound == nullptr)
		{
			Context.AddError(FText::Format(
				LOCTEXT("MissingSound", "[{0}] SwingSound 가 비어 있습니다. 이 소품이 뽑힌 패링은 무음이 됩니다."),
				Index));
			Result = EDataValidationResult::Invalid;
		}

		/**
		 * 이펙트는 경고에 그친다. 사운드와 다르게 취급하는 이유가 있다.
		 *
		 * 사운드는 "내가 누른 키가 먹혔다" 는 조작 피드백이라 빠지면 감각이 무너진다.
		 * 이펙트는 그 위에 얹는 장식이고, 없어도 소리와 스윙으로 성공을 알 수 있다.
		 *
		 * 더 중요한 건 소품 두 개 중 하나만 먼저 만들어 붙여 보는 상태가 정상이라는 점이다.
		 * 에러로 두면 그동안 카탈로그 저장이 계속 막힌다.
		 */
		if (Prop.SwingEffect == nullptr)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("MissingEffect", "[{0}] SwingEffect 가 비어 있습니다. 이 소품이 뽑힌 패링은 이펙트 없이 나갑니다."),
				Index));
		}

		if (Prop.Scale <= 0.f)
		{
			Context.AddError(FText::Format(
				LOCTEXT("BadScale", "[{0}] Scale 이 {1} 입니다. 0 이하면 메시가 보이지 않습니다."),
				Index, Prop.Scale));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif

const FCWParryProp* UCWParryPropCatalog::GetProp(int32 PropIndex) const
{
	return Props.IsValidIndex(PropIndex) ? &Props[PropIndex] : nullptr;
}

int32 UCWParryPropCatalog::PickRandomPropIndex() const
{
	if (Props.Num() == 0)
	{
		return INDEX_NONE;
	}

	return FMath::RandRange(0, Props.Num() - 1);
}

#undef LOCTEXT_NAMESPACE
