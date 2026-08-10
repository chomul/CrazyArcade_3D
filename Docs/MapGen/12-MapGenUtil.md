# FMapGenUtil

> `Source/CrazyArcade3D/MapGen/MapGenUtil.h/.cpp` · static 헬퍼

아이템 배치 공용 본체: `PlaceItemsInDestructibles(Grid, Rules, Seed, OutItems)`.
Destructible 블록을 Z→Y→X 고정 순서로 훑으며 정수 퍼센트 추첨 + 가중치 5종 누적 추첨.

## 왜 이렇게 했는가

- **존재 이유 = 절차·폴백 생성기의 공용 본체** — 아이템 배치 규칙이 생성기마다 따로 있으면
  "폴백으로 떨어진 판만 아이템 규칙이 다르다"가 조용히 생긴다. 본체를 하나로 뽑아
  두 생성기가 같은 함수를 부른다.
- **Z→Y→X 고정 순회** — 순회 순서 = 난수 소비 순서 = 결과. 컨테이너 순회(TMap 등)에
  맡기면 플랫폼마다 순서가 다를 수 있다(불변식 4). 3중 for문으로 순서를 코드에 박는다.
- **정수 퍼센트 추첨(`RandRange(0,99) < DropPercent`)** — float 확률 비교는 플랫폼 간
  미세 오차 가능성이 있다. 정수 비교는 어디서나 같다.
- **가중치 누적 추첨** — 아이템 5종(풍선30·물약30·롤러20·니들5·킥15)의 가중치 합에서
  정수 하나를 뽑아 구간으로 결정. 가중치가 룰셋에 있어 밸런스 조정이 코드 무수정.

## 연결
- 호출자: [09-ProcMapGenerator.md](09-ProcMapGenerator.md) · [10-FallbackMapGenerator.md](10-FallbackMapGenerator.md) ·
  출력 타입: [28-ItemTypes.md](../Gameplay/Item/28-ItemTypes.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
