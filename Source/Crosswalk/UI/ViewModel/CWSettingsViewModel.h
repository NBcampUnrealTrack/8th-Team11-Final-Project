// CWSettingsViewModel.h

#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "CWSettingsViewModel.generated.h"

class UGameUserSettings;
class UWorld;

// 비디오 모드 확인 시간이 만료됐음을 UI에 알림
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCWOnVideoModeConfirmationExpiredSignature);

UCLASS(BlueprintType)
class CROSSWALK_API UCWSettingsViewModel : public UMVVMViewModelBase
{
	GENERATED_BODY()

#pragma region ViewModelLifecycle

public:
	// 설정 화면 수명을 시작하고 10초 확인 타이머(5-3)용 World를 확보
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Settings")
	void Initialize(UObject* WorldContextObject);

	// 설정 화면 수명을 종료하고 상태를 정리 (여러 번 호출해도 안전)
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Settings")
	void Uninitialize();

#pragma endregion

#pragma region SettingsData

public:
	// 현재 GameUserSettings 값을 읽어 표시 상태(목록·선택 인덱스)를 갱신
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Settings")
	void RefreshSettingsData();

	// 콤보박스에 표시할 해상도 문자열 목록을 반환 (보관값)
	UFUNCTION(BlueprintPure, Category = "CW|UI|Settings")
	const TArray<FString>& GetResolutionOptions() const
	{
		return ResolutionOptions;
	}

	// 현재 선택된 해상도 인덱스를 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Settings")
	int32 GetSelectedResolutionIndex() const
	{
		return SelectedResolutionIndex;
	}

	// 현재 선택된 창 모드 인덱스를 반환 (0 Fullscreen / 1 WindowedFullscreen / 2 Windowed)
	UFUNCTION(BlueprintPure, Category = "CW|UI|Settings")
	int32 GetSelectedWindowModeIndex() const
	{
		return SelectedWindowModeIndex;
	}

	// 현재 선택된 그래픽 품질 인덱스를 반환 (0~3 = 엔진 ScalabilityLevel)
	UFUNCTION(BlueprintPure, Category = "CW|UI|Settings")
	int32 GetSelectedQualityIndex() const
	{
		return SelectedQualityIndex;
	}

	// VM이 단독 소유하는 지원 해상도 배열을 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Settings")
	const TArray<FIntPoint>& GetSupportedResolutions() const
	{
		return SupportedResolutions;
	}

	// 해상도 변경 요청 — 유효한 새 값이면 임시 적용하고 확인 대기. 요청을 처리했으면 true
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Settings")
	bool RequestSetResolutionByIndex(int32 Index);

	// 창 모드 변경 요청 — 유효한 새 값이면 임시 적용하고 확인 대기. 요청을 처리했으면 true
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Settings")
	bool RequestSetWindowModeByIndex(int32 Index);

	// 그래픽 품질 변경 요청 — 요청을 처리했으면 true
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Settings")
	bool RequestSetQualityByIndex(int32 Index);

	// 임시 적용된 비디오 모드의 10초 확인 타이머를 시작
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Settings")
	void StartVideoModeConfirmation();

	// 임시 적용된 비디오 모드를 확정하고 저장
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Settings")
	void ConfirmPendingVideoMode();

	// 임시 적용된 비디오 모드를 취소하고 마지막 확정값으로 복구
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Settings")
	void CancelPendingVideoMode();

	// 비디오 모드 확인 대기 여부를 반환
	UFUNCTION(BlueprintPure, Category = "CW|UI|Settings")
	bool IsVideoModeConfirmationPending() const
	{
		return bIsVideoModeConfirmationPending;
	}

#pragma endregion

#pragma region StateSetters

private:
	// 해상도 문자열 목록을 변경하고 FieldNotify를 발생
	void SetResolutionOptions(const TArray<FString>& InOptions);

	// 선택 해상도 인덱스를 변경하고 FieldNotify를 발생
	void SetSelectedResolutionIndex(int32 InIndex);

	// 선택 창 모드 인덱스를 변경하고 FieldNotify를 발생
	void SetSelectedWindowModeIndex(int32 InIndex);

	// 선택 품질 인덱스를 변경하고 FieldNotify를 발생
	void SetSelectedQualityIndex(int32 InIndex);

#pragma endregion

#pragma region SettingsHelpers

private:
	// 현재 GameUserSettings 인스턴스를 반환
	UGameUserSettings* GetGameUserSettings() const;

	// 품질 변경만 적용하고 저장 (해상도 적용과 분리)
	void ApplyQualityAndSave(UGameUserSettings* GameUserSettings) const;

	// 임시 적용 전 해상도와 창 모드를 복구용으로 저장
	void CaptureVideoModeSnapshot(UGameUserSettings* GameUserSettings);

	// 사용을 마친 비디오 모드 복구값을 초기화
	void ClearVideoModeSnapshot();

	// 확인·취소·만료·화면 종료의 공통 비디오 모드 처리
	void FinishPendingVideoMode(bool bConfirm, bool bNotifyExpiration);

	// 10초 확인 시간이 끝나면 비디오 모드를 복구
	void HandleVideoModeConfirmationExpired();

	// 실행 중인 비디오 모드 확인 타이머를 정리
	void ClearVideoModeConfirmationTimer();

#pragma endregion

#pragma region ViewModelState

private:
	// 콤보박스에 표시할 해상도 문자열 목록
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Settings",
		meta = (AllowPrivateAccess = "true")
	)
	TArray<FString> ResolutionOptions;

	// 현재 선택된 해상도 인덱스
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Settings",
		meta = (AllowPrivateAccess = "true")
	)
	int32 SelectedResolutionIndex = 0;

	// 현재 선택된 창 모드 인덱스
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Settings",
		meta = (AllowPrivateAccess = "true")
	)
	int32 SelectedWindowModeIndex = 0;

	// 현재 선택된 그래픽 품질 인덱스
	UPROPERTY(
		BlueprintReadOnly,
		FieldNotify,
		Category = "CW|UI|Settings",
		meta = (AllowPrivateAccess = "true")
	)
	int32 SelectedQualityIndex = 0;

	// VM이 단독 소유하는 지원 해상도 배열 (RequestSet 인덱스 검증 기준)
	UPROPERTY()
	TArray<FIntPoint> SupportedResolutions;

	// 10초 확인 타이머(5-3)용 현재 플레이 월드
	UPROPERTY()
	TWeakObjectPtr<UWorld> BoundWorld;

	// 비디오 모드 확인 제한시간 타이머
	FTimerHandle VideoModeConfirmationTimerHandle;

	// 임시 적용 전 해상도
	FIntPoint ResolutionBeforePending = FIntPoint::ZeroValue;

	// 임시 적용 전 창 모드 인덱스
	int32 WindowModeBeforePending = 0;

	// 복구 가능한 비디오 모드 스냅샷 보유 여부
	bool bHasVideoModeSnapshot = false;

	// 해상도·창 모드가 임시 적용되어 사용자 확인을 기다리는 상태
	bool bIsVideoModeConfirmationPending = false;

	// Initialize 완료 여부 (미초기화 상태의 Request는 거부)
	bool bIsInitialized = false;

#pragma endregion

#pragma region ViewModelEvents

public:
	// 확인 시간이 만료되어 비디오 모드가 자동 복구됐음을 UI에 알림
	UPROPERTY(BlueprintAssignable, Category = "CW|UI|Settings")
	FCWOnVideoModeConfirmationExpiredSignature OnVideoModeConfirmationExpired;

#pragma endregion
};
