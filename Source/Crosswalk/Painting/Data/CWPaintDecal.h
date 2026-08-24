// Copyright Crosswalk Team. All Rights Reserved.
// CWPaintDecal.h

#pragma once

#include "CoreMinimal.h"
#include "CWPaintDecal.generated.h"

/**
 * 터진 페인트 통이 남긴 자국 하나. (기획서 6-4)
 *
 * 파손 한 번이 이 자국 여러 개를 흩뿌린다. 큰 얼룩 하나로 만들지 않는 이유가 두 가지다.
 *   1. 그림 : 통이 터지면 사방으로 튀는 것이 자연스럽다
 *   2. 조작 : 큰 얼룩 하나면 어디를 문질러도 전체가 흐려져서 "지우고 있다" 는 감각이 없다
 *             작은 자국 여러 개면 하나씩 사라져서 진척이 눈에 보인다
 *
 * 서버가 만들고 배열째로 리플리케이트한다. 각 머신은 이 값으로 데칼 컴포넌트를 다시 만든다.
 */
USTRUCT()
struct FCWPaintDecal
{
	GENERATED_BODY()

public:
	// 자국 중심. 지면에 붙인 좌표다.
	UPROPERTY()
	FVector_NetQuantize Location = FVector::ZeroVector;

	// 자국 반경(cm).
	UPROPERTY()
	float Radius = 0.f;

	// 얼마나 남았는가. 1 이면 그대로, 0 이면 다 지워져 사라진다.
	UPROPERTY()
	float Remaining = 1.f;
};
