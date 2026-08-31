// CWAsphaltGun.h

#pragma once

#include "CoreMinimal.h"
#include "Painting/Tools/CWBaseTool.h"
#include "CWAsphaltGun.generated.h"

class ACWAsphaltGun;
class ACWAsphaltProjectileVisual;
class ACWAsphaltSplat;

/**
 * 탄약이나 재장전 상태가 바뀔 때. 서버와 클라이언트 양쪽에서 발행된다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCWOnAsphaltAmmoChangedSignature, ACWAsphaltGun*, Gun, int32, CurrentAmmo, bool, bReloading);

/**
 * 싱크홀을 메꾸는 아스팔트 건. (싱크홀 기획서 7절)
 *
 * 근접 상호작용이 아니라 원거리 사격이다. 마우스 커서가 가리키는 지면으로 아스팔트가
 * 날아가 착탄하고, 싱크홀에 맞으면 진행도가 오른다. 빗나가면 바닥에 자국(ACWAsphaltSplat)이
 * 남아 밟은 사람을 느리게 만든다.
 *
 * 원거리로 둔 이유: 차도 한복판의 싱크홀을 인도에서 처리하는 선택지가 생겨
 * "위험을 감수하지 않는 영리한 판단" 이 가능해진다. 대신 거리에 비례해 산포가 커져서
 * 안전한 만큼 부정확해진다.
 *
 * 페인트를 쓰지 않는다. 이 게임의 "작업" 은 횡단보도 도색이고, 싱크홀 메꾸기는 작업을
 * 방해하는 요소를 치우는 행위라 성격이 다르다. 잔량 게이지를 붙이면 페인트 보급과
 * 경쟁하는 두 번째 자원 관리 루프가 생겨 초점이 분산된다.
 * 탄창은 예비 탄약이 무제한이라 자원 관리가 아니라 정확도에 대한 피드백 장치다.
 *
 * 연사 간격과 입력 하한을 나눠 둔 이유
 *   입력 하한(0.05초)은 중복 격발로 인한 버그를 막는 기술적 값이고,
 *   연사 간격(0.4초)은 밸런스 값이다. 하한을 그대로 연사 속도로 쓰면 6발이 0.3초에
 *   나가서 "시간 손해" 라는 이 기믹의 핵심 리스크가 사라진다.
 *
 * 네트워크 (기획서 7-12)
 *   투사체 액터를 리플리케이트하지 않는다. 판정과 연출을 완전히 분리한다.
 *     1) 클라이언트가 서버에 사격을 요청한다 (UCWAsphaltGunnerComponent 경유)
 *     2) 서버가 산포 난수를 굴려 착탄점을 확정한다
 *     3) Multicast 로 (시작점, 착탄점, 비행시간)만 뿌린다
 *     4) 각 머신이 그 값으로 연출용 메쉬를 로컬 보간한다
 *     5) 착탄 판정은 서버에서만, 비행시간이 지난 뒤 처리한다
 *
 *   산포 난수는 반드시 서버에서만 굴린다. 각 클라이언트가 따로 굴리면 화면마다 다른
 *   곳에 떨어진다. 이 설계에서 유일하게 깨지기 쉬운 지점이다.
 *
 *   클라이언트 예측을 하지 않는다. 산포 때문에 착탄점을 예측할 수 없어서 미리 궤적을
 *   그리면 서버 결과와 어긋나 순간이동이 보인다. 대신 총구 이펙트와 발사음은 로컬에서
 *   즉시 재생해 지연을 체감시키지 않는다.
 */
UCLASS()
class CROSSWALK_API ACWAsphaltGun : public ACWBaseTool
{
	GENERATED_BODY()

public:
	ACWAsphaltGun();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void BeginPlay() override;

	/**
	 * 조준 지점으로 한 발 쏜다. 서버에서만 유효하다.
	 * 산포는 여기서 굴린다. 연사 간격과 잔탄을 여기서 검사한다.
	 *
	 * @param AimLocation 클라이언트가 보낸 조준 지점(지면 위 월드 좌표)
	 * @return 실제로 발사됐으면 true
	 */
	bool TryFire(const FVector& AimLocation);

	/**
	 * 수동 재장전. 서버에서만 유효하다.
	 * 한 발이라도 썼을 때만 들어간다. 가득 찬 상태에서 눌러도 아무 일도 없다.
	 *
	 * 수동을 여는 이유: 자동만 있으면 탄이 애매하게 남은 채로 다음 싱크홀에 접근했다가
	 * 중간에 끊기는 상황을 플레이어가 통제할 수 없다. 차가 지나간 직후의 안전한 틈에
	 * 미리 채워 두는 판단이 생겨야 남는 탄에 의미가 생긴다.
	 *
	 * @return 재장전을 시작했으면 true
	 */
	bool TryReload();

	/** 지금 쏠 수 있는가. 재장전 중이거나 연사 간격이 안 지났으면 false. */
	UFUNCTION(BlueprintPure, Category = "Asphalt|Gun")
	bool CanFire() const;

	UFUNCTION(BlueprintPure, Category = "Asphalt|Gun")
	int32 GetCurrentAmmo() const { return CurrentAmmo; }

	UFUNCTION(BlueprintPure, Category = "Asphalt|Gun")
	int32 GetMagazineCapacity() const { return MagazineCapacity; }

	UFUNCTION(BlueprintPure, Category = "Asphalt|Gun")
	bool IsReloading() const { return bReloading; }

