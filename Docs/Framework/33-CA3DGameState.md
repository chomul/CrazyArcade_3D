# ACA3DGameState

> `Framework/CA3DGameState.h/.cpp` · AGameStateBase — 로직 0

## 역할
- 매치 전역 복제 게시판: `Rules` · `AliveCount` · `bMatchEnded` · `MatchWinner` ·
  `MatchStartServerTime` · `bSuddenDeathActive`
- 쓰기 GameMode 단독 / 읽기 UI·봇·캐릭터·VoxelWorld

## 왜
- **왜 로직 0?** → 게시판에 로직을 넣으면 권한 경계가 흐려짐 (누가 바꿨는지 추적 불가)
- **왜 룰셋을 포인터로?** → 99개 개별 복제 대신 참조 하나. 에셋은 양쪽 디스크에 동일.
  복제의 진짜 이유 = 클라 프리뷰가 서버와 같은 값을 봐야 함
- **왜 bMatchEnded·MatchWinner가 같은 액터?** → 같은 액터 = 같은 복제 묶음 = 원자 도착.
  다른 액터면 "끝났는데 승자 모름" 프레임 (실제 무승부 오표시 버그의 수정)
- **왜 서버 시간?** → 클라 로컬 시간은 접속 시점마다 다름
- **왜 bSuddenDeathActive가 여기?** → HUD 경고 + 낙사 원인 분류가 모두 읽는 전역 상태

## 연결
쓰기: [35-CA3DGameMode.md](35-CA3DGameMode.md) · 읽기: [39-MatchWidget.md](../UI/39-MatchWidget.md) · [37-BotController.md](../AI/37-BotController.md)

## Q&A
아직 없음
