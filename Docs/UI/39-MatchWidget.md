# UMatchWidget

> `UI/MatchWidget.h/.cpp` · UUserWidget (Abstract) — 레이아웃은 WBP, 내용은 C++

## 역할
- HUD 3요소: 생존·시간 / 내 스탯 / 서든데스 경고 + 결과 화면(순위·공동 등수·무승부·탈주)
- 방식: 매 틱 폴링 → 스냅샷 비교 → static 순수 함수로 가공 → 변한 것만 갱신

## 왜
- **왜 폴링?(OnRep 아님)** → UI가 OnRep을 잡으면 Framework→UI 역방향 의존 (규칙 위반)
- **왜 스냅샷 비교?** → 값 같으면 문자열 재생성 안 함. float은 `IsNearlyEqual`(복제 오차 대응)
- **왜 static 순수 함수?** → 위젯 인스턴스 없이 테스트. 폴백과 공유
- **왜 결과 화면 매 틱 재확인?** → bMatchEnded(GameState)와 FinalRank(PlayerState)는
  다른 액터 = 도착 순서 미보장. 한 번만 그리면 "순위 0" 프레임이 굳음 (실제 버그)
- **왜 BindWidgetOptional?** → 필수(BindWidget)면 이름 다 맞을 때까지 WBP 컴파일 불가.
  대신 미바인딩 경고 1줄 — "경고 0 = 전부 연결"이 검증법
- **왜 무승부 = MatchWinner==nullptr?** → 행 스캔은 복제 타이밍에 취약
- **왜 로컬 대조가 포인터?** → 봇 기본 이름 중복 가능
- **UI→StatusComponent 읽기 예외** → 내 스탯 출처가 거기뿐. 읽기만 + 주석 명시

## 연결
[33-CA3DGameState.md](../Framework/33-CA3DGameState.md) · [34-CA3DPlayerState.md](../Framework/34-CA3DPlayerState.md) · [38-CA3DHUD.md](38-CA3DHUD.md)

## Q&A
아직 없음
