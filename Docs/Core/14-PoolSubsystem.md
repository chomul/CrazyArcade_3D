# UPoolSubsystem

> `Core/PoolSubsystem.h/.cpp` · UWorldSubsystem

## 역할
- 클라 시각 액터의 대여·반납·예열: `Acquire / Release / Prewarm` + 템플릿 `Acquire<T>`
- 잠든 액터 GC 보호 · 계약 위반(미구현·이중 반납) ensure

## 주요 변수·함수
| 이름 | 설명 |
|---|---|
| `Free` (TMap→FPooledActorArray) | 클래스별 프리 리스트. UPROPERTY 두 겹 = GC 보호 |
| `Prewarm(Class, Count)` | 미리 스폰 → 즉시 Release 상태로 보관 |
| `Acquire(Class, Transform)` | 프리 리스트 Pop(없으면 스폰) → 복원(Release 역순) → 콜백 |
| `Acquire<T>(...)` | 캐스트 래퍼 |
| `Release(Actor)` | 이중 반납 검사 → 콜백 → 숨김·비활성 → 보관 |
| `DeactivateAndStore` (내부) | **콜백 먼저** → Hidden/Collision/Tick off → 실제 클래스로 저장 |

## 왜
- **왜 풀링?** → 물줄기·데칼이 폭발마다 수십 개 생멸. 스폰/파괴+GC 비용을 재사용으로
- **왜 클라 시각만?** → 서버 권한 액터(ABomb)는 재사용 시 상태 오염 위험 > 이득
- **GC 보호가 왜 UPROPERTY 두 겹?** → 잠든 액터는 무참조라 GC 대상.
  중첩 컨테이너 UPROPERTY 금지라 TArray를 USTRUCT로 감쌈
- **"풀 안 = 항상 정리 완료" 규약** → Prewarm도 즉시 Release 상태로 보관.
  복원은 Release의 정확한 역순 — 대칭 깨지면 반쯤 잠든 액터
- **왜 콜백을 비활성화보다 먼저?** → 뒤집으면 숨겨진 액터의 타이머가 계속 돎
- **왜 실제 클래스로 보관?** → 파생 인스턴스가 베이스 요청에 섞이지 않게
- **Free 맵 순회 금지** → TMap 순회는 비결정 (프로젝트 공통 규율)

## 연결
계약: [13-PooledActor.md](13-PooledActor.md)

## Q&A
아직 없음
