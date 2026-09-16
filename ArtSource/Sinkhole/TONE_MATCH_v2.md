# 싱크홀 톤 조정 v2
2026-09-16. 첨부된 Prototype 플레이 화면을 기준으로 수정했습니다.

## 결과
- 원본: T_Sinkhole_Toon_v2.png (내장 image_gen으로 기존 텍스처를 스타일 수정).
- UE 텍스처: /Game/CrossWalk/Sinkhole/Textures/T_Sinkhole_Toon_v2
- M_SinkholeVoid: 푸른 돌/흰 모서리/세밀한 암반 그림을 얇은 아스팔트 가장자리와 단순한 면으로 대체.
- 자체 발광 Unlit에서 씬 조명을 받는 Substrate Slab / DefaultLit로 전환.
- ArtBrightness=0.35, PaletteDesaturation=0.85, SurfaceRoughness=1.0, F0=0.
- M_SinkholeCrack: CrackOpacity=0.7, CrackTint=(0.23,0.22,0.20). 기존 균열의 과한 대비를 낮춤.
- 텍스처 원본 알파 유지. UE 빌드 설정은 1024x1024, BC7, Clamp, 밉맵 사용.
- 이전 버전 머티리얼: /Game/CrossWalk/Sinkhole/Materials/Backup/*_BeforeToneMatch
- Blueprint, 맵 배치, 낙하/메우기 로직, C++ 소스는 변경하지 않았음.

## 확인
- 머티리얼 컴파일 성공 및 지정 애셋 저장.
- Prototype의 기존 싱크홀을 Simulate에서 실행하여 실제 차량/도로 조명 아래 외형 확인.
- 실행 종료 후 원래 에디터 카메라 복원.
- 적용 화면: ../../Saved/Diagnostics/ToonSinkhole_ToneMatched_v2.png

## 최종 생성 프롬프트 (내장 image_gen, 이미지 편집)
Use case: style-transfer
Asset type: revised transparent RGBA sinkhole ground texture for the actual low-poly cartoon game in reference image 2.
Input images: Image 1 is the current sinkhole texture to redesign. Image 2 is the actual game screenshot and is the authoritative STYLE AND COLOR reference only, do not edit the screenshot.
The current sinkhole looks like a detailed painted rock well pasted onto the flat game road. REDESIGN the texture to solve this decisively.
Output only the revised sinkhole asset, not a screenshot, square 1024x1024, real transparent background. Keep the centered top-down orthographic circular footprint, opaque dark cavity center, and transparent exterior. Do not include anything from the screenshot except its style.
Required style: VERY SIMPLE low-poly cel shading like the cars and flat asphalt in image 2; large uniform color regions, only 3 or 4 restrained shadow tones. No grain, mottling, rocky texture, tiny facets, brush noise, realistic shading, white highlights, blue-purple cast, or shiny rims.
Required shape: the road has caved inward, a jagged torn opening in thin flat asphalt. REMOVE the ring of separate bright stones. Replace it with an extremely narrow irregular chipped lip continuous with the road, warm neutral gray / brown-gray (#56524e to #45413e), with 9 to 12 unequal broad angular breaks. No raised rocks, no cobblestone ring, no brick well, no rubble sitting on top of the road, no outward cast shadow. Keep the outer contour near 90 percent canvas diameter.
Inside: sparse broad trapezoid wall faces descending inward, dark muted warm gray, with just a very thin dull dark brown earth seam directly under the asphalt. Faces must appear recessed, never extruded upward. At least 65 percent of opening area is a simple deep charcoal center (#111315 or darker). Depth must remain readable using a few broad flat planes, without a tunnel full of tiny triangular rocks or a radial starburst.
Outer lip should occupy at most 3 to 5 percent of overall radius. No bright edge outline. A few short hairline jagged cracks may extend into transparent space, but keep them sparse; the game has a separate cracks decal.
The result should visually merge into the flat gray-brown game asphalt rather than read as an illustration or a collectible item. Main palette neutral warm charcoal, desaturated gray-brown, no saturated colors. Entire exterior beyond contour transparent; inner dark void fully opaque. No text, logos, UI, yellow lines, objects, characters, photographic texture, white backdrop or checkerboard.
