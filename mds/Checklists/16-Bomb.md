# Checklist 16 — Bomb

> 대응 Task: `mds/Tasks/16-Bomb.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] 퓨즈 타이머가 서버 전용 — 클라에 타이머 코드 없음 (불변식 3)
- [ ] 풀링 미사용 (스폰/디스트로이)
- [ ] `ServerPlaceBomb` 검증 4종: 개수·셀 Empty·중복 폭탄·Alive
- [ ] `bDetonated` 중복 폭발 방지
- [ ] 퓨즈·잔존 시간 룰셋 참조
- [ ] 프리뷰 데칼이 `Propagate` 재사용 — 별도 범위 계산 로직 없음
- [ ] 클라 시각 경로(BeginPlay 연출)에 데디 가드

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [ ] 설치 키 → 발밑 셀에 폭탄 → `BombFuseTime` 후 폭발
- [ ] 6방향 물줄기 FX가 풀에서 나오고 `WaterLingerTime` 후 반납
- [ ] 블록 파괴 + 렌더 갱신 + 그 위 캐릭터 낙하 (설계서 5장 7번)
- [ ] 물줄기 피격 → Trap. **제자리 점프는 피격, 다른 발판 점프는 회피** (5장 10번)
- [ ] 프리뷰 데칼과 실폭발 범위 100% 일치 (5장 9번)
- [ ] 연쇄: 인접 폭탄 유발, 단계 간 `ChainStepDelay` 지연 ("촤르륵")
- [ ] 폭탄 10개 연쇄 `stat unit` 스파이크 없음 (5장 8번)
- [ ] `MaxBombCount` 초과 설치 거부
- [ ] 폭발 후 `ActiveBombCount` 감소 → 재설치 가능
