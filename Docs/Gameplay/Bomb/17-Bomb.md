# ABomb

> `Gameplay/Bomb/Bomb.h/.cpp` · AActor (서버 권한)

## 역할
- 폭탄 한 발의 수명: 장전(`ServerArm`) → 킥/낙하 이동 → 기폭 → 슬롯 반환. 전부 서버
- 플레이어 막기(BlockingBox, Overlap→Block 승격)
- 클라는 `Cell`·`Range` 복제 + 데칼 표시만

## 주요 변수·함수
| 이름 | 설명 |
|---|---|
| `Cell` (복제·OnRep) | 현재 셀 — 킥·낙하로 갱신. 데칼이 따라옴 |
| `Range` (복제) | 폭발 범위 — 설치 순간 스탯을 굳힌 값 |
| `FuseTimer` / `bDetonated` | 서버 퓨즈 · 중복 폭발 가드 (비복제) |
| `bKicking` / `bFalling` | 이동 상태 (비복제 — 위치는 bReplicateMovement로) |
| `BlockingBox` / `bBlocking` / `OverlappingPawns` | 막기 물리 — Overlap→Block 승격 재료 |
| `ServerArm(Owner, Range, Cell)` | 장전: 값 확정 + 슬롯 차지 + 레지스트리 + 퓨즈 시작 |
| `ServerForceDetonate()` | 기폭 단일 진입점 — 이동 정지 후 서브시스템에 위임 |
| `ServerReleaseSlot()` | 설치자 슬롯 반환 (1회 가드) |
| `ServerStartKick(Dir)` | 킥 시작 판단 — 진행 중·막힘이면 무시 |
| `ServerUpdateKick / ServerUpdateFall` (Tick) | 칸 단위 이동 소비 — 낙하 판정이 정지 판정보다 먼저 |
| `CanKickInto(Cell)` | 그리드 정지 판정: 벽·블록·다른 폭탄·플레이어(시체 제외) |
| `IsSupportedAt(Cell)` | 발밑 솔리드 여부 (미확보 시 true = 안전측) |
| `ServerSetCell(Cell)` | Cell 갱신 단일 경로 — 리슨 호스트 데칼 갱신 포함 |
| `OnRep_Cell()` | 클라: 위험 데칼 다시 깔기 |
| `PromoteToBlockingIfClear()` | 겹친 폰 0이면 Block 승격 |
| `BeginPlay()` (클라) | 예측 회수(`ReleasePredictedVisualAt`) + 설치음 분기 + 데칼 |

## 왜
- **왜 per 액터?** → 최대 ~40개. 킥(비정수 위치)·개별 퓨즈·복제가 자연스러움 (sparse=액터)
- **왜 풀링 안 함?** → 서버 권한 상태 재사용 = 오염 위험 > 이득
- **왜 RPC 없음?** → 폭발 직후 Destroy라 전송 미보장. FX·큐는 릴레이가 대신
- **왜 SpawnActorDeferred+ServerArm?** → Cell·Range가 첫 복제에 실려야 클라 프리뷰가 맞음
- **왜 Overlap→Block 승격?** → 처음부터 Block이면 설치자가 자기 폭탄에 갇힘 (원작 규칙).
  초기 겹침은 `GetOverlappingActors`로 직접 수집. **데디에서도 유지** (막힘은 물리)
- **왜 킥에 클라 예측 없음?** → 예측 어긋남이 폭발 원점→프리뷰까지 무너뜨림
- **왜 정지 판정이 그리드?** → "멈추는 칸"과 "터지는 칸"이 같은 데이터.
  플레이어만 `GetFootCell()` 비교 (시체 제외)
- **왜 낙하 판정이 정지보다 먼저?** → 뒤집으면 "마지막 칸이 절벽이면 공중에 섬"
- **왜 낙하 시 수평 절단?** → 포물선 = 칸 정렬 붕괴 = 폭발 원점 영구 어긋남
- **⚠️ bReplicateMovement 명시적 켬** → AActor 기본 false. 안 켜면 데칼만 움직이고
  메시 제자리 (실제 사고 — 플래그 자체를 검사하는 회귀 테스트 있음)
- **왜 맵 밖 소멸 시 슬롯 직접 반환?** → EndPlay 안전망 의존 금지 —
  안전망이 진짜 이상을 덮음

## 네트워크
복제: `Cell`(OnRep — 데칼 추종) · `Range`. RPC 없음

## 연결
[16-ExplosionSubsystem.md](16-ExplosionSubsystem.md) · [18-PredictedBombVisual.md](18-PredictedBombVisual.md) · 설치: [23-CA3DCharacter.md](../Character/23-CA3DCharacter.md)

## Q&A
아직 없음
