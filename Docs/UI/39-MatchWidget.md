# UMatchWidget

> `UI/MatchWidget.h/.cpp` · UUserWidget (Abstract) — 레이아웃은 WBP, 내용은 C++

## 역할
- HUD 3요소: 생존·시간 / 내 스탯 / 서든데스 경고 + 결과 화면(순위·공동 등수·무승부·탈주)
- 방식: 매 틱 폴링 → 스냅샷 비교 → static 순수 함수로 가공 → 변한 것만 갱신

## 주요 변수·함수
| 이름 | 설명 |
|---|---|
| `FMatchStatSnapshot` | 내 스탯 6값+Cap 스냅샷 — `==` 비교로 갱신 억제 |
| 위젯 바인딩 11개 (`BindWidgetOptional`) | WBP의 텍스트 블록들 — 미바인딩은 경고 1줄 |
| `NativeTick()` | 폴링 5단계: 시간→생존→스탯→결과→서든데스 경고 |
| `NativeConstruct()` | 미바인딩 위젯 이름 경고 |
| `ShowResult()` | 결과 화면 — 출처를 스스로 읽음 (인자 없음) |
| `RefreshPawnCache()` | 폰 바뀔 때만 StatusComponent 재해석 |
| `FormatElapsedTime / FormatAliveCount / Format*` (static) | 표시 문자열 가공 — 위젯 없이 테스트 |
| `BuildResultRows()` (static) | 정렬·공동 등수·`!bMatchEnded`면 빈 배열(누출 게이트) |
| `FormatResultRow / FormatLocalHeadline` (static) | 행 문자열·헤드라인(무승부>우승>내 순위) |
| `CollectResultRows / CaptureStats / ResolveRules` (static) | 수집·스냅샷·룰셋 해석 |
| `UpdateSuddenDeathWarning(bool)` | 경고 표시/숨김 |

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
