// CWStageSelectViewModel.h

#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "CWStageSelectViewModel.generated.h"

class UCWStageCatalog;
class UCWStageEntryViewModel;

// 선택 확정 시 발행하는 이벤트 (StageId 전달)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCWOnStageConfirmedSignature,
	FName, StageId
);

// 스테이지 선택 팝업의 ViewModel (로컬 선택까지)
// 카탈로그를 카드 VM 목록으로 변환해 TileView 에 제공하고, 현재 선택과 확정을 관리함
// 확정은 로컬에서 OnStageConfirmed(StageId) 발행까지만 — 서버 상태 변경은 다음 단계
UCLASS(BlueprintType)
class CROSSWALK_API UCWStageSelectViewModel : public UMVVMViewModelBase
{
	GENERATED_BODY()

#pragma region Lifecycle

public:
	// 카탈로그로 카드 목록을 구성하고 기본 선택을 잡음 (팝업 Construct 에서 호출)
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Stage")
	void Initialize(UCWStageCatalog* Catalog);

#pragma endregion

#pragma region Selection

public:
	// 카드 클릭 등으로 하이라이트를 옮김 (확정 아님). 범위 밖이면 무시하고 false
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Stage")
	bool RequestSelectByIndex(int32 Index);

	// 현재 선택을 확정 → OnStageConfirmed 발행. 선택 없으면 무시하고 false
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Stage")
	bool ConfirmSelection();

	// 현재 선택된 스테이지 식별자. 선택 없으면 NAME_None
	UFUNCTION(BlueprintPure, Category = "CW|UI|Stage")
	FName GetSelectedStageId() const;

	// 선택이 있는지 (확정 버튼 활성화 등)
	UFUNCTION(BlueprintPure, FieldNotify, Category = "CW|UI|Stage")
	bool HasSelection() const
	{
		return StageEntries.IsValidIndex(SelectedIndex);
	}

	// 현재 선택된 카드 VM. 선택 없으면 nullptr (미리보기 패널이 여기 바인딩)
	UFUNCTION(BlueprintPure, FieldNotify, Category = "CW|UI|Stage")
	UCWStageEntryViewModel* GetSelectedEntry() const;

#pragma endregion

#pragma region State

private:
	// 선택 전에도 미리보기 바인딩이 빈 값으로 초기화되도록 함
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "CW|UI|Stage", meta = (AllowPrivateAccess = "true"))
	FText SelectedDisplayName;

	// TileView 아이템 (카탈로그 순서 그대로)
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Stage",
		meta = (AllowPrivateAccess = "true")
	)
	TArray<TObjectPtr<UCWStageEntryViewModel>> StageEntries;

	// 현재 하이라이트된 인덱스 (-1 = 선택 없음)
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Stage",
		meta = (AllowPrivateAccess = "true")
	)
	int32 SelectedIndex = INDEX_NONE;

#pragma endregion

#pragma region Events

public:
	// 선택 확정 시 발행
	// 로컬 단계: 위젯이 팝업 닫기
	// 서버 단계: 위젯이 Owning PlayerController의 스테이지 선택 요청 API 호출
	UPROPERTY(BlueprintAssignable, Category = "CW|UI|Stage")
	FCWOnStageConfirmedSignature OnStageConfirmed;

#pragma endregion
};
