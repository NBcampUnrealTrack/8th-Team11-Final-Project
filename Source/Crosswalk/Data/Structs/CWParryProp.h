// CWParryProp.h

#pragma once

#include "CoreMinimal.h"

#include "CWParryProp.generated.h"

class UNiagaraSystem;
class USoundBase;
class UStaticMesh;

/**
 * 패링 스윙에 쓸 소품 하나의 정의. 배트, 형광봉 같은 것들이다.
 *
 * 메시와 사운드를 한 행에 묶어 둔 것이 핵심이다. 둘을 따로 배열로 두면 인덱스가 어긋났을 때
 * 배트를 들고 형광봉 소리가 나는 상태가 되는데, 그건 데이터를 보고는 알아차리기 어렵다.
 */
USTRUCT(BlueprintType)
struct FCWParryProp
{
	GENERATED_BODY()

public:
	/** 로그와 에디터 표시용 이름. 동작에는 쓰지 않는다. */
	UPROPERTY(EditAnywhere, Category = "Prop")
	FName PropName;

	/** 손에 나타날 메시. */
	UPROPERTY(EditAnywhere, Category = "Prop")
	TObjectPtr<UStaticMesh> Mesh;

	/** 이 소품을 휘두를 때 나는 소리. 비우면 이 소품이 뽑힌 패링이 무음이 된다. */
	UPROPERTY(EditAnywhere, Category = "Prop")
	TObjectPtr<USoundBase> SwingSound;

	/**
	 * 이 소품이 차를 쳐낼 때 터지는 이펙트. 비우면 이 소품이 뽑힌 패링만 이펙트 없이 나간다.
	 *
	 * 사운드 옆에 두는 이유는 메시와 같다. 배트는 금속 스파크, 형광봉은 빛 번짐처럼
	 * 소품마다 달라야 하는데, 배열을 따로 두면 인덱스가 어긋났을 때 형광봉에서
	 * 쇳가루가 튀는 상태가 되고 그건 데이터만 봐서는 못 찾는다.
	 */
	UPROPERTY(EditAnywhere, Category = "Prop")
	TObjectPtr<UNiagaraSystem> SwingEffect;

	/**
	 * 컴포넌트에 적용할 크기.
	 *
	 * 트라이포 메시는 가장 긴 축이 100 근처로 정규화되어 들어오는데 캐릭터는 98cm 다.
	 * 그대로 두면 자기 키만 한 배트를 휘두르게 된다. 소품마다 원래 길이가 달라서
	 * (배트 138, 형광봉 125) 값을 따로 둔다.
	 */
	UPROPERTY(EditAnywhere, Category = "Prop")
	float Scale = 1.f;

	/**
	 * 소켓 기준 미세조정.
	 *
	 * 소품마다 소켓을 따로 만들지 않기 위한 장치다. 예전에 ToolSocket_R 하나를
	 * 도구별 소켓으로 늘렸다가 인게임 어태치가 깨진 적이 있어서, 소켓 구조는 건드리지 않고
	 * 차이를 여기서 흡수한다.
	 *
	 * 배트와 형광봉은 둘 다 원점이 그립이고 +Z 로 뻗도록 임포트해서, 보통은 기본값으로 둔다.
	 */
	UPROPERTY(EditAnywhere, Category = "Prop")
	FTransform GripOffset;
};
