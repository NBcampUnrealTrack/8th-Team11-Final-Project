// Copyright Crosswalk Team. All Rights Reserved.
// CWBrushStroke.h

#pragma once

#include "CoreMinimal.h"
#include "CWBrushStroke.generated.h"

/**
 * 붓질 한 번. 칸을 칠하는 존이 네트워크로 주고받는 최소 단위다.
 *
 * 칸 배열 자체를 실시간으로 리플리케이트하지 않는다. 수백~수만 개 bool 을 통째로 보내는 대신
 * "어디를 얼마만큼 칠했다" 만 보내면 각자 같은 그림에 도달한다.
 *
 * 하나씩 보내지 않고 배열로 묶어서 보낸다. 붓질은 작업 키를 누르고 있는 동안 매 프레임
 * 생기는데, 그걸 그대로 하나씩 Reliable 로 내보내면 4명이 동시에 칠할 때 커넥션마다
 * 초당 240개가 쌓인다. 붓 궤적은 연속이라 몇 개를 묶어 보내도 그려지는 그림은 같다.
 *
 * 좌표가 FVector_NetQuantize 인 이유는 1cm 단위 반올림을 모든 기계가 똑같이 맞추기
 * 위해서다. 자세한 사정은 ACWWorkZone_Cells::QuantizeStrokeLocation 주석 참고.
 */
USTRUCT()
struct FCWBrushStroke
{
	GENERATED_BODY()

	// 붓 중심. 존 로컬이 아니라 월드 좌표다.
	UPROPERTY()
	FVector_NetQuantize Center = FVector::ZeroVector;

	// 붓 반경(cm). 체온에 따른 계수가 이미 곱해진 값이 들어온다.
	// 세 경로(로컬 적용 / 서버 제출 / 멀티캐스트)가 같은 값을 써야 기계마다 그림이 갈라지지 않는다.
	UPROPERTY()
	float Radius = 0.f;

	// true 면 칸을 켜고, false 면 끈다.
	UPROPERTY()
	bool bFilled = false;

	// true 면 Area 바깥 고리만 건드린다. (검은색 롤러 / 차량 자국)
	UPROPERTY()
	bool bSpillOnly = false;
};
