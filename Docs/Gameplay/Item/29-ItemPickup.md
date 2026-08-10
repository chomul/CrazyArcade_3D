# AItemPickup

> `Gameplay/Item/ItemPickup.h/.cpp` · AActor · bReplicates · bAlwaysRelevant

## 역할
- 노출된 아이템 1개: 표시(메시·회전) + 획득 판정(스피어)
- 퇴장 경로 둘: 획득(→`ServerApplyItem`→Destroy) / 물줄기 소멸(`ServerBurn` — 효과 없음)
- 레지스트리 등록 — 봇이 셀로 검색

## 주요 변수·함수
| 이름 | 설명 | 멀티 이유 |
|---|---|---|
| `Type` (복제·OnRep) / `Cell` (복제) | 종류·위치 | `bAlwaysRelevant` — 늦게 접속해도 보여야 함. OnRep_Type → 표시 갱신 |
| `bConsumed` (서버) | 중복 획득 가드 | 비복제 — 획득 확정은 서버 프레임 안에서 끝남 |
| `PickupSphere` | 획득 판정 | **데디에서도 유지** — 판정은 서버의 일 (HISM 사건 재발 방지) |
| `Mesh` | 표시 | 데디에서 파괴 — 서버에 그림은 불필요 |
| `ServerInit(Type, Cell)` | 스폰 직후 값 확정 | Deferred 스폰 — 첫 복제에 값이 실리게 (ABomb과 같은 이유) |
| `OnOverlap` (서버만 바인딩) | Alive 검사 → 적용 → 큐 → Destroy | 획득은 저빈도라 클라 예측 불필요 — 잘못 예측하면 스탯이 두 번 오른 듯 보임 |
| `ServerBurn()` | 효과 없이 Destroy | 서버 전용 — 소멸도 액터 파괴 복제로 클라에 전달 |
| `RefreshVisual()` | BeginPlay·OnRep_Type 공용 | 복제 도착 순서와 무관하게 표시가 같은 코드로 수렴 |
| `ApplyCellScale()` | 크기를 CellSize에서 파생 | |

## 왜
- **왜 컬리전·표시 분리?** → HISM 사건 재발 방지. 데디는 메시만 파괴, 판정 스피어 유지
  → 데디에서도 획득 동작 (실증됨)
- **왜 획득 판정이 서버만?** → 저빈도라 예측 불필요. 잘못 예측하면 스탯이 두 번 오른 듯 보임
- **왜 갇힘 중 획득 불가 판정이 아이템 쪽?** → 캐릭터가 "먹을 수 있나"를 들면
  아이템 종류마다 캐릭터가 커짐
- **왜 bConsumed 가드?** → 같은 프레임 두 명 겹침 = 오버랩 2회. 첫 획득만 유효
- **왜 소멸≠획득 함수?** → 원작 심리전(아이템을 물줄기에서 지키기). 로그·집계 구분
- **왜 큐가 릴레이 경유?** → 획득 직후 Destroy — 자기 RPC 전송 미보장 (ABomb과 동일)
- **왜 OnRep_Type·BeginPlay가 같은 RefreshVisual?** → 복제 도착 순서 무관하게 표시 수렴

## 멀티 처리
**존재·종류는 복제로, 획득 판정은 서버만.** 클라는 복제 액터를 보고 그리기만 하고,
획득하면 서버가 스탯 적용 후 Destroy — 액터 소멸 복제가 곧 "사라짐" 전달이다.
획득 큐만 `ACA3DCueRelay` 경유(Destroy 직후라 자기 RPC 미보장). RPC 없음

## 연결
스폰: [16-ExplosionSubsystem.md](../Bomb/16-ExplosionSubsystem.md) · 효과: [24-StatusComponent.md](../Character/24-StatusComponent.md) · 봇: [37-BotController.md](../../AI/37-BotController.md)

## Q&A
아직 없음
