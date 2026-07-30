# Checklist 11 — CA3DPlayerController

> 대응 Task: `mds/Tasks/11-CA3DPlayerController.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [x] `CrazyArcade3DEditor` 빌드 통과 (2026-07-29)
- [x] `CrazyArcade3DServer` 빌드 통과 (2026-07-29)
- [x] 프로젝트 파일 재생성 실행 (2026-07-29)

## 코드 검증 (정적)
- [x] 게임 상태를 바꾸는 코드 없음 (입력·카메라만)
- [x] IMC 등록이 로컬 컨트롤러에서만 (`IsLocalPlayerController` + 데디 가드)
- [x] IMC/IA는 프로퍼티 지정 — 하드코딩 키 바인딩 없음
- [x] 두 입력 기준(월드축/카메라) 토글 존재 — `ca3d.CameraRelativeInput` (기본 0=월드 축)

## 에디터 연결
- [x] `IMC_Default` + `IA_Move/Jump/PlaceBomb/RotateCam` 에셋 생성·연결 (2026-07-30 · 에셋 존재 + PIE 에서 이동·점프·설치 입력 동작으로 실증)
- [x] GameMode `PlayerControllerClass` 지정 (BP_CA3DPlayerController — 입력 바인딩이 동작하므로 확인)

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [ ] 45도 스냅 회전 8방향 모두 순회 가능 (Q/E 왕복)
- [ ] 회전 중 이동 입력이 끊기지 않음
- [x] 입력 기준 토글 시 WASD 매핑이 의도대로 바뀜 — 사용자 PIE 비교 후 **카메라 기준으로 확정** (2026-07-30). `ca3d.CameraRelativeInput` 기본값 1, 월드 축(0)은 비교·디버그용으로 유지
- [x] ~~카메라가 벽 안으로 파고들지 않음~~ → **정책 변경 (2026-07-30 사용자 결정)**: 붐 컬리전(`bDoCollisionTest`)을 **끈다**. 켜두면 지형에 닿을 때 팔 길이가 줄어 화면이 확 확대되는데, 고정 시점 아케이드에서는 시야 배율이 튀는 쪽이 지형에 걸치는 것보다 나쁘다(칸 세기가 무너짐). 가림 블록은 3주차 디더 페이드로 처리 — 거리 축소로는 해결하지 않는다

## 튜닝 확정 (사용자 답변 필요 — 답변 전 미검증)
- [x] WASD 입력 기준(월드축 vs 카메라) 확정 — **월드 축** (2026-07-29 사용자 확정, 잠정 — 토글로 언제든 비교 가능)
