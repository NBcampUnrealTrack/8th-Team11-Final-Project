# 툰 싱크홀 제작 기록
현재 적용 버전은 [시점 대응 v3](VIEW_STABLE_v3.md)입니다. 아래는 최초 제작 기록입니다.
2026-09-16. 내장 image_gen 도구로 투명 PNG 2종을 생성하고 Unreal MCP로 머티리얼에 연결했습니다.

## 원본 이미지
- T_Sinkhole_Toon.png: 불규칙한 아스팔트 가장자리, 흙 단면, 어두운 내부.
- T_Sinkhole_Cracks_Toon.png: 투명 배경의 꺾이고 분기하는 균열.
- 원본 알파를 유지했습니다. Unreal에서 1024×1024로 빌드하고 BC7, 밉맵, Clamp를 사용합니다.

## 프로젝트 연결
- /Game/CrossWalk/Sinkhole/Textures/T_Sinkhole_Toon
- /Game/CrossWalk/Sinkhole/Textures/T_Sinkhole_Cracks_Toon
- M_SinkholeVoid: 기존 평면에 알파 마스크와 툰 텍스처를 연결. Substrate Unlit, ArtBrightness=0.55, ArtTint로 조정 가능.
- M_SinkholeCrack: 기존 MI_SinkholeCrack의 부모를 유지하면서 텍스처 기반 Substrate 데칼로 변경. CrackOpacity=1, CrackTint=(0.25,0.25,0.25), Roughness=0.9.
- M_SinkholeHole: 기존 둥근 테두리가 새 텍스처 외곽에 겹치지 않도록 Opacity=0. 가장자리 표현은 M_SinkholeVoid에 포함.
- 변경 전 머티리얼은 /Game/CrossWalk/Sinkhole/Materials/Backup/*_BeforeToon에 보관.
- BP_Sinkhole, 낙하 판정, 메우기 횟수, C++ 소스는 변경하지 않았습니다. 기존 평면의 깊이 착시 표현이며 도로 메시를 실제로 파내지는 않습니다.

## 확인
- 머티리얼 컴파일 및 지정 애셋 저장 성공.
- Stage_02의 실제 도로에서 편집기 미리보기와 Simulate 실행 중 활성 싱크홀 렌더링 확인.
- 균열 단독 모습은 편집기에서 임시 미리보기 액터의 VoidPlane을 숨겨 확인.
- 낙하/메우기 입력 전체 흐름은 이번 작업에서 별도 검증하지 않았습니다.
- 임시 액터는 삭제했고 에디터 카메라는 복원했습니다.
- 결과 이미지: ../../Saved/Diagnostics/ToonSinkhole_Applied.png 및 ToonSinkhole_Cracks.png.

## 최종 생성 프롬프트: 싱크홀
Use case: stylized-concept
Asset type: production RGBA game ground texture for a sinkhole in a cheerful low-poly toon street-repair game, not a concept sheet or screenshot.
Primary request: one deep collapsed asphalt sinkhole, viewed EXACTLY straight down in orthographic top view, centered, circular overall but angular broken irregular outline. Make it read as a dangerous deep hole, not a raised pile of rocks, crater with a floor, black disk, or water puddle.
Composition: square 1024 x 1024, isolated on a genuinely transparent background with real alpha; no white or checkerboard baked in. Entire sinkhole and few outward cracks fit inside a 4 percent transparent margin. The opening occupies about 80 percent of the canvas width, with a narrow fractured rim extending to 92 percent. Keep it radially circular in footprint, not an ellipse, no tilted camera.
Style: clean chunky polygonal cel-shaded game art, broad simple facets, crisp dark edge accents, restrained painted shading, readable at small gameplay size. Muted blue-gray asphalt top slabs, narrow light cool-gray chipped top edges, warm desaturated brown exposed earth faces beneath, darker charcoal-violet inward rock layers dropping sharply into a near-black deep center. Large darkest inner cavity occupies most of opening; no visible flat bottom. Shape shadow so it clearly descends inward; no raised volcano or bowl.
Details: irregular fractured asphalt teeth around circumference with about 14-18 unequal chunks, several narrow angular radial cracks and a few tiny detached flat chips just outside rim, no repeated brick or stone-well pattern. Subtle layer depth shading with darkest interior, no global cast shadow outside because this is a flat ground asset. Soft light from upper left but mostly diffuse.
Avoid: photorealistic grit, realistic photographic texture, grass, lava, water, pipes, road markings, warning stripes, cones, characters, text, labels, UI, border, watermark, perspective background. Exterior outside cracks is transparent; the deep dark interior is OPAQUE, not a transparent hole.

## 최종 생성 프롬프트: 균열
Use case: stylized-concept
Asset type: transparent RGBA ground decal texture, 1024x1024 square, for the warning stage before a sinkhole opens in a cheerful low-poly toon street-repair game.
Create ONLY an irregular network of thin broken asphalt fissures, isolated on actual transparent alpha background. Strict orthographic top view, flat, no perspective. Transparent everywhere except the narrow jagged crack lines and a few small chipped edge slivers. No solid background color and no checkerboard.
Design: a loose roughly circular cracked area occupies 88 percent of frame, safe transparent margins all around. 8-10 asymmetrical branching jagged fissures grow outward from a broken polygonal central stress ring. The center stays transparent unbroken road, NOT a dark hole. Paths have angular bends and forks, differing lengths and thicknesses, never straight radial spokes or a symmetric spiderweb. Chunky simple graphic cel-shaded edges consistent with a playful low-poly city game. Fissures are dark charcoal-violet black, with a tiny narrow cool gray chipped bevel along a few edges, no white glow. Main cracks 6-14 pixels thick, narrowing toward tips; small branches thinner. No closed filled stone slabs: all spaces between lines transparent so the underlying game asphalt remains visible.
No crater, no dark disk, no floating rock pile, no earth, no radial sun rays, no photographic microtexture, no grass, no text, no watermark, no road markings, no border. The crack network must be easy to read at gameplay distance, but not cover the road in a thick black patch.
