// CWTrafficLog.h

#pragma once

#include "CoreMinimal.h"

/**
 * 트래픽 파트 전용 로그 카테고리. 차량·차선·스포너·신호·패링이 전부 여기로 찍는다.
 *
 * 콘솔에서 `Log LogCWTraffic Verbose` 로 켜고 `Log LogCWTraffic Warning` 으로 조용히 만든다.
 * 줄 앞의 [TrafficSync] [TrafficNet] [Parry] 태그는 그대로 둔다 - 카테고리는 "누가 찍었나",
 * 태그는 "무엇에 관한 줄인가" 라서 역할이 다르고, 4대 로그를 나란히 놓고 읽는 절차가 태그로 걸러낸다.
 *
 * 진단 로그의 켜짐 여부는 여전히 cw.Traffic.* 콘솔 변수가 정한다(UCWTrafficSubsystem 참고).
 * 그쪽은 "찍을지 말지" 를 사건 단위로 고르는 스위치라 Verbosity 로 대신할 수 없다.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogCWTraffic, Log, All);
