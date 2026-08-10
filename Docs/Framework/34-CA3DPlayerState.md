# ACA3DPlayerState

> `Framework/CA3DPlayerState.h/.cpp` · APlayerState — 로직 0

## 역할
- 참가자 1명의 복제 게시판: `ColorIndex` · `FinalRank` · `bAlive` · `CamYawIndex` · `bLeftMatch`
- 폰이 죽어도 매치 끝까지 남음 — 결과 화면·관전의 데이터 출처

## 주요 변수·함수
| 이름 | 설명 | 멀티 이유 |
|---|---|---|
| `ColorIndex` (복제) | 참가 순서 색 배정 | 모든 클라가 서로의 색을 그려야 함. 늦은 접속자도 복제로 받음 |
| `FinalRank` (복제) | 최종 순위. **0 = 아직 생존** | 결과 화면을 각 클라가 로컬로 그림 — 순위 데이터가 클라에 있어야 함 |
| `bAlive` (복제) | 판정상 생존 | 관전 후보 선정이 **클라 로컬**이라(추가 RPC 없는 관전의 재료) 복제 필수 |
| `CamYawIndex` (복제, uint8) | 카메라 스냅 인덱스 | 컨트롤러 yaw는 복제 안 되는 값 — 관전 각의 유일한 대체 통로. 각도 대신 인덱스 1바이트 |
| `bLeftMatch` (복제) | 중도 이탈 | 결과 화면 "(탈주)" 표시 — 클라가 그리므로 복제 |
| `OnDeactivated()` (override) | Super 안 부름 = 파괴 안 함 | 엔진 기본은 접속 종료 시 파괴 → 탈주자가 결과에서 사라짐. 남겨야 전 클라 결과가 완전 |

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

## 멀티 처리
**참가자별 단방향 게시판.** 쓰기는 서버(판정·CamYaw RPC 수신), 읽기는 전 클라.
폰과 달리 모든 클라에 항상 복제되고 접속 종료에도 남아, "결과 화면·관전처럼
폰 없이도 알아야 하는 것"의 자리가 된다.

## 연결
쓰기: [35-CA3DGameMode.md](35-CA3DGameMode.md) · [26-CA3DPlayerController.md](../Gameplay/Character/26-CA3DPlayerController.md) · 읽기: [39-MatchWidget.md](../UI/39-MatchWidget.md)

## Q&A
아직 없음
