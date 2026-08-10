# FMapGenUtil

> `MapGen/MapGenUtil.h/.cpp` · static 헬퍼

## 역할
- 파괴 블록에 아이템 배치(`PlaceItemsInDestructibles`) — 드롭 확률 + 종류 가중치 추첨
- 절차·폴백 생성기가 공유하는 유일한 배치 규칙

## 왜
- **왜 공용 본체?** → 생성기마다 따로면 "폴백 판만 아이템 규칙이 다름"이 조용히 발생
- **왜 Z→Y→X 고정 순회?** → 순회 순서 = 난수 소비 순서 = 결과 (불변식 4)
- **왜 정수 % 추첨?** → float 확률 비교는 플랫폼 간 오차 가능. 정수는 어디서나 동일
- **가중치는 룰셋** → 밸런스 조정이 코드 무수정

## 연결
호출: [09-ProcMapGenerator.md](09-ProcMapGenerator.md) · [10-FallbackMapGenerator.md](10-FallbackMapGenerator.md) · 타입: [28-ItemTypes.md](../Gameplay/Item/28-ItemTypes.md)

## Q&A
아직 없음
