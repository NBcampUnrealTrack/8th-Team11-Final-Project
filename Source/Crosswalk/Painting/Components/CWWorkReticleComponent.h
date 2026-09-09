#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CWWorkReticleComponent.generated.h"

class ACWWorkZone_Cells;
class UCWWorkerComponent;
class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * 도색 조준 표시기. 붓이 닿을 자리와 크기를 바닥에 원으로 그린다. (조준 표시기 기획서)
 *
 * 없앨 문제가 두 가지다.
 *   1. 붓 중심이 커서와 다르다. MaxBrushDistance 로 캐릭터 주변까지 끌어당겨지는데 화면에 단서가 없다.
 *   2. 붓이 덮는 범위가 안 보인다. 체온에 따라 반경이 변하는데 역시 단서가 없다.
 * 그래서 "점" 이 아니라 "반경을 가진 원" 이어야 한다.
 *
 * 두 값 모두 다시 계산하지 않고 이미 있는 것을 그대로 읽는다.
 *   중심 = UCWWorkerComponent::GetWorkLocation()
 *   반경 = ACWWorkZone_Cells::GetEffectiveBrushRadius()
 * 같은 값을 두 번 계산하면 화면의 원과 실제 결과가 어긋나는데, 그게 이 기능이 없애려는 문제다.
 *
 * 완전히 로컬이다. 리플리케이트하지 않고 RPC 도 없다. 남의 조준은 남의 화면에만 뜬다.
 *
 * 표시만 하고 아무것도 바꾸지 않는다. UCWWorkerComponent 에 넣지 않은 이유가 이것이다.
 * 그쪽은 이미 네트워크·페인트·체온 책임이 몰려 있어서, 연출을 섞으면 문제가 생겼을 때
 * 어느 쪽이 원인인지 안 보인다.
 */
