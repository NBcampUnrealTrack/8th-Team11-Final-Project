// CWWearableCatalog.cpp

#include "Characters/Wearables/CWWearableCatalog.h"

#include "Animation/Skeleton.h"
#include "Characters/Wearables/CWWearableItem.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/DataValidation.h"

// LOCTEXT 매크로가 쓸 네임스페이스. 번역 키가 파일 간에 충돌하지 않게 한다.
#define LOCTEXT_NAMESPACE "CWWearableCatalog"

int32 UCWWearableCatalog::GetSlotCount() const
{
	return Slots.Num();
}

const FCWWearableSlotDef* UCWWearableCatalog::GetSlotDef(int32 SlotIndex) const
{
	// IsValidIndex 는 음수와 범위 초과를 한 번에 걸러 준다.
	return Slots.IsValidIndex(SlotIndex) ? &Slots[SlotIndex] : nullptr;
}

int32 UCWWearableCatalog::IndexOfSlot(FName SlotName) const
{
	// 이름 없는 슬롯은 찾지 않는다. IsNone 검사가 없으면 이름을 안 채운 슬롯이 걸려 버린다.
	if (SlotName.IsNone())
	{
		return INDEX_NONE;
	}

	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		if (Slots[SlotIndex].SlotName == SlotName)
		{
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}

UCWWearableItem* UCWWearableCatalog::GetItem(int32 ItemIndex) const
{
	// INDEX_NONE 은 "벗은 상태" 라서 정상적인 값이다. 경고 없이 조용히 nullptr 로 넘긴다.
	return Items.IsValidIndex(ItemIndex) ? Items[ItemIndex].Get() : nullptr;
}

int32 UCWWearableCatalog::IndexOfItem(const UCWWearableItem* Item) const
{
	// nullptr 을 넣으면 INDEX_NONE 이 나온다. "기본 아이템을 비워 둔 슬롯" 이 이 경로를 탄다.
	if (Item == nullptr)
	{
		return INDEX_NONE;
	}

	for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ++ItemIndex)
	{
		if (Items[ItemIndex] == Item)
		{
			return ItemIndex;
		}
	}

	return INDEX_NONE;
}

bool UCWWearableCatalog::IsItemAllowedInSlot(int32 SlotIndex, int32 ItemIndex) const
{
	const FCWWearableSlotDef* SlotDef = GetSlotDef(SlotIndex);
	const UCWWearableItem* Item = GetItem(ItemIndex);

	// 둘 중 하나라도 없으면 거절한다. 서버 검증이라 판단이 애매하면 막는 쪽이 맞다.
	if (SlotDef == nullptr || Item == nullptr)
	{
		return false;
	}

	return Item->SlotName == SlotDef->SlotName;
}

void UCWWearableCatalog::AppendItemIndicesForSlot(int32 SlotIndex, TArray<int32>& OutItemIndices) const
{
	const FCWWearableSlotDef* SlotDef = GetSlotDef(SlotIndex);
	if (SlotDef == nullptr)
	{
		return;
	}

	for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ++ItemIndex)
	{
		const UCWWearableItem* Item = Items[ItemIndex].Get();
		if (Item != nullptr && Item->SlotName == SlotDef->SlotName)
		{
			OutItemIndices.Add(ItemIndex);
		}
	}
}

