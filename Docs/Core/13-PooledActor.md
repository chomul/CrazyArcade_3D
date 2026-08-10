# IPooledActor

> `Core/PooledActor.h` · UINTERFACE (BP 구현 차단)

## 역할
- 풀 수명 콜백 계약: `OnAcquiredFromPool` / `OnReleasedToPool`
- 책임 분담선: 위치·표시·컬리전·틱 복원 = 풀 / 타이머·FX·내부 상태 정리 = 액터

## 주요 함수
| 이름 | 설명 |
|---|---|
| `OnAcquiredFromPool()` | 대여 직후 — 위치·표시·컬리전·틱은 풀이 이미 복원했다는 전제로 자기 상태 초기화 |
| `OnReleasedToPool()` | 반납 직전 — **타이머·FX·사운드 정지 필수** |

## 왜
- **왜 필요?** → 풀은 액터를 파괴하지 않고 재사용. "다시 태어남/잠듦" 시점에
  액터 스스로 정리할 훅이 필요 (풀은 액터 내부를 모름)
- **왜 BP 구현 차단?** → `Cast<IPooledActor>`가 네이티브만 찾게 고정.
  BP 구현 허용 시 "구현했는데 호출 안 됨" 미궁
- **미구현 시?** → Acquire/Release 양쪽에서 ensure — 계약 위반을 즉시 발견

## 연결
[14-PoolSubsystem.md](14-PoolSubsystem.md) · 구현체: WaterSegment·DangerDecal·PredictedBombVisual·DropMarker

## Q&A
아직 없음
