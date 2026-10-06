// CWStageEntryViewModel.h

#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "CWStageEntryViewModel.generated.h"

struct FCWStageInfo;
class UTexture2D;

// 스테이지 선택 목록(TileView)의 카드 1장을 표시하는 ViewModel
// TileView 는 UObject 아이템만 받으므로, FCWStageInfo(구조체) 한 건을
// 이 UObject VM 으로 감싸 카드 위젯이 MVVM 으로 바인딩함
UCLASS(BlueprintType)
class CROSSWALK_API UCWStageEntryViewModel : public UMVVMViewModelBase
{
	GENERATED_BODY()

#pragma region Setup

public:
	// 카탈로그의 스테이지 1건을 카드 표시 상태로 채움 (SelectVM 이 목록 구성 시 호출)
	void SetStageInfo(const FCWStageInfo& StageInfo);

	// 선택 확정 시 사용할 스테이지 식별자
	UFUNCTION(BlueprintPure, Category = "CW|UI|Stage")
	FName GetStageId() const
	{
		return StageId;
	}

#pragma endregion

#pragma region DisplayState

public:
	const FText& GetDisplayName() const { return DisplayName; }
	bool GetIsUnlocked() const { return bIsUnlocked; }
	void SetIsUnlocked(bool bInUnlocked);

	// 설명 유무 (위젯 설명 영역 표시 분기 — 비면 숨김)
	UFUNCTION(BlueprintPure, FieldNotify, Category = "CW|UI|Stage")
	bool HasDescription() const
	{
		return !Description.IsEmpty();
	}

	// 썸네일 지정 유무 (위젯 기본 이미지 분기)
	UFUNCTION(BlueprintPure, FieldNotify, Category = "CW|UI|Stage")
	bool HasThumbnail() const
	{
		return !Thumbnail.IsNull();
	}

private:
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "CW|UI|Stage", meta = (AllowPrivateAccess = "true"))
	bool bIsUnlocked = true;

	// 선택 확정에만 쓰는 안정 식별자 (표시용 아님 → FieldNotify 아님)
	FName StageId = NAME_None;

	// 카드 표시 이름
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Stage",
		meta = (AllowPrivateAccess = "true")
	)
	FText DisplayName;

	// 스테이지 설명 (비면 위젯이 숨김)
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Stage",
		meta = (AllowPrivateAccess = "true")
	)
	FText Description;

	// 카드 썸네일 (비면 위젯이 기본 이미지 사용)
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Stage",
		meta = (AllowPrivateAccess = "true")
	)
	TSoftObjectPtr<UTexture2D> Thumbnail;

#pragma endregion
};
