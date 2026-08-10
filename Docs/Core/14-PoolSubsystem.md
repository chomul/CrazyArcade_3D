# UPoolSubsystem

> `Source/CrazyArcade3D/Core/PoolSubsystem.h/.cpp` · UWorldSubsystem

제네릭 액터 풀. `Prewarm(Class, Count)` / `Acquire(Class, Transform)` / `Release(Actor)` +
템플릿 `Acquire<T>`.

## 역할

- 클라 시각 액터의 **대여·반납·예열**을 관리한다: 클래스별 프리 리스트에서 꺼내 주고
  (`Acquire`), 반납받아 잠재우고(`Release`), 미리 만들어 둔다(`Prewarm`).
- 잠든 액터를 GC로부터 보호한다(UPROPERTY 참조 유지).
- 계약 위반(인터페이스 미구현·이중 반납)을 ensure로 조기에 드러낸다.

## 왜 이렇게 했는가

- **왜 풀링인가** — 물줄기·데칼·예측 폭탄·마커는 폭발마다 수십 개가 생겼다 사라진다.
  스폰/파괴 비용(+GC 압력)을 프리 리스트 재사용으로 바꾼다. 설계 결정 7번:
  "제네릭 풀 하나로 통일 API" — 대상마다 전용 풀을 만들지 않는다.
- **풀링은 클라 시각 요소만 (프로젝트 규칙)** — 서버 권한 상태를 가진 액터(`ABomb`)는
  풀링하지 않는다. 재사용 시 상태 오염(옛 퓨즈·옛 슬롯) 위험이 스폰 비용 절감보다 크다.
- **GC 보호가 UPROPERTY 두 겹인 이유** — 풀에 잠든 액터는 아무도 참조하지 않으므로
  GC가 수거해 간다. `TMap<TObjectPtr<UClass>, FPooledActorArray>`를 UPROPERTY로 —
  UE는 중첩 컨테이너 UPROPERTY를 금지하므로 TArray를 USTRUCT(`FPooledActorArray`)로
  감싸고 그 안의 배열도 UPROPERTY.
- **"풀 안에 있는 동안은 항상 정리 완료" 규약** — Prewarm도 스폰 직후 즉시
  `DeactivateAndStore`(Release 상태로 보관). 꺼낼 때의 복원 순서는 Release의 정확한 역순
  (Transform → Tick on → Collision on → Hidden off). 대칭이 깨지면 "풀에서 나온 액터가
  반쯤 잠든 상태"가 된다.
- **콜백을 비활성화보다 먼저** — `DeactivateAndStore`가 `OnReleasedToPool`(타이머·FX 정지)을
  먼저 부르고 숨긴다. 순서를 뒤집으면 숨겨진 액터의 타이머가 계속 돈다.
- **이중 반납 방어** — `Release`가 이미 풀에 있는 액터면 무시(`Contains` 검사).
  이중 반납은 같은 액터가 두 번 대여되는 최악의 오염으로 이어진다.
- **실제 클래스로 보관** — `FindOrAdd(Actor->GetClass())` — 파생 클래스 인스턴스가
  베이스 클래스 요청에 섞여 나가지 않는다.
- **Free 맵은 조회만, 순회 금지** — TMap 순회 순서는 비결정적. 이 프로젝트의 컨테이너
  규율이 풀에도 일관 적용된다.

## 연결
- 계약: [13-PooledActor.md](13-PooledActor.md) · 사용자: WaterSegment·DangerDecal·
  PredictedBombVisual·SuddenDeathDropMarker

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