	/** 입력 하한. 컨트롤 쪽에서 요청 자체를 걸러낼 때 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Asphalt|Gun")
	float GetInputMinInterval() const { return InputMinInterval; }

protected:
	/** 발사 순간. BP 에서 반동과 카메라 셰이크를 붙이는 자리. 모든 머신에서 불린다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Asphalt|Gun")
	void OnFired();

	/**
	 * 투사체 연출을 시작하라는 신호. 모든 머신에서 불린다.
	 * BP 에서 시작점→착탄점을 포물선으로 잇는 연출용 메쉬를 돌린다.
	 * 판정과 무관한 순수 외형이라 어긋나도 게임에 영향이 없다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Asphalt|Gun")
	void OnProjectileLaunched(const FVector& StartLocation, const FVector& ImpactLocation, float FlightTime);

	UFUNCTION(BlueprintImplementableEvent, Category = "Asphalt|Gun")
	void OnReloadStarted(float Duration);

	UFUNCTION(BlueprintImplementableEvent, Category = "Asphalt|Gun")
	void OnReloadFinished();

private:
	UFUNCTION()
	void OnRep_AmmoState();

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayProjectile(const FVector& StartLocation, const FVector& ImpactLocation, float FlightTime);

	/** 조준 지점에 거리 비례 산포를 얹어 실제 착탄점을 만든다. 서버에서만 굴린다. */
	FVector ResolveImpactLocation(const FVector& MuzzleLocation, const FVector& AimLocation) const;

	/** 착탄 판정. 비행시간이 지난 뒤 서버에서만 돈다. */
	void ResolveImpact(FVector ImpactLocation);

	/** 착탄점을 덮는 싱크홀을 찾는다. 없으면 nullptr. */
	class ACWSinkhole* FindSinkholeAt(const FVector& ImpactLocation) const;

	/** 재장전 완료. 서버에서만 돈다. */
	void FinishReload();

	/** 총구 위치. 메쉬에 소켓이 없으면 액터 위치를 쓴다. */
	FVector GetMuzzleLocation() const;

protected:
	// 한 탄창의 탄 수. (기획서 7-3)
	//
	// 싱크홀 필요 탄수(6)보다 넉넉하게 둔다. 산포 때문에 원거리 사격은 반드시 빗나가는데
	// 탄창을 필요 탄수에 맞추면 두 발만 빗나가도 재장전이 강제되어 처벌이 과해진다.
	// 남는 4발은 수동 재장전과 묶여 "언제 채울지 판단하는 재료" 가 된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt|Gun", meta = (ClampMin = "1"))
	int32 MagazineCapacity = 10;

	// 연사 간격(초). 밸런스 값이다. InputMinInterval 과 혼동하면 안 된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt|Gun", meta = (ClampMin = "0.05"))
	float FireInterval = 0.4f;

	// 입력 하한(초). 중복 격발로 인한 버그를 막기 위한 기술적 값이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt|Gun", meta = (ClampMin = "0.01"))
	float InputMinInterval = 0.05f;

	// 재장전 시간(초).
	//
	// 재장전 중에도 이동은 막지 않는다. 2초간 발이 묶이면 차를 피할 수 없어
	// 플레이어가 손쓸 수 없는 죽음이 나온다. 사격만 막아도 압박은 충분히 전달된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt|Gun", meta = (ClampMin = "0.1"))
	float ReloadDuration = 2.f;

	// 투사체 비행 시간(초). 착탄 판정도 이 시간만큼 늦춰서 연출과 맞춘다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt|Gun", meta = (ClampMin = "0.0"))
	float ProjectileFlightTime = 0.35f;

	// 이 거리(cm) 안에서는 산포가 없다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt|Gun|Spread", meta = (ClampMin = "0.0"))
	float NoSpreadDistance = 300.f;

	// 산포가 최대에 이르는 거리(cm).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt|Gun|Spread", meta = (ClampMin = "1.0"))
	float MaxSpreadDistance = 2000.f;

	// 최대 거리에서의 산포 반경(cm). 싱크홀 반지름과 같이 봐야 한다.
	// 이 값이 싱크홀 반지름보다 훨씬 크면 원거리 사격이 사실상 불가능해진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt|Gun|Spread", meta = (ClampMin = "0.0"))
	float MaxSpreadRadius = 220.f;

	// 빗나갔을 때 바닥에 남길 자국.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt|Gun")
	TSubclassOf<ACWAsphaltSplat> SplatClass;

	// 날아가는 덩어리의 궤적 연출. 비워 두면 궤적이 보이지 않는다.
	// 기본값이 들어 있어 BP 애셋을 만들지 않아도 바로 확인할 수 있다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt|Gun")
	TSubclassOf<ACWAsphaltProjectileVisual> ProjectileVisualClass;

	// 총구 소켓. 없으면 액터 위치에서 발사된 것으로 친다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt|Gun")
	FName MuzzleSocketName = TEXT("Muzzle");

private:
	// 남은 탄. 서버만 바꾸고 전원이 받는다.
	UPROPERTY(ReplicatedUsing = OnRep_AmmoState)
	int32 CurrentAmmo = 0;

	UPROPERTY(ReplicatedUsing = OnRep_AmmoState)
	bool bReloading = false;

	// 마지막 발사 시각(월드 시간). 연사 간격 검사에 쓴다. 서버에서만 의미가 있다.
	float LastFireTime = -1000.f;

	FTimerHandle ReloadTimerHandle;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Asphalt|Gun")
	FCWOnAsphaltAmmoChangedSignature OnAmmoChanged;
};
