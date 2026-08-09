# Checklist 31 — 아트·사운드 패스 (에디터 제작 절차)

> 대응: GDD 8장 3주차 "아트/사운드 패스, HUD, 결과 화면". 2026-08-09 착수.
> **이 문서는 사용자가 에디터에서 할 일의 단일 목록이다.** C++ 훅은 전부 준비돼 있다.
> 하나도 안 만들어도 게임은 그대로 돈다 — 미지정은 조용히 no-op(큐당 Verbose 1회)이다.

## 왜 이 순서인가
화면을 가장 크게 바꾸는 것부터다. ③ 물줄기가 없으면 **폭발이 판정만 있고 형체가 없고**,
② 마커가 없으면 서든데스를 **테스트조차 할 수 없다**("보고 피한다"가 그 설계의 전부다).

---

## ① `BP_WaterSegment` — 폭발이 보이기 시작한다 (우선순위 1)
- [ ] 부모 `AWaterSegment` 로 BP 생성 → `MeshComponent` 에 메시 지정
- [ ] `DA_Rules_Default` 의 `WaterSegmentClass` 에 지정
- [ ] (선택) 같은 BP 에 나이아가라 컴포넌트를 붙이면 **C++ 수정 0** 으로 물줄기 이펙트가 붙는다.
      칸마다 하나씩 스폰되므로 파티클 수를 아끼는 편이 좋다 (한 폭발에 최대 4~6칸 × 6방향)
- [ ] ⚠️ 세그먼트는 **풀링 대상**이다 — BP 에 상태를 들고 있지 말 것 (재사용 시 오염)

## ② `BP_DropMarker` — 서든데스가 성립한다
- [ ] 부모 `ASuddenDeathDropMarker` 로 BP 생성 → 메시 지정 (바닥에 깔리는 납작한 형태 권장)
- [ ] `DA_Rules_Default` 의 `DropMarkerClass` 에 지정
- [ ] 확인: 마커가 뜬 **바로 그 칸**이 `DropWarningTime`(1.5초) 뒤에 파괴되는가.
      마커 ≠ 낙하 칸이면 실패다 (체크리스트 24)

## ③ `WBP_Match` — 디버그 화면을 벗어난다
- [ ] 부모 `UMatchWidget` 로 UserWidget 생성. **아래 이름 그대로** 배치하면 자동 바인딩된다
- [ ] `BP_CA3DHUD` 의 `MatchWidgetClass` 에 지정 → 지정하는 순간 캔버스 텍스트 폴백은 물러난다

| 위젯 이름 | 타입 | 내용 |
|---|---|---|
| `AliveCountText` | TextBlock | 생존 인원 |
| `MatchTimeText` | TextBlock | 경과 시간 |
| `ItemPanel` | 아무 Panel | 아이템 묶음 (표시/숨김만) |
| `BombCountText` | TextBlock | 폭탄 개수 |
| `BombRangeText` | TextBlock | 폭발 범위 |
| `MoveSpeedText` | TextBlock | 이동 속도 배율 |
| `NeedleText` | TextBlock | 니들 보유 |
| `KickText` | TextBlock | 킥 보유 |
| `SuddenDeathWarning` | 아무 Widget | 서든데스 경고 (평소 숨김) |
| `ResultPanel` | 아무 Widget | 결과 화면 (평소 숨김) |
| `ResultText` | TextBlock | 순위 목록 — **여러 줄**이 들어간다 (Auto Wrap·세로 여유) |

- [ ] 이름이 틀려도 **WBP 는 컴파일된다**(`BindWidgetOptional`). 실행 시 비어 있는 이름을
      `NativeConstruct` 가 한 줄로 경고하니 로그를 보고 맞추면 된다
- [ ] ⚠️ BP 에 로직 금지 — 값 갱신은 전부 C++ 이 한다. WBP 는 배치·스타일만

## ④ `BP_ItemPickup` 메시 5종
- [ ] 부모 `AItemPickup` → `ItemMeshes` 맵에 `Balloon`·`Potion`·`Roller`·`Needle`·`Kick`
- [ ] `DA_Rules_Default` 의 `ItemPickupClass` 에 지정
- [ ] 5종이 **한눈에 구분**되어야 한다 — 색만 다르면 45도 내려보기에서 못 고른다

---

## ⑤ 사운드 — 지정 위치는 전부 `DA_Rules_Default` 의 `Feedback` 카테고리

C++ 훅은 완성돼 있다. 큐가 발화하는 것은 `-game` 실전에서 확인했다(아래 "확인된 발화").

