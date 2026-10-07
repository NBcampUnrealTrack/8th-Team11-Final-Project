// CWVehicleAppearanceComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Traffic/Data/CWVehicleVariantPick.h"
#include "CWVehicleAppearanceComponent.generated.h"

class ACWBaseVehicle;
struct FCWVehicleMeshVariant;

/**
 * 차량 한 대의 외형을 맡는다. 차체·색 변형 뽑기와 복제, 메쉬 씌우기, 바퀴 회전.
 *
 * 왜 컴포넌트인가
 *   ACWBaseVehicle 은 주행·동기화·패링·피격을 이미 들고 있어서, 외형 코드까지 같은 파일에
 *   있으면 "이 함수가 판정에 영향을 주나" 를 매번 따져야 했다. 여기 있는 것은 전부 눈에만
 *   보이는 일이다. 단 하나의 예외가 콜리전 크기인데, 그건 차량의 ApplyCollisionExtent 에
 *   맡기고 여기서는 값만 넘긴다.
 *
 * 데이터는 차량에 남아 있다
 *   MeshVariants / WheelRadius / WheelBones 는 ACWBaseVehicle 의 UPROPERTY 그대로다.
 *   프로퍼티까지 이리로 옮기면 차종 BP 에 입력해 둔 변형 목록(승용차 BP 만 차체 15종)이
 *   통째로 사라져 다시 쳐야 한다. 그 복붙은 틀려도 랜덤이라 재현이 안 되는 종류의 사고다.
 *   그래서 "차종 정의는 차량 BP, 런타임 처리는 컴포넌트" 로 나눴고, 여기서는 게터로 읽는다.
 *
 * 네트워크 - VariantPick 을 복제한다. 그래서 이 컴포넌트는 복제 컴포넌트다.
 *   서버가 뽑은 차체·색을 모든 화면이 똑같이 봐야 한다. 바퀴 각도는 복제하지 않는다 -
 *   차량 위치·속도가 이미 양쪽에 있어서 각자 굴려도 되고, 어긋나 봐야 아무도 모른다.
 *
 * 틱이 없다. 바퀴 회전은 차량 Tick 이 맨 앞에서 UpdateWheelSpin 을 부른다.
 *   자기 틱으로 돌리면 액터 틱과의 순서가 엔진 설정에 달려서, "지난 프레임 속도로 돌린다"
 *   는 전제(UpdateWheelSpin 주석)가 보장되지 않는다.
 */
