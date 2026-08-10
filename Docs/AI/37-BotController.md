# ABotController

> `AI/BotController.h/.cpp` · AAIController — 서버 전용 FSM 5상태

## 역할
- 매 틱: 상황 인식(위험·목표) → 상태 결정(`Replan`) → 경로(BFS) → 캐릭터 조작(`FollowPath`)
- 우선순위: **Evade > PopTrapped > 설치 > SeekItem > Attack > Wander**
- 관전용 카메라 각(`CamYawIndex`)을 이동 방향에서 산출
- 판정 로직 소유 안 함 — 전부 기존 함수 조회

## 왜
- **왜 BT가 아니라 FSM?** → 3주 일정, 상태 5개 규모. C++의 디버깅·테스트·결정론이 이김
- **왜 판정 재사용?** → 위험=`Propagate`(폭탄이 든 Range로) · 이동=`VoxelMove`(검증기와
  동일) · 아이템 가치=`HasRoomForItem` · 행동=사람과 같은 진입점.
  봇 전용 판정 = 검증 두 벌
- **우선순위 근거** → PopTrapped>설치: 갇힌 상대에게 폭탄 무효, 접촉은 확정 킬 /
  SeekItem<설치: 폭탄 놓기가 곧 아이템 생성 / SeekItem>Attack: Attack은 쿨다운
  없어 아래 두면 영영 미발동. 실측: SeekItem 추가 후 파괴 61→78 (배치 검증)
- **왜 설치에 탈출로 BFS 필수?** → 없으면 자폭 반복
- **왜 위험 집합 하나 공유?** → 셀마다 Propagate 재실행 = 비용 곱 + 경로 갈라짐
- **왜 bPlanFailed?** → 실패→매 틱 재계획 폭주 차단 (전원 갇힌 상황이 최악)
- **BFS 결정론** → 방문 TMap 조회 전용 · `PlanarDirs` 고정 순서 · 난수는 ColorIndex 시드
- **왜 FollowPath에 경로 폐기 가드?** → 발판이 폭발로 사라지면 옛 코드는 벽에 붙어 점프
- **왜 MaxPathNodes 1024?** → 512는 잘림≠실패 구분 불가 — 먼 상대를 도달 불가로 오판
- **왜 CamYawIndex에 데디 가드 없음?** → 복제 값 = 상태이지 시각이 아님

## 연결
[16-ExplosionSubsystem.md](../Gameplay/Bomb/16-ExplosionSubsystem.md) · [06-VoxelMovement.md](../Voxel/06-VoxelMovement.md) · [23-CA3DCharacter.md](../Gameplay/Character/23-CA3DCharacter.md) · [24-StatusComponent.md](../Gameplay/Character/24-StatusComponent.md)

## Q&A
아직 없음
