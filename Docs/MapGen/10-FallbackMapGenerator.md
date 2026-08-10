# UFallbackMapGenerator

> `MapGen/FallbackMapGenerator.h/.cpp` · UObject + IMapGenerator — 하드코딩 21×21×4

## 역할
- 절대 실패하지 않는 고정 맵 — 절차 생성 실패 시 안전망
- 1주차엔 본 맵, 지금은 폴백 + 검증기 임계값의 기준점

## 왜
- **왜 하드코딩?** → 폴백의 덕목은 "절대 실패 안 함". 랜덤·float 없음 = 실패 모드 없음
- **실패도 결정론** → 서버가 폴백을 탔으면 클라도 똑같이 폴백(리롤이 고정식이라).
  "서버는 폴백, 클라는 절차 맵" 상황이 구조적으로 불가
- **아이템만 공용 본체** → 지형은 하드코딩이어도 아이템 규칙은 한 벌
- **덤** → 폴백 실측(스폰 간 맨해튼 9)이 검증 임계값(8)의 근거

## 연결
[08-MapGenerator.md](08-MapGenerator.md) · [12-MapGenUtil.md](12-MapGenUtil.md)

## Q&A
아직 없음
