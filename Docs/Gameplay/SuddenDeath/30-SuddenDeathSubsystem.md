# USuddenDeathSubsystem (+ Relay · DropMarker)

> `Gameplay/SuddenDeath/SuddenDeathSubsystem.h/.cpp` · 한 파일 3클래스 (1 Task=1 Class 예외)

## 역할
- **Subsystem**: 서버 낙하 스케줄러 — 주기 추첨 → 예고 방송 → 지연 후 폭발 적용 위임
- **Relay**(AInfo): 예고 Multicast 스피커 / **DropMarker**: 예고 마커 표시(풀링)

## 주요 변수·함수
| 이름 | 설명 |
|---|---|
| `bRunning` / `DropTimer` / `Stream` | 실행 플래그 · 주기 타이머 · 서버 난수 |
| `PendingWaves` (배열) | 예고 중 웨이브들 — `FSuddenDeathWave{Id, Cells, Timer}` |
| `ServerStart() / ServerStop()` | GameMode가 부르는 스위치 (정지 시 웨이브 전부 해제) |
| `ProcessDrop()` (주기) | 셀 추첨 → 웨이브 확정 → 예고 방송 → 실행 타이머 |
| `ExecuteWave(Id)` | 웨이브 셀마다 `ServerApplyExplosionAt(...)` |
| `PickDropCell(...)` (static) | 낙하 후보 추첨 (외곽 가중치·시도 상한) |
| `Relay::MulticastWarnDrop(Cells, Delay)` | 예고: 경고 큐 → 마커 풀 획득 + StartWarning |
| `DropMarker::StartWarning(Seconds)` | 마커 표시 후 자체 반납 |

## 왜
- **⭐ Propagate 무수정** → `bFloorDestructible`이 이미 인자(불변식 2의 회수).
  서든데스는 자기 룰셋 값만 넘김 — "낙하만 바닥을 부순다"
- **왜 폭발 적용을 폭탄과 공유?** → `ServerApplyExplosionAt` → 같은 `ApplyExplosionCells`.
  따로면 "서든데스로 부순 블록만 다르게 동작". 연쇄 유발은 적용 **후** (순서 뒤집으면
  안 부서진 그리드를 읽음)
- **왜 웨이브 배열?** → 예고(1.5s) > 간격(1.0s)이라 예고 중 웨이브가 동시 다수.
  핸들 하나면 뒤가 앞을 덮어 앞 웨이브가 영영 안 떨어짐
- **왜 셀을 예고 시점에 확정?** → 만료 시 재추첨 = 마커와 실제 어긋남 = "보고 피한다" 붕괴
- **왜 FMath::Rand 허용?** → 서버 단독 결정 + Multicast 통지. 클라 재현 불필요
- **왜 Relay 분리?** → 서브시스템은 RPC 불가. 경고음이 마커 확인보다 위 —
  마커 에셋 없어도 소리는 나야 함
- **왜 정지 시 웨이브 전부 해제?** → 결과 화면 뒤에 블록이 떨어지면 안 됨
- **왜 마커 타입 고정?** → 순수 AActor면 풀 Acquire가 ensure로 낙하마다 터짐
- **낙사 원인은 시각으로 판정** → 그리드에 파괴 원인을 넣으면 Voxel 독립성 붕괴.
  통계에 그 비용을 안 치름

## 연결
[16-ExplosionSubsystem.md](../Bomb/16-ExplosionSubsystem.md) · 스위치: [35-CA3DGameMode.md](../../Framework/35-CA3DGameMode.md)

## Q&A
아직 없음
