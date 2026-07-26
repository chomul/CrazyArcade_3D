# Checklist 07 — HISMVoxelRenderer

> 대응 Task: `mds/Tasks/07-HISMVoxelRenderer.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] `UActorComponent` + `IVoxelRenderer` 구현
- [ ] 생성·빌드 경로에 `if (IsRunningDedicatedServer()) return;` (데디에서 렌더러 생성 안 됨)
- [ ] **RemoveInstance 스왑 보정** — 마지막 인스턴스의 셀→인덱스/인덱스→셀 맵 갱신 코드 존재
- [ ] 표면 판정: 6면 중 하나라도 Empty(범위 밖 포함)면 인스턴스화
- [ ] 메시·머티리얼은 BP 지정 프로퍼티 — 동적 머티리얼 생성 없음

## 에디터 연결
- [ ] `BP_VoxelWorld`에 블록 3종 메시 지정
- [ ] `L_Arena` 맵에 배치, 열림 확인 후에만 `GameDefaultMap` 주석 해제

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [ ] **맵이 눈에 보이고** Task 04 레이아웃과 일치 (층·기둥·계단)
- [ ] 표면 추출 비율 20~40% (로그: 총 블록 vs 인스턴스 수)
- [ ] 블록 1개 파괴 → 그 블록만 사라짐 (엉뚱한 블록 사라지면 인덱스 맵 버그)
- [ ] 파괴 후 주변 6칸의 새 표면 노출
- [ ] 연속 파괴 20회+ 에서도 렌더·그리드 불일치 없음
- [ ] `stat unit` — BuildFromGrid 시 1회성 히치 외 지속 스파이크 없음
