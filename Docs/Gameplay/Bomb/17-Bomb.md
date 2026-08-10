# ABomb

> `Gameplay/Bomb/Bomb.h/.cpp` · AActor (서버 권한)

## 역할
- 폭탄 한 발의 수명: 장전(`ServerArm`) → 킥/낙하 이동 → 기폭 → 슬롯 반환. 전부 서버
- 플레이어 막기(BlockingBox, Overlap→Block 승격)
- 클라는 `Cell`·`Range` 복제 + 데칼 표시만

## 주요 변수·함수
| 이름 | 설명 | 멀티 이유 |
|---|---|---|
| `Cell` (복제·OnRep) | 현재 셀 — 킥·낙하로 갱신 | 클라의 위험 데칼·프리뷰가 폭탄을 따라가야 함. 메시 위치는 별도(`bReplicateMovement`) — 이 둘이 갈라졌던 게 "데칼만 움직이는" 사고 |
| `Range` (복제) | 설치 순간 스탯을 굳힌 값 | 클라 프리뷰가 서버와 같은 범위를 그려야 함. 설치자 스탯을 나중에 참조하면 포션 획득 시 어긋남 |
| `FuseTimer` / `bDetonated` | 서버 퓨즈 · 중복 가드 | **비복제(불변식 3)** — 타이머는 서버만. 클라는 폭발 결과(Multicast)만 받으므로 불일치가 원천 불가 |
| `bKicking` / `bFalling` | 이동 상태 | 비복제 — 위치는 `bReplicateMovement`가 나름. 상태까지 복제하면 진실이 두 벌 |
| `BlockingBox` / `bBlocking` / `OverlappingPawns` | 막기 물리 — Overlap→Block 승격 | 비복제 — 서버·클라가 **각자 로컬 오버랩**으로 같은 결론에 도달(물리는 양쪽에 존재). 데디에서도 유지(CMC는 서버에서도 돎) |
| `ServerArm(Owner, Range, Cell)` | 장전: 값 확정+슬롯+레지스트리+퓨즈 | `FinishSpawning` **전** 호출 — Cell·Range가 첫 복제에 실려야 클라 BeginPlay 프리뷰가 맞음 |
| `ServerForceDetonate()` | 기폭 단일 진입점 | 서버 전용 — 폭발은 권한. 킥·낙하를 먼저 정지 = 폭발 원점 고정 |
| `ServerReleaseSlot()` | 슬롯 반환 (1회 가드) | 서버 — `ActiveBombCount`는 서버 스탯 |
| `ServerStartKick(Dir)` | 킥 시작 판단 | 서버 전용 — **킥은 클라 예측 금지** (어긋나면 폭발 원점→프리뷰까지 붕괴) |
| `ServerUpdateKick / ServerUpdateFall` (Tick) | 칸 단위 이동 소비 | 서버 틱 — 위치는 bReplicateMovement로, 셀은 `ServerSetCell`로 클라에 전달 |
| `CanKickInto(Cell)` / `IsSupportedAt(Cell)` | 그리드 정지·지지 판정 | 서버 판정 — 그리드 데이터라 클라도 같은 답을 낼 수 있지만 확정은 서버만 |
| `ServerSetCell(Cell)` | Cell 갱신 단일 경로 | 리슨 호스트는 OnRep이 안 오므로 여기서 직접 데칼 갱신 |
| `OnRep_Cell()` | 클라: 데칼 다시 깔기 | BeginPlay 이전엔 no-op — 최초 복제와 이중 생성 방지 |
| `PromoteToBlockingIfClear()` | 겹친 폰 0이면 Block 승격 | 양쪽에서 로컬 실행 — 승격 시점이 몇 프레임 달라도 무해(권위 판정은 그리드) |
| `BeginPlay()` (클라) | 예측 회수 + 설치음 분기 + 데칼 | **액터 복제 도착 = 서버 확정 신호** — 별도 확인 RPC가 필요 없는 이유 |

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

## 멀티 처리
**판정은 100% 서버.** 클라로 가는 것은 셋뿐 — 액터 존재(복제 스폰) · `Cell`·`Range` 값 ·
`bReplicateMovement` 위치. 타이머·킥 상태·연쇄는 아예 안 보내고, 클라는 받은 값으로
데칼·프리뷰를 로컬 재구성한다. RPC 없음(폭발 직후 Destroy라 전송 미보장 — 릴레이가 대행)

## 연결
[16-ExplosionSubsystem.md](16-ExplosionSubsystem.md) · [18-PredictedBombVisual.md](18-PredictedBombVisual.md) · 설치: [23-CA3DCharacter.md](../Character/23-CA3DCharacter.md)

## Q&A
아직 없음
