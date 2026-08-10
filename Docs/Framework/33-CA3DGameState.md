# ACA3DGameState

> `Framework/CA3DGameState.h/.cpp` · AGameStateBase — 로직 0

## 역할
- 매치 전역 복제 게시판: `Rules` · `AliveCount` · `bMatchEnded` · `MatchWinner` ·
  `MatchStartServerTime` · `bSuddenDeathActive`
- 쓰기 GameMode 단독 / 읽기 UI·봇·캐릭터·VoxelWorld

## 주요 변수
| 이름 | 설명 | 멀티 이유 |
|---|---|---|
| `Rules` (복제) | 룰셋 에셋 포인터 | 클라 프리뷰·HUD가 서버와 같은 튜닝 값을 봐야 함. 99개 대신 **참조 하나**만(에셋은 양쪽 디스크에 동일) |
| `AliveCount` (복제) | 생존 수 | HUD가 매 틱 읽음 — 개별 PlayerState를 세는 것보다 서버가 확정한 값 하나가 정확 |
| `bMatchEnded` (복제) | 매치 종료 스위치 | 결과 화면·가드들의 기준. `MatchWinner`와 **같은 액터** = 원자 도착 |
| `MatchWinner` (복제) | 우승자. **nullptr = 무승부** | 클라가 행 스캔으로 우승자를 추론하면 복제 타이밍에 취약 — 서버가 확정한 포인터 하나로 |
| `MatchStartServerTime` (복제) | 시작 시각 | **서버 시간** 기준 — 클라 로컬 시간은 접속 시점마다 달라 타이머가 갈라짐 |
| `bSuddenDeathActive` (복제) | 서든데스 중 | HUD 경고 + 낙사 사인 분기 — 서브시스템 내부 플래그면 클라가 못 읽음 |

## 왜
- **왜 로직 0?** → 게시판에 로직을 넣으면 권한 경계가 흐려짐 (누가 바꿨는지 추적 불가)
- **왜 룰셋을 포인터로?** → 99개 개별 복제 대신 참조 하나. 에셋은 양쪽 디스크에 동일.
  복제의 진짜 이유 = 클라 프리뷰가 서버와 같은 값을 봐야 함
- **왜 bMatchEnded·MatchWinner가 같은 액터?** → 같은 액터 = 같은 복제 묶음 = 원자 도착.
  다른 액터면 "끝났는데 승자 모름" 프레임 (실제 무승부 오표시 버그의 수정)
- **왜 서버 시간?** → 클라 로컬 시간은 접속 시점마다 다름
- **왜 bSuddenDeathActive가 여기?** → HUD 경고 + 낙사 원인 분류가 모두 읽는 전역 상태

## 멀티 처리
**서버(GameMode)가 쓰고 전 클라가 읽는 단방향 게시판.** RPC 없음, OnRep도 없음 —
읽는 쪽(UI)이 폴링하므로 도착 알림이 필요 없다. GameState는 엔진이 모든 클라에
자동 복제하는 액터라 "전역 매치 상태"의 표준 자리.

## 연결
쓰기: [35-CA3DGameMode.md](35-CA3DGameMode.md) · 읽기: [39-MatchWidget.md](../UI/39-MatchWidget.md) · [37-BotController.md](../AI/37-BotController.md)

## Q&A
아직 없음