| 룰셋 필드 | 언제 나는가 |
|---|---|
| `BombPlaceSound` | 폭탄 설치 — **설치자는 예측 시점**(즉시), 남·리슨 호스트는 서버 확정 시점 |
| `ExplosionSound` | 폭발 — 연쇄 1단계당 1회, 물줄기 무게중심 |
| `BlockBreakSound` | 블록 파괴 — 20칸이 함께 부서져도 **1회** |
| `ItemPickupSound` | 아이템 획득 |
| `TrappedSound` | 물방울에 갇힘 |
| `EscapeSound` | 니들로 탈출 |
| `DeathSound` | 사망 |
| `KickSound` | 폭탄을 참 |
| `SuddenDeathWarnSound` | 서든데스 예고 — 웨이브당 1회 |
| `MatchEndSound` | 매치 종료 — **감쇠(Attenuation) 지정 금지.** 2D 로 들려야 한다 |

- [ ] `SA_CA3D_Default` (Sound Attenuation) 생성 → `MatchEndSound` **를 제외한** 9종에 지정.
      3D 공간 음향이 있어야 "어디서 터졌는지"가 소리로 읽힌다 (GDD 5장)
- [ ] `SC_CA3D_Default` (Sound Concurrency) 생성 — `Max Count 4~5` · `Resolution: Stop Oldest`.
      최소한 `Explosion`·`BlockBreak`·`BombPlace`·`Kick` 에 지정
      ⚠️ **C++ 에는 동시 재생 상한이 없다.** 이 에셋이 유일한 상한이다 (GDD 7.4).
      8인 연쇄 폭발에서 같은 소리가 수십 개 겹치면 그 순간 오디오가 뭉개진다
- [ ] `FeedbackVolumeMultiplier`(기본 1.0) — 효과음 전체를 한 번에 미는 값

## ⑥ 나이아가라 (선택)
같은 이름 규칙의 `*FX` 필드 10개(`BombPlaceFX` … `MatchEndFX`). `NS_CA3D_Explosion`,
`NS_CA3D_BlockBreak` 정도부터 만들면 체감이 크다.

- [ ] ⚠️ `ExplosionFX`·`BlockBreakFX` 는 **사건당 1회, 한 지점(무게중심)** 에 스폰된다.
      칸마다 파편이 튀는 연출을 원하면 그건 이 큐가 아니라 `BP_WaterSegment` 쪽 작업이다
- [ ] 데디 서버에서 나이아가라가 돌지 않는지 확인 (GDD 7.4 — 재생 경로에 데디 가드가 있다)

---

## 확인된 발화 (2026-08-09 `-game` 실전 · 대형 맵 · 봇 6 · 200초)
큐가 실제로 도달하는지 로그로 확인했다 — 아래 7종이 **각각 한 번씩만** 찍혔다(스팸 없음).

- [x] `Death` · `BombPlace` · `Explosion` · `BlockBreak` · `ItemPickup` · `Trapped` · `MatchEnd`
- [x] `LogCA3D` Error·ensure **0**
- [ ] `Kick` · `SuddenDeathWarn` — 그 판에서 조건이 안 나왔다(킥 아이템 미획득 · 매치가 90초에 종료).
      배선은 자동화 테스트가 덮지만 실전 발화는 미확인

## 남은 검증 (미실행 — 체크 금지)
- [ ] **리슨+클라: 설치자가 설치음을 한 번만 듣는가.** 예측 시점과 서버 확정 시점 두 경로가
      있어 **멀티에서만** 중복이 드러난다. 단독 세션은 예측을 만들지 않아 이 검증이 성립하지 않는다
- [ ] 데디 서버 exe 에서 사운드·나이아가라가 돌지 않는가 (PIE 로는 안 잡힌다 — 에디터 프로세스는
      `IsRunningDedicatedServer()` 가 false)
- [ ] 8인 연쇄 폭발에서 Concurrency 가 실제로 소리를 잘라 주는가

## 알아 둘 것 — 파괴 알림이 "실제로 바뀐 칸"으로 좁혀졌다
`AVoxelWorld::ApplyDestruction` 이 **요청받은 칸이 아니라 이번 호출로 실제 비워진 칸**을
알리도록 고쳤다(2026-08-09, 파괴음 테스트가 잡음). 같은 셀이 두 번 들어오는 것은 정상 경로다 —
중간 접속 클라의 따라잡기, 연쇄 폭발의 범위 겹침, 서든데스와 폭탄이 같은 칸을 치는 경우.
그때 요청 목록을 그대로 알리면 **아무것도 안 부서졌는데 "부서졌다" 는 알림**이 나가고
구독자(프리뷰 갱신·파괴음)가 헛돈다.
