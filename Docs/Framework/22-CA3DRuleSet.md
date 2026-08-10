# UCA3DRuleSet

> `Framework/CA3DRuleSet.h/.cpp` · UPrimaryDataAsset — 튜닝 값 99개

## 역할
- 모든 튜닝 값·에셋 참조의 창고: 수치(퓨즈·속도·확률) · 클래스 참조 · 큐별 사운드+FX 쌍
- 로직 없음. 읽는 쪽이 해석

## 주요 값 (카테고리별 대표 — 총 99개)
| 카테고리(개수) | 대표 |
|---|---|
| Feedback(21) | 큐별 `~Sound`/`~FX` 쌍 · `FeedbackVolumeMultiplier` |
| Bomb(15) | `BombFuseTime=3` · `ChainStepDelay` · `BombKickSpeedCellsPerSec=8` · `BombFallSpeedCellsPerSec=12` · `BombBlockExtentCells` |
| Camera(13) | `CameraDistanceCells=12` · `CameraPitchDeg=-55` · `OcclusionFadeAmount=1.0` |
| Map(12) | `MapSizeSmall/Large` · `SmallMatchMaxPlayers=4` · `ProcDestructiblePercent=35` · `ProcRerollMaxAttempts` |
| SuddenDeath(8) | `SuddenDeathStart=150` · `DropInterval=1` · `bSuddenDeathDestroysFloor=true` · `DropExplosionRange=2` |
| Item(8) | `ItemDropPercent=30` · 가중치 5종(30/30/20/5/15) · `MaxBombCountCap` |
| Bot(8) | `BotReplanInterval=0.4` · `BotMaxPathNodes=1024` · `BotSeekItemMaxCells=6` |
| Character(6) | `MoveSpeedCellsPerSec=4` · `JumpApexCellFactor=1.4` · `JumpAirSpeedFactor=0.7` |
| Life(4) | `TrappedDuration=4` · `bPopTrappedOnContact=true` |
| 기타 | `RollerSpeedStep` · `MoveSpeedMulCap` · `PickupSpinDegreesPerSecond` · `MinPlayersForMatchEnd=2` |

## 왜
- **왜 DataAsset?** → 밸런스 조정 = 에셋 저장 (재빌드 없음). 프리셋 다중화 = 에셋 교체.
  "코드에 매직 넘버 금지"의 실체
- **왜 GameState에 포인터 복제?** → 클라 프리뷰(데칼·HUD 상한)가 서버와 같은 값을
  봐야 함. 에셋은 양쪽 디스크에 동일하니 참조만
- **왜 맵 생성 값은 전부 정수?** → float 비교의 플랫폼 오차 차단 (불변식 4)
- **왜 여기 없는 값도 있나?** → `CameraYawSnap::NumSteps`는 구조 상수 —
  런타임에 바뀌면 복제된 인덱스의 의미가 붕괴. 튜닝/구조의 경계
- **왜 계수 저장?** → "4칸/초"처럼 CellSize 파생 — 셀 크기가 바뀌어도 감각 유지

## 연결
복제 경로: [33-CA3DGameState.md](33-CA3DGameState.md) · 소비: 사실상 전부

## Q&A
아직 없음
