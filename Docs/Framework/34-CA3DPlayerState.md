# ACA3DPlayerState

> `Framework/CA3DPlayerState.h/.cpp` · APlayerState — 로직 0

## 역할
- 참가자 1명의 복제 게시판: `ColorIndex` · `FinalRank` · `bAlive` · `CamYawIndex` · `bLeftMatch`
- 폰이 죽어도 매치 끝까지 남음 — 결과 화면·관전의 데이터 출처

## 왜
- **왜 폰이 아니라 PlayerState?** → 순위·색·생존은 폰 수명보다 길어야 함
- **왜 OnDeactivated에서 Super 안 부름?** → 엔진 기본 = 접속 종료 시 파괴.
  파괴되면 결과 화면에서 탈주자가 사라짐 — 남겨서 `bLeftMatch` 표시
- **왜 봇도 가짐?(`bWantsPlayerState`)** → 승패 판정이 PlayerArray만 보면 됨 —
  사람·봇 구분 불필요
- **왜 FinalRank 쓰기가 한 곳?** → 순위는 판정(`ResolvePendingDeaths`)의 출력.
  0 = 생존(미확정) 겸용
- **왜 CamYawIndex가 여기?** → 컨트롤러 yaw는 비복제인데 관전자가 대상 각을 알아야 함.
  폰 리스폰과 무관한 표현 값이라 이 자리. uint8 1바이트만
- **왜 bAlive가 LifeState와 별도?** → 승패·관전은 "판정상 생존" 하나만 필요 —
  폰 없이도 읽히는 자리에 최소 형태

## 연결
쓰기: [35-CA3DGameMode.md](35-CA3DGameMode.md) · [26-CA3DPlayerController.md](../Gameplay/Character/26-CA3DPlayerController.md) · 읽기: [39-MatchWidget.md](../UI/39-MatchWidget.md)

## Q&A
아직 없음