UCLASS(ClassGroup = (Crosswalk), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWVehicleAppearanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWVehicleAppearanceComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * 차체와 색을 뽑아 씌운다. 서버에서만 부른다. 뽑은 차체를 돌려주고, 변형이 없으면 nullptr.
	 *
	 * 클라이언트가 각자 굴리면 화면마다 다른 차가 보인다. 결과는 VariantPick 으로 복제되고
	 * 클라이언트는 OnRep 에서 같은 메쉬를 씌운다.
	 *
	 * 차체를 먼저 균등하게 뽑고 그 안에서 색을 뽑는다. 색까지 한 번에 늘어놓고 뽑으면
	 * 색이 네 가지인 차체가 한 가지인 차체보다 네 배 자주 나온다. (FCWVehicleVariantPick 주석)
	 *
	 * 색 목록이 빈 차체도 뽑힐 수 있다. 그때 Color 는 0 이 되고 ApplyMeshVariant 가
	 * 정적 메쉬 쪽으로 떨어뜨린다. 유효하지 않은 인덱스를 들고 있는 것이 아니라,
	 * 애초에 색을 고를 것이 없는 차체다.
	 *
	 * 돌려주는 포인터는 차량의 MeshVariants 배열 원소다. 부르는 쪽(차량)이 그 차체의
	 * SpeedType 으로 속도를 굴리는 데 쓴다 - 세단과 밴이 같은 BP 에 들어 있어서
	 * 어느 쪽이 나올지는 추첨 전에는 알 수 없다.
	 */
	const FCWVehicleMeshVariant* PickVariant();

	/**
	 * 바퀴 회전각을 이번 프레임 이동만큼 민다. 서버·클라이언트 양쪽에서 돈다.
	 *
	 * 차량 Tick 맨 앞에서 지난 프레임 속도로 계산한다. 이번 프레임 속도를 쓰려면 튕겨나가는
	 * 경로와 차선 주행 경로가 각자 속도를 채운 뒤에 불러야 하는데, 그러면 호출 지점이
	 * 둘로 갈라져 한쪽을 빼먹기 쉽다. 바퀴 각도가 한 프레임 늦는 것은 눈에 보이지 않는다.
	 */
	void UpdateWheelSpin(float DeltaSeconds);

private:
	/** 소유 차량. 차량이 아닌 액터에 붙어 있으면 nullptr 이고 모든 동작이 비켜간다. */
	ACWBaseVehicle* GetVehicle() const;

	/** 지금 뽑혀 있는 차체. 아직 안 뽑았거나 인덱스가 범위 밖이면 널이다. */
	const FCWVehicleMeshVariant* GetActiveVariant() const;

	/** 고른 차체와 색의 메쉬·콜리전 크기를 반영한다. 뽑은 것이 없으면 아무것도 하지 않는다. */
	void ApplyMeshVariant();

	/**
	 * 지금 씌운 변형의 바퀴 반지름(cm). 변형이 0 이면 차종 BP 의 WheelRadius 로 떨어진다.
	 *
	 * 결과를 멤버에 캐시하지 않는 이유: 캐시하면 반지름을 지정한 변형을 한 번 뽑은 차량이
	 * 재사용될 때 그 값이 남아, 지정하지 않은 변형에 엉뚱한 반지름이 붙는다.
	 * (콜리전 익스텐트가 같은 구조라 풀링을 도입하면 그쪽도 같이 봐야 한다)
	 */
	float GetActiveWheelRadius() const;

	/**
	 * 지금 씌운 변형의 바퀴 본 이름. 변형이 비어 있으면 차종 BP 의 WheelBones 로 떨어진다.
	 *
	 * 반지름과 같은 규칙이다. 한 차종 BP 안에 본 구성이 다른 차체가 섞이기 때문에
	 * (트럭 BP 의 4륜 Truck_01 과 6륜 Truck_02) 차종 값 하나로는 덮을 수 없다.
	 */
	const TArray<FName>& GetActiveWheelBones() const;

	/**
	 * 바퀴 본의 레퍼런스 포즈 회전을 받아 둔다. 메쉬를 씌운 직후에만 부른다.
	 *
	 * 이 자세 위에 회전을 얹어야 바퀴가 눕지 않는다. 이유는 ApplyWheelSpinToBones 주석 참고.
	 * 메쉬에 없는 본은 여기서 걸러내므로, 이름을 잘못 넣어도 경고가 스폰당 한 줄로 끝난다.
	 */
	void CacheWheelBoneRestRotations();

	/**
	 * 누적된 바퀴 각도를 바퀴 본에 얹는다. 순수 연출이라 서버·클라이언트가 각자 돈다.
	 *
	 * 각도 계산과 나눠 둔 이유는 화면 밖에서 이쪽만 건너뛰기 위해서다. 각도는 계속
	 * 누적해야 다시 보일 때 맞는 값이 나온다.
	 */
	void ApplyWheelSpinToBones();

	/** 서버가 고른 차체와 색이 클라이언트에 도착했을 때. */
	UFUNCTION()
	void OnRep_VariantPick();

private:
	/**
	 * 서버가 고른 차체와 색. 뽑기 전에는 두 칸 모두 NotPicked 다.
	 *
	 * 둘을 한 구조체로 묶어 한 번에 보내는 이유는 FCWVehicleVariantPick 주석 참고.
	 *
	 * 같은 액터에 PickVariant 를 다시 부르는 구조가 생기면(지금은 없다) 그 전에 이 값을
	 * 기본값으로 되돌려야 한다. 직전과 같은 조합을 다시 뽑으면 값이 안 바뀌어 전송이 생략된다.
	 */
	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing = OnRep_VariantPick, Category = "Traffic|Vehicle")
	FCWVehicleVariantPick VariantPick;

	/**
	 * 바퀴 누적 회전각(도). 시각 전용이라 복제하지 않는다.
	 *
	 * 스폰 시 0 으로 시작한다. 같은 차선의 차들이 바퀴 위상까지 똑같이 맞아 도는 것이
	 * 눈에 띄면 여기에 초기 난수를 넣으면 되는데, 지금 속도와 차간 거리에서는 두 차가
	 * 한 화면에 나란히 서는 일이 드물어 미뤄 둔다.
	 */
	float WheelSpinDegrees = 0.f;

	/**
	 * 실제로 돌릴 바퀴 본과 그 레퍼런스 자세. 두 배열은 같은 순서로 짝을 이룬다.
	 *
	 * GetActiveWheelBones 를 그대로 쓰지 않고 한 벌 더 두는 이유는 메쉬에 없는 이름을
	 * 걸러낸 결과이기 때문이다. 매 틱 거르면 그때마다 엔진 경고가 찍힌다.
	 *
	 * 메쉬가 바뀔 때마다(ApplyMeshVariant) 다시 채운다. 같은 액터에 다른 변형을 씌우는
	 * 구조가 생기면 이 두 배열도 반드시 같이 갱신돼야 한다.
	 */
	TArray<FName> CachedWheelBones;
	TArray<FQuat> WheelBoneRestRotations;

	/** 위 두 배열을 한 번이라도 채웠는가. 변형을 쓰지 않는 차종도 첫 틱에 채우기 위한 표시다. */
	bool bWheelRestCached = false;
};
