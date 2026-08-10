# AItemPickup

> `Source/CrazyArcade3D/Gameplay/Item/ItemPickup.h/.cpp` · AActor (`bReplicates`, `bAlwaysRelevant`)

맵에 노출된 아이템 1개. 획득(`OnOverlap` → `ServerApplyItem`)과 물줄기 소멸(`ServerBurn`)의 두 퇴장 경로.

## 왜 이렇게 했는가

- **컬리전(Sphere)과 표시(Mesh)를 분리 — HISM 사건의 재발 방지책** — 데디 서버는 메시를
  파괴하지만 판정 스피어는 살려 둔다. "시각 전용으로 보이는 것에 판정이 얹혀 있는" 사고
  (HISM=컬리전)를 겪은 뒤, 새 액터는 처음부터 판정과 표시를 분리해 설계했다.
  실증: 데디 실전에서 획득 정상 동작.
- **획득 판정이 서버에만 있다** — `BeginPlay`에서 `HasAuthority()`일 때만 오버랩 바인딩.
  클라 판정은 없다 — 아이템 획득은 예측할 필요가 없는 저빈도 사건이고,
  잘못 예측하면 스탯이 두 번 오른 것처럼 보인다.
- **갇힘 중 획득 불가(`LifeState != Alive` 거부)** — 사용자 확정 규칙. 판정 위치가
  아이템 쪽인 이유: 캐릭터가 "먹을 수 있나"를 들면 아이템 종류마다 캐릭터가 커진다.
- **`bConsumed` 가드** — 같은 프레임에 두 명이 겹치면 오버랩이 두 번 온다.
  첫 획득만 유효, 이후는 무시.
- **소멸(`ServerBurn`)과 획득이 다른 함수인 이유** — 물줄기에 탄 아이템은 효과 없이
  사라진다(원작의 심리전 — 아이템을 지키려고 폭발을 막는 판단). 획득과 경로를 분리해야
  로그·집계에서 구분된다.
- **획득 큐가 `ACA3DCueRelay` 경유인 이유** — 획득 직후 `Destroy()`라 자기 RPC 전송이
  보장되지 않는다(`ABomb`과 같은 사정).
- **`OnRep_Type`과 BeginPlay가 같은 `RefreshVisual`을 탄다** — 복제 도착 순서와 무관하게
  표시가 같은 코드로 수렴.

## 네트워크 표면
복제: `Type`(OnRep) · `Cell`. RPC 없음.

## 연결
- 스폰: `ProcessStepItems`([16-ExplosionSubsystem.md](../Bomb/16-ExplosionSubsystem.md)) ·
  효과: [24-StatusComponent.md](../Character/24-StatusComponent.md) · 봇 목표: [37-BotController.md](../../AI/37-BotController.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