UCLASS(ClassGroup = (Painting), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWWorkReticleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWWorkReticleComponent();

	virtual void BeginPlay() override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** 지금 원을 그려야 할 존. 조건에 하나라도 안 맞으면 nullptr 이고, 그러면 원은 사라진다. */
	ACWWorkZone_Cells* FindReticleZone() const;

	/**
	 * 데칼 컴포넌트와 MID 를 만든다. 처음 원이 뜰 때 한 번만 돈다.
	 *
	 * BeginPlay 가 아니라 여기서 만드는 이유: BeginPlay 시점에는 컨트롤러가 아직 안 붙어 있어
	 * "내가 조종하는 캐릭터인가" 를 믿고 판단할 수 없다. 데디케이티드 서버와 남의 캐릭터에서는
	 * 원이 뜰 일이 없으므로 여기까지 오지 않고, 따라서 데칼도 만들어지지 않는다.
	 */
	bool EnsureDecal();

	/** 원을 그린다. 색은 ColorBlendTime 에 걸쳐 부드럽게 넘어간다. */
	void ShowReticle(const FVector& Location, float Radius, const FLinearColor& TargetColor, float DeltaTime);

	void HideReticle();

	/** bShowDebugReticle 이 켜져 있을 때 지금 상태를 화면에 띄운다. */
	void PrintDebugState(const TCHAR* State, const ACWWorkZone_Cells* Zone, float Radius) const;

protected:
	// 조준 표시기를 켤 것인가. 끄면 데칼 자체가 만들어지지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle")
	bool bEnabled = true;

	// 원을 그릴 데칼 머티리얼. 없으면 이 컴포넌트는 아무것도 하지 않는다.
	//
	// 아래 파라미터 이름들을 파라미터로 갖고 있어야 한다. 이름이 안 맞으면 값이 조용히 버려져서
	// 원이 기본값 그대로 그려진다. 그래서 처음 만들 때 한 번 검사하고 로그를 남긴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle")
	TObjectPtr<UMaterialInterface> ReticleMaterial;

	// 지금 누르면 진행되는 상태의 색. (기획서 7-2)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle|Color")
	FLinearColor WorkableColor = FLinearColor(0.1f, 0.9f, 0.2f, 1.f);

	// 눌러도 진행되지 않는 상태의 색. 덧칠과 페인트 소진을 하나로 합친 것이다.
	//
	// 플레이어 입장에서 둘은 "눌러도 안 된다" 로 결과가 같다. 원인 구분은 페인트 게이지 UI 가
	// 이미 하고 있어서, 발밑의 원까지 그걸 반복할 이유가 없다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle|Color")
	FLinearColor BlockedColor = FLinearColor(0.9f, 0.15f, 0.1f, 1.f);

	// 색이 바뀌는 데 걸리는 시간(초). 0 이면 즉시 바뀐다.
	//
	// 덧칠 경계에 붓을 걸치고 있으면 초록/빨강 판정이 프레임마다 뒤집힐 수 있다. 그대로 두면
	// 원이 깜빡여서 읽을 수가 없다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle|Color", meta = (ClampMin = "0.0"))
	float ColorBlendTime = 0.1f;

	// 원 안쪽 채움의 투명도. 낮을수록 아래 도색 상태가 잘 비친다.
	//
	// 원이 도색 데칼 위에 그려지므로(DecalSortOrder) 불투명하게 채우면 덧칠 여부가 가려진다.
	// "칠할 범위" 와 "이미 칠해진 자리" 를 동시에 봐야 해서 옅게 깐다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle|Shape", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FillAlpha = 0.35f;

	// 테두리 선의 두께. 반경 대비 비율이다.
	//
	// 채움만으로는 경계가 뭉개져서 "여기까지" 가 안 읽힌다. 선이 경계를 확정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle|Shape", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float EdgeThicknessRatio = 0.12f;

	// 테두리 선의 최소 두께(cm). 체온으로 붓이 작아져도 선이 사라지지 않게 한다.
	//
	// 비율만 쓰면 반경이 줄어들 때 선도 같이 줄어서, 정작 붓이 작아진 것을 확인해야 할 때
	// 원이 가장 안 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle|Shape", meta = (ClampMin = "0.0"))
	float MinEdgeThickness = 3.f;

	// 화면에 그리는 원의 최소 반경(cm). 실제 붓이 이보다 작아도 원은 이 크기로 그린다.
	//
	// 표시 크기를 붓 범위에서 일부러 떼어낸 값이다. 붓 반경은 존 설정과 체온에 따라
	// 6cm 까지 내려가는데, 그러면 쿼터뷰에서 점으로 보여서 조준을 도와주지 못한다.
	//
	// 대신 원 안쪽이 곧 칠해질 범위라는 보장은 사라진다. 원은 "지금 여기를 작업 중" 을
	// 알리는 표지이지 정확한 붓 자국이 아니다. 정확한 범위 표시로 되돌리려면
	// 이 값을 0, ReticleRadiusScale 을 1 로 두면 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle|Shape", meta = (ClampMin = "0.0"))
	float MinReticleRadius = 20.f;

	// 실제 붓 반경에 곱하는 표시 배율. 1 이면 실제 크기 그대로다.
	//
	// 붓이 커졌을 때도 원을 키우고 싶으면 올린다. MinReticleRadius 와 함께 적용되며,
	// 둘 중 큰 쪽이 최종 반경이 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle|Shape", meta = (ClampMin = "0.1"))
	float ReticleRadiusScale = 1.f;

	// 데칼이 아래로 투영되는 깊이(cm). 도로 요철과 연석을 타넘는 여유다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle|Shape", meta = (ClampMin = "1.0"))
	float DecalProjectionDepth = 50.f;

	// 데칼 정렬 순서. 조준 원은 도색 결과보다 위에 보여야 한다.
	//
	// 같은 자리를 여러 데칼이 덮으면 이 값이 큰 쪽이 위로 온다.
	// 참고 — 먼지 = -10, 도색 자국(ACWPaintDecalField) = 0. 그래서 넉넉히 위로 올려 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle|Shape")
	int32 DecalSortOrder = 100;

	// 머티리얼 파라미터 이름. 머티리얼 쪽 이름을 바꿨다면 여기도 같이 바꾼다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle|Material")
	FName ColorParameterName = TEXT("Color");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle|Material")
	FName FillAlphaParameterName = TEXT("FillAlpha");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle|Material")
	FName EdgeThicknessParameterName = TEXT("EdgeThickness");

	// 조준 원이 왜 안 뜨는지 화면 좌상단에 띄운다. 확인이 끝나면 끄면 된다.
	// 자기가 조종하는 캐릭터에서만 그린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle|Debug")
	bool bShowDebugReticle = false;

private:
	// 같은 캐릭터의 작업 컴포넌트. BeginPlay 에서 한 번 찾아 둔다. 없으면 이 컴포넌트는 놀고 있는다.
	UPROPERTY(Transient)
	TObjectPtr<UCWWorkerComponent> WorkerComponent;

	// 원 하나에 데칼 하나. 매 프레임 만들고 지우지 않고 위치·크기·색만 갈아 끼운다.
	UPROPERTY(Transient)
	TObjectPtr<UDecalComponent> ReticleDecal;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ReticleMaterialInstance;

	// 지금 화면에 나가고 있는 색. 목표 색으로 조금씩 따라간다. (ColorBlendTime)
	FLinearColor CurrentColor = FLinearColor::White;

	// 데칼을 만들려다 실패했는가. 실패 로그를 매 프레임 찍지 않으려고 둔다.
	bool bDecalSetupFailed = false;
};
