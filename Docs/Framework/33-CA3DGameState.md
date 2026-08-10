# ACA3DGameState

> `Source/CrazyArcade3D/Framework/CA3DGameState.h/.cpp` · AGameStateBase

클라에 복제되는 매치 상태. **로직 0** — 복제 필드 6개와 `GetLifetimeReplicatedProps`뿐.
`Rules` · `AliveCount` · `bMatchEnded` · `MatchWinner` · `MatchStartServerTime` · `bSuddenDeathActive`.

## 왜 이렇게 했는가

- **왜 로직이 0인가** — 이 클래스는 "서버 결정의 게시판"이다. 쓰는 쪽은 GameMode 하나,
  읽는 쪽은 UI·봇·캐릭터 등 여럿. 게시판에 로직을 넣으면 권한 경계가 흐려진다
  (누가 어디서 값을 바꿨는지 추적 불가).
- **룰셋을 에셋 "포인터"로 복제하는 이유** — 튜닝 값 99개를 개별 복제하지 않는다.
  DataAsset은 양쪽 디스크에 같은 내용이 있으므로 참조만 복제하면 된다.
  클라 프리뷰(위험 데칼 등)가 서버와 같은 룰셋을 봐야 하는 것이 복제의 진짜 이유.
- **`bMatchEnded`와 `MatchWinner`가 같은 액터에 있는 이유** — 한 액터의 프로퍼티들은
  같은 복제 묶음으로 도착한다. 종료 플래그와 승자가 다른 액터에 있으면 클라가
  "끝났는데 승자를 모르는" 프레임이 생긴다(실제 무승부 오표시 버그의 수정).
- **`MatchStartServerTime`이 서버 시간인 이유** — 클라 로컬 시간으로 경과를 재면
  접속 시점마다 다르다. `GetServerWorldTimeSeconds()` 기준이라 모든 클라의 타이머가 같다.
- **`bSuddenDeathActive`가 여기 있는 이유** — HUD 경고(UI)와 낙사 원인 분류(캐릭터)가
  모두 읽는 전역 매치 상태다. 서브시스템 내부 플래그면 클라가 못 읽는다.

## 연결
- 쓰기: [35-CA3DGameMode.md](35-CA3DGameMode.md) 단독 · 읽기: [39-MatchWidget.md](../UI/39-MatchWidget.md) ·
  [37-BotController.md](../AI/37-BotController.md) · [03-VoxelWorld.md](../Voxel/03-VoxelWorld.md)(Rules 대기)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
