# 싱크홀 시점 대응 v3
2026-09-16. 현재 적용 버전.

## 변경
- M_SinkholeVoid의 고정된 그림을 Unreal 기본 머티리얼 노드로 만든 시점 대응 깊이 표현으로 대체했습니다.
- 바깥 실루엣은 지면 기준의 불규칙한 다각형과 작은 깨짐으로 고정합니다.
- CameraVectorWS와 평면의 위치로 바닥 아래 가상 공동의 깊이를 계산합니다. 카메라를 돌리면 보이는 안쪽 벽도 맞춰 바뀝니다.
- 기존의 따뜻한 회색, 낮은 대비, DefaultLit / Substrate Slab 및 외부 균열 데칼을 유지했습니다.
- 새 이미지 파일이나 Custom HLSL은 사용하지 않았습니다.
- C++ 소스, Blueprint, 낙하 판정, 메우기 로직은 수정하지 않았습니다.
- 이는 기존 평면 위의 깊이 착시 표현이며 도로 메시 자체를 파내는 변경은 아닙니다.

## 조정 항목
M_SinkholeVoid의 View Stable Sinkhole 그룹:
- CavityRadius = 0.875
- DepthContrast = 1.10
- LipWidth = 0.026
- RoadLipTone, CutEdgeTone, WallTone, DeepWallTone, VoidTone: 깊이별 색상
- SurfaceRoughness = 1.0 유지

## 복원
이전 텍스처 방식 머티리얼:
`/Game/CrossWalk/Sinkhole/Materials/Backup/M_SinkholeVoid_BeforeViewStable`

## 확인
Prototype의 기존 싱크홀을 Simulate로 실행하고 동일한 높이에서 네 방향을 비교했습니다. 위쪽 시점도 확인했습니다. 머티리얼 컴파일/저장이 성공했고 편집기 카메라를 복원했습니다.
카메라가 낮아질 때 지면의 원근에 따른 타원형 변화는 자연스럽게 남습니다.

### 서쪽
![서쪽](E:/UE_5.8/Crosswalk/Saved/Diagnostics/Sinkhole_ViewStable_West.png)

### 남쪽
![남쪽](E:/UE_5.8/Crosswalk/Saved/Diagnostics/Sinkhole_ViewStable_South.png)

### 동쪽
![동쪽](E:/UE_5.8/Crosswalk/Saved/Diagnostics/Sinkhole_ViewStable_East.png)

### 북쪽
![북쪽](E:/UE_5.8/Crosswalk/Saved/Diagnostics/Sinkhole_ViewStable_North.png)

### 위쪽
![위쪽](E:/UE_5.8/Crosswalk/Saved/Diagnostics/Sinkhole_ViewStable_Top.png)
