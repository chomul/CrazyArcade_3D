# WBP_Lobby 제작 가이드 (Task 41)

> `LobbyWidget.cpp` 의 실제 갱신 코드 기준. 형식은 `mds/Tasks/26-MatchWidget.md`(WBP_Match) 와 같다.
> **없어도 게임은 돈다** — 캔버스 폴백 + 콘솔(`ca3d.Ready` · `ca3d.StartMatch`)로 로비가 동작한다.

## 만들기

`Content/UI/` 우클릭 → User Interface → **Widget Blueprint** → 부모 클래스 **`LobbyWidget`**
(All Classes 검색). 이름 `WBP_Lobby`. → `BP_CA3DHUD` 의 **`UI > Lobby Widget Class`** 에 지정.

## 계층 구조

```
[Root] Canvas Panel                        (UserWidget 기본 루트)
│
└─ LobbyRoot ················ Vertical Box       ← 이름 자유. 화면 중앙 패널
   │
   ├─ (Text Block "로비")  ···················    ← 이름 자유. 제목, 없어도 됨
   │
   ├─ StatusText ············ Text Block    ★    "2/3 명 준비 완료 — 준비를 기다리는 중"
   │
   ├─ RosterText ············ Text Block    ★    참가자 목록 (여러 줄이 한 덩어리로 들어온다)
   │
   └─ ButtonRow ············· Overlay 또는 Size Box   ← 이름 자유. 버튼 두 개가 겹쳐 산다
      │
      ├─ ReadyButton ········ Button        ★    비방장 전용
      │  └─ ReadyButtonText · Text Block    ★    "준비" / "준비 해제"
      │
      └─ StartButton ········ Button        ★    방장 전용
         └─ (Text Block "게임 시작") ·········     ← 이름 자유. C++ 가 안 만진다 (고정 문구)
```

★ = C++ 바인딩 이름. **대소문자까지 정확히** 일치해야 하고 **"Is Variable" 체크**가 켜져 있어야 한다
(Button 은 기본 켜짐, Text Block 은 꺼져 있는 경우가 있으니 확인할 것).

### 버튼 두 개를 왜 겹쳐 두나
C++ 가 **방장에게는 `StartButton` 만, 비방장에게는 `ReadyButton` 만** 보이게 하고 반대쪽을
`Collapsed` 로 접는다. 겹쳐 두면 누가 보든 버튼이 같은 자리에 하나만 나타난다.
Overlay 대신 Vertical Box 에 나란히 넣어도 동작은 같다(접힌 쪽이 자리를 차지하지 않는다).

## 캔버스 슬롯 배치값

| 위젯 | Anchor | Alignment | Position | Size |
|---|---|---|---|---|
| `LobbyRoot` | Center (0.5, 0.5) | (0.5, 0.5) | (0, 0) | 480 × 420 (Size To Content 꺼도 됨) |

`LobbyRoot` 내부(Vertical Box) 슬롯 권장값:

| 자식 | Horizontal Align | Padding | 비고 |
|---|---|---|---|
| 제목 | Center | (0, 0, 0, 12) | Font 28 |
| `StatusText` | Center | (0, 0, 0, 16) | Font 18 |
| `RosterText` | **Fill** | (0, 0, 0, 24) | Font 20, **Justification = Left** · Size Rule **Fill(1.0)** |
| `ButtonRow` | Center | (0, 0, 0, 0) | 버튼 Size Box 220 × 56 권장 |

## RosterText 는 여러 줄이다 — 이것만 주의

C++ 가 참가자 목록을 **개행으로 이어 붙인 한 덩어리 문자열**로 넣는다:

```
▶ ★ 방장  Player 1
   [준비]  Player 2
   [대기]  Player 3
```

- **`Auto Wrap Text` 는 끄는 편이 낫다** — 줄바꿈은 C++ 가 이미 넣었고, 켜면 긴 이름에서
  한 줄이 두 줄로 접혀 열 정렬이 깨진다.
- 앞 3칸은 **본인 표식(`▶`) 자리**다. 남의 행은 공백으로 들여써 열을 맞춘다 —
  **고정폭(monospace) 폰트**를 쓰면 정렬이 정확해진다 (`Roboto Mono` 등).
- 세로 여유를 넉넉히: 8인이면 8줄이다. `RosterText` 슬롯을 Fill 로 두면 알아서 늘어난다.

## C++ 가 덮어쓰는 값 — 디자이너에서 만지지 말 것

| 위젯 | 무엇을 | 언제 |
|---|---|---|
| `StatusText` Text | 안내 문구 | 인원·준비 수·방장 여부가 바뀐 프레임 |
| `RosterText` Text | 참가자 목록 | 목록 본문이 바뀐 프레임 |
| `ReadyButtonText` Text | "준비" / "준비 해제" | 내 준비 상태가 바뀔 때 |
| `ReadyButton`·`ReadyButtonText` Visibility | 방장이면 `Collapsed` | 〃 |
| `StartButton` Visibility | 비방장이면 `Collapsed` | 〃 |
| `StartButton` **IsEnabled** | 전원 준비 전 `false` | 〃 |

디자이너 값(에디터에서 입력한 문구)은 **편집 중 미리보기용**이다 — 실행하면 C++ 값으로 덮인다.

폰트·색·버튼 스타일·배경 Border·애니메이션은 전부 디자이너 소관.

## ⚠️ 하지 말 것

- **이벤트 그래프에 로직 금지.** 특히 `OnClicked` 를 BP 에서 다시 바인딩하지 말 것 —
  C++ `NativeConstruct` 가 이미 바인딩했고, BP 에서 또 걸면 요청이 두 번 나간다.
- `StartButton` 의 IsEnabled 를 BP 바인딩(Function Binding)으로 만들지 말 것 —
  판정은 서버와 **같은 함수**(`CanStartFromLobby`)를 통과해야 한다. BP 로 다시 계산하면
  "버튼은 활성인데 눌러도 시작이 안 되는" 상태가 생긴다.
- 버튼을 `Collapsed` 가 아닌 `Hidden` 으로 바꾸지 말 것 (C++ 가 매번 덮어쓰므로 의미는 없지만,
  Hidden 은 자리를 차지해 겹쳐 둔 두 버튼의 레이아웃이 흔들린다).

## 확인

1. 실행 후 로그에 **`미바인딩 위젯` 경고가 없어야** 한다 — `BindWidgetOptional` 이라
   이름이 틀려도 WBP 는 컴파일되므로, **경고 부재가 5개 전부 연결됐다는 유일한 증거**다.
2. `DA_Rules_Default` → `Lobby` → **`Use Lobby` 체크** (안 켜면 로비 자체가 안 뜬다).
3. 클라 2대로: 첫 접속자에게 시작 버튼(비활성) · 둘째에게 준비 버튼 →
   둘째가 준비 → 첫 접속자 시작 버튼 활성 → 시작 → 캐릭터 선택 (`mds/packaged-test.md`).
