# 로비 연출 — 2026-09-16

## 적용된 흐름

- Title의 방 생성 팝업에서 생성 버튼을 누르면 2회 경적이 울리고 UI 입력과 표시가 잠시 잠긴다.
- `LS_Lobby_CreateRoom`이 메뉴 전경에서 트럭 앞유리로 3.2초 동안 접근한다. OnFinished에서 기존 `CWSessionViewModel.RequestCreateRoom`을 호출한다.
- 기존 방 생성 성공 처리가 Room 화면을 연다. 방 생성 실패 시 팝업과 이전 카메라를 복구한다.
- `BP_LobbyPresentation`은 Room/StageSelect 화면에서 로비 카메라를 사용한다. 다른 페이지로 나가면 메뉴 카메라로 복귀하고 승객 표시를 정리한다.
- 서버가 플레이어 ID에 따라 빈 좌석을 배정하고 SeatOwnerId0~3을 복제한다. 먼저 앉은 사람의 자리는 유지되며, 퇴장한 사람의 좌석만 비워진다.
- 새로 배정된 승객은 약 0.67초 동안 옆에서 좌석으로 이동하며 내려앉고, 기존 `AS_Rider_MotorCycle_Idle_Loop` 착석 루프를 사용한다. 문 열기/걷기/전용 승하차 모션은 별도로 제작하지 않았다.

## 애셋

- `/Game/CrossWalk/Maps/Title/Lobby/BP_LobbyPresentation`
- `/Game/CrossWalk/Maps/Title/Cinematics/LS_Lobby_CreateRoom`
- `/Game/CrossWalk/Maps/Title/Lobby/Audio/SW_Lobby_DoubleHorn`
- 원본 음원: `ArtSource/Lobby/SW_Lobby_DoubleHorn.wav` (직접 합성한 2회 경적)
- 변경 UI: `WBP_CreateRoom`, `WBP_Room` (방 화면의 전체 배경을 투명하게 하여 트럭 표시)
- 배치 차량: 기존 `BP_DoubleCabTruck_5Meshes`, 위치 `(14200, 0, 8)` — Title 중앙 포장 구역, 주행 차선 밖.
- 좌석 기준: 기존 트럭의 Seat_Front_L/R, Seat_Rear_L/R.
- 기본 `PreviewPlayerCount=0`. 이 값이 양수면 실인원 대신 미리보기 인원을 사용하므로 배포/일반 실행은 반드시 0으로 유지한다.
- 레벨에 배치된 Presentation 액터의 Replicates/Always Relevant가 켜져 있어야 한다.

## 검증

- 실제 방 생성 버튼 클릭 → 시퀀스 재생 → 세션 생성 성공 → Room UI 전환 확인 (현재 에디터 OnlineSubsystem NULL 환경).
- 카메라 최종 위치 `(14635, -28, 188)`, 회전 `(-4, 180, 0)`, FOV 75 확인.
- 실제 호스트 ID가 첫 좌석에 배정되고 준비 상태 변경 시 착석 진행도가 1로 유지됨을 확인.
- 4인 Listen Server PIE에서 호스트 + 세 클라이언트 모두 동일한 좌석 ID `[307, 308, 309, 310]`, 착석 진행도 `[1, 1, 1, 1]` 확인.
- 2인 PIE에서 클라이언트 연결 종료 후 서버 목록 `[311, 312] → [311]`, 두 번째 좌석 `312 → -1`, 호스트 착석 진행도 1 유지 확인.
- 테스트 후 PIE를 종료하고 실행 설정을 Standalone / 1인 / 기존 창 크기로 복구했다.
- EOS/Steam 외부 네트워크 실접속은 별도로 검증하지 않았다.
- C++ 소스 파일은 변경하지 않았다.

## 복구 지점

`/Game/CrossWalk/Maps/Title/Backup/` 안에 다음 변경 전 사본이 있다.

- `Title_BeforeLobby_20260916`
- `WBP_CreateRoom_BeforeLobby_20260916`
- `WBP_Room_BeforeLobby_20260916`

## Helmet02 연출 착용 (2026-09-16)

- 로고 주변 작업자 2명: BP_TitleHelmet02의 Wearer 참조를 통해 head 본에 SM_Helmet02 부착.
- 로비의 Passenger0~3: Helmet02_Seat0~3 컴포넌트를 각 head 본에 부착.
- DA_Helmet02의 기존 상대 위치·회전·크기를 사용한다. 좌석 캐릭터의 크기를 상속한다.
- ConfigureHelmets를 ConstructionScript와 BeginPlay에서 호출하여 레벨에 이미 배치된 인스턴스도 초기화한다.
- 좌석 표시/숨김은 자식에게 전파하여 빈 좌석에 헬멧만 남지 않게 한다. 헬멧은 충돌·오버랩·데칼 수신을 끈다.
- PIE에서 로고 작업자 착용, 로비 4인 착용 화면, 4→1인 좌석 표시, 로비 종료 시 전원 숨김을 확인했다. 테스트용 PreviewPlayerCount는 0으로 복구했다.
- 변경 전 사본: Title_BeforeHelmet02_20260916, BP_LobbyPresentation_BeforeHelmet02_20260916 (Title/Backup).

## main 통합 검증 (2026-09-17)

- `cbe3c25` 기준 최신 main에 리베이스했다. 새 수첩형 방 생성 UI와 방 목록 입력 잠금 처리, 방 제목·참여 코드·복사 UI를 유지하면서 로비 연출을 연결했다.
- 차량 폴더 정리에 맞춰 Title 및 시퀀스의 트럭 참조를 `/Game/CrossWalk/Traffic/Vehicles/Meshes/DoubleCabTruck/` 경로로 저장했다. 임시 CoreRedirect 설정은 제거했다.
- main에서 제거된 기존 착석 루프는 `/Game/CrossWalk/Maps/Title/Lobby/Animation/AS_Rider_MotorCycle_Idle_Loop`에 로비 전용으로 보존했다.
- Stage_02는 main의 조명·배치를 유지하며 CWPaintDecalField에 MI_PaintSplat, DecalRadius 120, ScatterRadiusPerCan 90을 적용했다.
- CrosswalkEditor Win64 Development 빌드와 관련 Blueprint 8개 컴파일을 통과했다.
- 리베이스 통합 후 Standalone PIE에서 새 수첩 UI로 방 생성 → 트럭 로비 진입 → Helmet02를 쓴 호스트 착석 및 방 제목·참여 코드 표시를 확인했다. 다인 접속 검증은 위의 통합 전 검증 기록을 참고한다.
- 로컬 Backup 애셋은 PR에 포함하지 않으며, 최종 Title·로비·방 생성·방 화면의 의존성에 Backup 경로가 없는 것을 확인했다.
