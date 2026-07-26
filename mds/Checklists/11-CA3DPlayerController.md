# Checklist 11 — CA3DPlayerController

> 대응 Task: `mds/Tasks/11-CA3DPlayerController.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] 게임 상태를 바꾸는 코드 없음 (입력·카메라만)
- [ ] IMC 등록이 로컬 컨트롤러에서만 (`IsLocalController`)
- [ ] IMC/IA는 프로퍼티 지정 — 하드코딩 키 바인딩 없음
- [ ] 두 입력 기준(월드축/카메라) 토글 존재

## 에디터 연결
- [ ] `IMC_Default` + `IA_Move/Jump/PlaceBomb/RotateCam` 에셋 생성·연결
- [ ] GameMode `PlayerControllerClass` 지정

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [ ] 45도 스냅 회전 8방향 모두 순회 가능 (Q/E 왕복)
- [ ] 회전 중 이동 입력이 끊기지 않음
- [ ] 입력 기준 토글 시 WASD 매핑이 의도대로 바뀜
- [ ] 카메라가 벽 안으로 파고들지 않음 (가림 반투명은 3주차 — 범위 아님)

## 튜닝 확정 (사용자 답변 필요 — 답변 전 미검증)
- [ ] WASD 입력 기준(월드축 vs 카메라) 확정