#if WITH_EDITOR
EDataValidationResult UCWWearableCatalog::IsDataValid(FDataValidationContext& Context) const
{
	// 부모가 먼저 검사한 결과를 이어받는다. 부모가 아무 규칙도 없으면 NotValidated 가 온다.
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Slots.Num() == 0)
	{
		Context.AddError(LOCTEXT("NoSlots",
			"Slots 가 비어 있습니다. 슬롯이 없으면 아무것도 입을 수 없습니다."));
		Result = EDataValidationResult::Invalid;
	}

	/**
	 * 복제되는 인덱스가 int8 이라 127 개가 상한이다.
	 *
	 * 넘으면 값이 잘려서 엉뚱한 아이템이 입혀지는데, 증상만 보면 원인을 짐작할 수 없다.
	 * 실제로 넘길 일은 없지만, 넘겼을 때 조용히 망가지지 않도록 여기서 막는다.
	 */
	if (Items.Num() > MAX_int8)
	{
		Context.AddError(FText::Format(
			LOCTEXT("TooManyItems", "아이템이 {0} 개입니다. 복제 타입이 int8 이라 {1} 개까지만 됩니다."),
			Items.Num(), static_cast<int32>(MAX_int8)));
		Result = EDataValidationResult::Invalid;
	}

	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		const FCWWearableSlotDef& SlotDef = Slots[SlotIndex];

		if (SlotDef.SlotName.IsNone())
		{
			Context.AddError(FText::Format(
				LOCTEXT("EmptySlotName", "{0} 번 슬롯의 SlotName 이 비어 있습니다."), SlotIndex));
			Result = EDataValidationResult::Invalid;

			// 이름이 없으면 아래 검사들이 의미가 없다. 다음 슬롯으로 넘어간다.
			continue;
		}

		/**
		 * 슬롯 이름 중복 검사.
		 *
		 * IndexOfSlot 은 앞에서부터 찾으므로, 반환값이 지금 인덱스와 다르면 앞에 같은
		 * 이름이 이미 있다는 뜻이다. 중복되면 뒤쪽 슬롯은 이름으로 절대 선택되지 않아
		 * "그 슬롯만 아무 반응이 없는" 상태가 된다.
		 */
		if (IndexOfSlot(SlotDef.SlotName) != SlotIndex)
		{
			Context.AddError(FText::Format(
				LOCTEXT("DuplicateSlotName", "슬롯 이름 '{0}' 이 중복됩니다."),
				FText::FromName(SlotDef.SlotName)));
			Result = EDataValidationResult::Invalid;
		}

		/**
		 * 기본 아이템이 Items 목록에도 있어야 한다.
		 *
		 * 복제되는 값은 Items 의 인덱스다. 목록에 없는 아이템은 인덱스가 INDEX_NONE 이라
		 * 시작 복장이 조용히 무시된다. "왜 시작할 때 옷을 안 입고 있지" 로 나타난다.
		 */
		if (SlotDef.DefaultItem != nullptr && IndexOfItem(SlotDef.DefaultItem) == INDEX_NONE)
		{
			Context.AddError(FText::Format(
				LOCTEXT("DefaultItemNotListed",
					"슬롯 '{0}' 의 DefaultItem 이 Items 목록에 없습니다. Items 에도 추가하세요."),
				FText::FromName(SlotDef.SlotName)));
			Result = EDataValidationResult::Invalid;
		}

		// 기본 아이템의 슬롯이 이 슬롯과 다르면 입혀지지 않는다. 애셋을 잘못 고른 경우다.
		if (SlotDef.DefaultItem != nullptr && SlotDef.DefaultItem->SlotName != SlotDef.SlotName)
		{
			Context.AddError(FText::Format(
				LOCTEXT("DefaultItemWrongSlot",
					"슬롯 '{0}' 의 DefaultItem 은 '{1}' 슬롯 아이템입니다."),
				FText::FromName(SlotDef.SlotName), FText::FromName(SlotDef.DefaultItem->SlotName)));
			Result = EDataValidationResult::Invalid;
		}
	}

	// 검증 전용 로드다. 에디터에서만 도는 코드라 동기 로드로 충분하다.
	const USkeleton* ExpectedSkeleton = BodySkeleton.LoadSynchronous();

	for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ++ItemIndex)
	{
		const UCWWearableItem* Item = Items[ItemIndex].Get();

		if (Item == nullptr)
		{
			// 빈 칸이 있으면 그 인덱스는 영원히 "벗은 상태" 가 된다. 칸을 지우는 게 맞다.
			Context.AddError(FText::Format(
				LOCTEXT("NullItem", "Items 의 {0} 번 칸이 비어 있습니다."), ItemIndex));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		// 어느 슬롯에도 속하지 않는 아이템은 고를 방법이 없다. 슬롯 이름 오타가 대부분이다.
		if (IndexOfSlot(Item->SlotName) == INDEX_NONE)
		{
			Context.AddError(FText::Format(
				LOCTEXT("UnknownItemSlot", "'{0}' 의 SlotName '{1}' 이 Slots 에 없습니다."),
				FText::FromString(Item->GetName()), FText::FromName(Item->SlotName)));
			Result = EDataValidationResult::Invalid;
		}

		/**
		 * 리더 포즈 옷의 스켈레톤 검사. 1단계에서 아이템 애셋이 하지 못한 검사다.
		 *
		 * 아이템은 자기가 어느 몸에 입혀질지 모르지만, 카탈로그는 캐릭터마다 하나이므로 안다.
		 * 스켈레톤이 다르면 SetLeaderPoseComponent 가 만드는 본 매핑이 거의 비어서, 옷이
		 * 레퍼런스 포즈에 굳은 채로 남는다. 임포트할 때 Skeleton 을 지정하지 않아 새
		 * 스켈레톤이 생긴 경우가 대부분이다.
		 */
		if (Item->AttachMode == ECWWearAttachMode::LeaderPose && ExpectedSkeleton != nullptr)
		{
			const USkeletalMesh* Mesh = Item->SkeletalMesh.LoadSynchronous();
			if (Mesh != nullptr && Mesh->GetSkeleton() != ExpectedSkeleton)
			{
				Context.AddError(FText::Format(
					LOCTEXT("SkeletonMismatch",
						"'{0}' 의 스켈레톤이 BodySkeleton 과 다릅니다. 임포트할 때 몸과 같은 스켈레톤을 지정했는지 확인하세요."),
					FText::FromString(Item->GetName())));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
