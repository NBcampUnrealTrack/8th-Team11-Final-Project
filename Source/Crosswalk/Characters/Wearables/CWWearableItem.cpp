// CWWearableItem.cpp

#include "Characters/Wearables/CWWearableItem.h"

#include "Misc/DataValidation.h"

// LOCTEXT 매크로가 쓸 네임스페이스. 번역 키가 파일 간에 충돌하지 않게 한다.
#define LOCTEXT_NAMESPACE "CWWearableItem"

namespace CWWearableSlots
{
	const FName Head = TEXT("Head");
	const FName Torso = TEXT("Torso");
	const FName Legs = TEXT("Legs");
	const FName BodyColor = TEXT("BodyColor");
}

bool UCWWearableItem::HasMesh() const
{
	if (AttachMode == ECWWearAttachMode::BodyMaterial)
	{
		return false;
	}
	// 모드에 따라 보는 칸이 다르다. IsNull 은 로드 없이 경로만 확인하므로 값이 싸다.
	return (AttachMode == ECWWearAttachMode::Socket)
		? !StaticMesh.IsNull()
		: !SkeletalMesh.IsNull();
}

FText UCWWearableItem::GetDisplayNameSafe() const
{
	// GetName() 은 애셋 이름 문자열("DA_Helmet_Yellow"). 이름을 안 채웠을 때의 최후 수단이다.
	return DisplayName.IsEmpty() ? FText::FromString(GetName()) : DisplayName;
}

#if WITH_EDITOR
EDataValidationResult UCWWearableItem::IsDataValid(FDataValidationContext& Context) const
{
	// 부모가 먼저 검사한 결과를 이어받는다. 부모가 아무 규칙도 없으면 NotValidated 가 온다.
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (SlotName.IsNone())
	{
		Context.AddError(LOCTEXT("MissingSlotName",
			"SlotName 이 비어 있습니다. 카탈로그에 등록된 슬롯 이름을 적으세요."));
		Result = EDataValidationResult::Invalid;
	}

	if (AttachMode == ECWWearAttachMode::BodyMaterial)
	{
		if (BodyMaterial.IsNull() || BodyMaterialIndex < 0)
		{
			Context.AddError(LOCTEXT("MissingBodyMaterial", "몸 색상에는 머티리얼과 0 이상의 슬롯 번호가 필요합니다."));
			Result = EDataValidationResult::Invalid;
		}
	}
	else if (!HasMesh())
	{
		Context.AddError(FText::Format(
			LOCTEXT("MissingMesh", "{0} 모드인데 쓸 메시가 비어 있습니다."),
			UEnum::GetDisplayValueAsText(AttachMode)));
		Result = EDataValidationResult::Invalid;
	}

	if (AttachMode == ECWWearAttachMode::Socket && SocketName.IsNone())
	{
		// 에러가 아니라 경고다. 동작은 하지만 몸의 원점(발밑)에 붙어서 의도한 모습이 아니다.
		Context.AddWarning(LOCTEXT("MissingSocketName",
			"SocketName 이 비어 있어 몸 메시의 원점에 붙습니다. 붙일 본 이름을 적으세요."));
	}

	/**
	 * 모드를 바꾼 뒤 반대쪽 칸에 옛 값이 남아 있는 경우.
	 *
	 * 동작에는 영향이 없지만, 쓰지 않는 애셋이 참조로 남아 쿠킹 결과에 따라 들어간다.
	 * EditConditionHides 로 칸이 숨겨져 있어서 눈으로는 찾을 수 없다. 그래서 알려 준다.
	 */
	if (AttachMode == ECWWearAttachMode::Socket && !SkeletalMesh.IsNull())
	{
		Context.AddWarning(LOCTEXT("StaleSkeletalMesh",
			"Socket 모드인데 SkeletalMesh 칸에 값이 남아 있습니다. 비워 두세요."));
	}

	if (AttachMode == ECWWearAttachMode::LeaderPose && !StaticMesh.IsNull())
	{
		Context.AddWarning(LOCTEXT("StaleStaticMesh",
			"LeaderPose 모드인데 StaticMesh 칸에 값이 남아 있습니다. 비워 두세요."));
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
