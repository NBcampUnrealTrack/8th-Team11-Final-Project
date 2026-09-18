// Copyright Crosswalk Team. All Rights Reserved.
// CWInteractionLog.h

#pragma once

#include "CoreMinimal.h"

/**
 * 상호작용 프롬프트 전용 로그 카테고리.
 *
 * 콘솔에서 `Log LogCWInteraction Verbose` 로 켜고 `Log LogCWInteraction Warning` 으로 조용히 만든다.
 * 줄 앞의 [Prompt] [PromptPerf] 태그는 그대로 둔다 - 카테고리는 "누가 찍었나",
 * 태그는 "무엇에 관한 줄인가" 라서 역할이 다르다. 여러 대의 로그를 나란히 놓고 읽을 때 태그로 거른다.
 *
 * 진단 로그를 실제로 찍을지는 cw.Prompt.LogDiagnostics 가 정한다.
 * (UCWInteractionPromptComponent 참고. LogCWTraffic 과 같은 구조다)
 */
DECLARE_LOG_CATEGORY_EXTERN(LogCWInteraction, Log, All);
