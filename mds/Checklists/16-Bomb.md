# Checklist 16 — Bomb

> 대응 Task: `mds/Tasks/16-Bomb.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [x] `CrazyArcade3DEditor` 빌드 통과 (2026-07-29)
- [x] `CrazyArcade3DServer` 빌드 통과 (2026-07-29)
- [x] 프로젝트 파일 재생성 실행 (2026-07-29)

## 코드 검증 (정적)
- [x] 퓨즈 타이머가 서버 전용 — 클라에 타이머 코드 없음 (불변식 3 — FuseTimer 는 ServerArm 에서만 세팅)
- [x] 풀링 미사용 (스폰/디스트로이 — ProcessChainStep 이 Destroy, WaterSegment/DangerDecal 만 풀링)
- [x] `ServerPlaceBomb` 검증 4종: 개수·셀 Empty·중복 폭탄·Alive
- [x] `bDetonated` 중복 폭발 방지 (+ 자동화 테스트 ⑪·⑫로 동작 확인)
- [x] 퓨즈·잔존 시간 룰셋 참조 (BombFuseTime·WaterLingerTime·ChainStepDelay — GameState 복제 → CDO 폴백)
- [x] 프리뷰 데칼이 `Propagate` 재사용 — 별도 범위 계산 로직 없음 (ABomb::TryShowDangerPreview)
- [x] 클라 시각 경로(BeginPlay 연출)에 데디 가드 (ABomb·ExplosionFXRelay·프리뷰 전부 IsRunningDedicatedServer 가드)

## 동작 검증 — 자동화 (2026-07-29 · `CrazyArcade3D.Gameplay.Bomb` 헤드리스 통과)
- [x] 설치 셀 계산: 지상 발밑·공중 -Z 스캔·경계 파고듦 보정·발판 없음 거부 (잠정 규칙)
- [x] 권위 검증: 개수 초과·점유 셀·솔리드 셀·사망 상태 → 거부 / 정상 → 스폰+장전
- [x] 퓨즈 만료 → Destructible 파괴(ServerDestroyBlocks 단일 경로)·Floor 보존·발밑 셀 피격 Trap·범위 밖 회피
- [x] 물줄기 세그먼트 Multicast → 풀 획득 (칸 수 일치)
- [x] 연쇄: 1단계 즉시 + 다음 단계 ChainStepDelay 분산, bDetonated 중복 방지, 슬롯 반환→재설치 가능

## 동작 검증 (PIE 필수 — 미실행 시 미검증)

근거 세션: 2026-07-30 · 2인 PIE(Listen + 클라 1, 지연 에뮬레이션 PktLagMin 100 / PktLagMax 130) ·
`Saved/Logs` 기록 기준. 폭탄 36개 설치·연쇄 다수·솔리드 756→737칸(19칸 파괴).

- [x] 설치 키 → 발밑 셀에 폭탄 → `BombFuseTime` 후 폭발 — 로그: 장전 22:31:44.592 → 폭발 처리 22:31:47.599 = **3.007초** (룰셋 `BombFuseTime` 3.0초와 일치)
- [ ] 6방향 물줄기 FX가 풀에서 나오고 `WaterLingerTime` 후 반납 — 부분 확인: 서버 물줄기 셀 4~6칸 로그 확인. **풀 획득·반납 실측은 Verbose 로그 필요 — 미검증**
- [ ] 블록 파괴 + 렌더 갱신 + 그 위 캐릭터 낙하 (설계서 5장 7번) — 부분 확인: 파괴 로그 + 솔리드 756→737칸 감소로 그리드 반영 확인. **"그 위 캐릭터 낙하" 미확인**
- [ ] 물줄기 피격 → Trap. **제자리 점프는 피격, 다른 발판 점프는 회피** (5장 10번) — 이 세션에 갇힘·익사 로그 없음 → 미검증
- [x] 프리뷰 데칼과 실폭발 범위 100% 일치 (5장 9번) — 사용자 PIE 확인 (데칼 크기 버그·실시간 갱신 수정 후 재확인, 2026-07-30). 구조적으로도 실폭발과 같은 `Propagate` 호출
- [x] 연쇄: 인접 폭탄 유발, 단계 간 `ChainStepDelay` 지연 ("촤르륵") — 로그: 4단 연쇄, 단계 간격 **78 / 70 / 74ms** (룰셋 `ChainStepDelay` 0.07초와 일치)
- [ ] 폭탄 10개 연쇄 `stat unit` 스파이크 없음 (5장 8번) — 미측정
- [x] `MaxBombCount` 초과 설치 거부 — 로그: `폭탄 설치 거부 — [Alive 1 / 슬롯 0 / Empty 1 / 폭탄없음 1]` 다수 (슬롯 0 = 개수 상한)
- [x] 폭발 후 `ActiveBombCount` 감소 → 재설치 가능 — 로그: 22:31:45~46 슬롯 0 거부 → 22:31:47 폭발 → 22:31:49 같은 자리 재설치 성공
