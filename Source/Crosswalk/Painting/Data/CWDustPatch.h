// Copyright Crosswalk Team. All Rights Reserved.
// CWDustPatch.h

#pragma once

#include "CoreMinimal.h"
#include "CWDustPatch.generated.h"

class UDecalComponent;
class UMaterialInstanceDynamic;

/**
 * 먼지 존을 이루는 데칼 패치 하나. (ACWWorkZone_Dust 참고)
 *
 * 존 하나를 큰 직사각형 데칼 한 장으로 덮으면 실루엣이 직선이라 "바닥에 붙인 스티커" 로 보인다.
 * 그래서 존을 패치 여러 장으로 흩뿌리고, 패치마다 회전 / 크기 / 텍스처 위치 / 밝기를 다르게 준다.
 *
 * 이 값들은 리플리케이트하지 않는다. 배치를 만드는 난수 시드가 존 이름에서 나오므로
 * 모든 기계가 같은 배치를 각자 만들어낸다. 네트워크로 오가는 것은 진행도 하나뿐이다.
 */
USTRUCT()
struct FCWDustPatch
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TObjectPtr<UDecalComponent> Decal = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material = nullptr;

	// 이 패치가 텍스처의 어느 자리를 볼 것인가. 이게 없으면 패치마다 같은 알갱이가 나온다.
	UPROPERTY(Transient)
	FVector2D UVOffset = FVector2D::ZeroVector;

	// 흙 텍스처의 실제 크기를 유지하기 위한 UV 배율. 패치 크기에 비례한다.
	UPROPERTY(Transient)
	float Tiling = 1.f;

	// 밝기만 흔든 색. 색조까지 흔들면 먼지가 아니라 얼룩으로 보인다.
	UPROPERTY(Transient)
	FLinearColor Tint = FLinearColor::White;

	/**
	 * 이 패치가 몇 번째로 걷히는가. 진행도를 이 패치의 알파로 바꿀 때 쓴다.
	 *
	 * 배열 순서를 그대로 쓰면 먼지가 한쪽 끝에서부터 차례로 사라져서 부자연스럽다.
	 * 배치와 같은 시드로 한 번 섞어서 넣는다.
	 */
	UPROPERTY(Transient)
	int32 ClearSlot = 0;

	// 지금 알파. 1 이면 그대로, 0 이면 다 걷혔다. 0 으로 넘어가는 순간을 잡아 연출을 띄운다.
	UPROPERTY(Transient)
	float Fade = 1.f;
};
