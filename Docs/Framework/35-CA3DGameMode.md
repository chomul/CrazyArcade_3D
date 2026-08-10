# ACA3DGameMode

> `Framework/CA3DGameMode.h/.cpp` · AGameModeBase — 서버 전용

## 역할
- 매치 시작: 시드 결정 → VoxelWorld 초기화(크기 티어) → 스폰 게이트 → 스폰 셀 배정
- 참가 관리: `RegisterParticipant`(사람·봇 공용) · 봇 채우기 · 중도 이탈
- 승패: `NotifyPlayerDeath` 수집 → 다음 틱 `ResolvePendingDeaths` → GameState 기록
- 서든데스 시작/정지 스위치

## 왜
- **왜 매치 로직이 여기?** → GameMode는 클라에 없음 — 권한 분리가 공짜
- **왜 스폰 게이트?** → 엔진이 PostLogin을 World::BeginPlay **앞에서** 부름.
  맵은 BeginPlay에서 생성 → 첫 입장자는 지형 없음 → 원점 낙사.
  해소는 StartPlay의 Super **직후** (BeginPlay 안이면 폰 BeginPlay 누락 가능)
- **왜 StartSpot 오버라이드 2개?** → 오염된 원점이 `StartSpot`에 굳음 —
  `UpdatePlayerStartSpot`·`ShouldSpawnAtStartSpot` 둘 다 막아야 함.
  ⚠️ PIE는 APlayerStartPIE 덕에 면역 — 데모 빌드에서만 터졌던 결함
- **왜 RegisterParticipant 단일 경로?** → 색·참가 수·AliveCount가 한 함수.
  봇도 PlayerState를 가져 판정이 사람·봇 무구분
- **왜 사망을 다음 틱 배칭?** → 동시 사망 = 공동 등수. 즉시 처리면 도착 순서(우연)가
  등수를 가름. 공식 `Max(2, AliveBefore-K+1)` — **하한 2 = 우승 자리 비워두기(무승부 장치)**
- **왜 승패를 GameState에?** → bMatchEnded+MatchWinner 원자 복제 (다른 액터면 순서 미보장)
- **왜 이탈 = 사망 재사용?** → 폰 있으면 `ServerKill(Left)`, 없으면 통지 직접
  (안 하면 AliveCount가 안 줄어 매치가 안 끝남). 종료 후 이탈은 탈주 표시 안 함
- **왜 여기만 FMath::Rand 허용?** → 결정론이 필요한 건 "시드→맵"이지 시드 생성이 아님

## 연결
[03-VoxelWorld.md](../Voxel/03-VoxelWorld.md) · [33-CA3DGameState.md](33-CA3DGameState.md) · [34-CA3DPlayerState.md](34-CA3DPlayerState.md) · 통지자: [24-StatusComponent.md](../Gameplay/Character/24-StatusComponent.md)

## Q&A
아직 없음
