# UProcMapGenerator

> `MapGen/ProcMapGenerator.h/.cpp` · UObject + IMapGenerator — 산·협곡 Z6층

## 역할
- 절차 지형 생성: 외벽 → 계단식 구조물 → 평지 → 스폰 → 아이템
- 검증(`FMapValidator`) 불통과 시 파생 시드로 리롤. 서버·클라 양쪽에서 같은 결과

## 왜
- **왜 리롤 구조?** → "항상 유효한 맵 직접 구성"은 어렵고 "생성+검증+재시도"는 쉽고
  예측 가능. 상한 초과 시 폴백
- **왜 리롤 시드가 `Seed*7919+attempt` 고정식?** → 리롤도 결정론. 서버가 3번째에
  성공했으면 클라도 정확히 3번째에 같은 맵
- **왜 단계 순서가 계약?** → 순서 = 난수 소비 순서 = 결과. 바꾸면 같은 시드에 다른 맵
- **왜 웨딩케이크 계단?** → 이동 규칙이 "1칸만 오름". 전 층이 1칸 계단이어야 연결성 통과
- **파괴:고정 8:2** → 사용자 확정. 구조물 전부 Destructible, Immortal은 외벽+기둥만
- **왜 크기 티어?** → 인원 대비 맵이 크면 조우가 없음. 판정 서버·결과 복제
- **아이템은 `FMapGenUtil` 공용** → 폴백과 같은 함수 (규칙 두 벌 방지)

## 실증
같은 시드 5회 해시 동일 · 다른 시드 10개 고유 10/10

## 연결
[08-MapGenerator.md](08-MapGenerator.md) · [11-MapValidator.md](11-MapValidator.md) · [12-MapGenUtil.md](12-MapGenUtil.md)

## Q&A
아직 없음
