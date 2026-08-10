# VoxelRay (namespace)

> `Voxel/VoxelRayCast.h/.cpp` · 순수 함수 — 3D DDA (Amanatides & Woo)

## 역할
- 두 셀 사이 광선이 지나는 솔리드 셀 수집(`GatherSolidCells`) — 가림 판정의 계산 엔진
- 그리드만 읽음. 월드 좌표·물리·카메라 모름

## 주요 함수
| 이름 | 설명 |
|---|---|
| `GatherSolidCells(Grid, StartCell, EndCell, MaxSteps, OutCells)` | 시작→끝 셀 광선이 지나는 솔리드 셀 수집. 좌표는 셀 단위 float |

## 왜
- **왜 엔진 트레이스 안 씀?** → 답이 "어느 셀"이라 그리드 질문이 자연스러움.
  물리 트레이스는 셀 역변환 필요 + 컬리전 상태 의존
- **왜 DDA?** → 균일 격자의 표준. 셀을 하나도 안 놓침(대각 통과 없음)
- **왜 셀 좌표계?** → 월드→셀 변환은 호출자 책임. 의존 없음 = 테스트·재사용 가능

## 연결
유일한 소비자: [27-OcclusionFadeComponent.md](../Gameplay/Character/27-OcclusionFadeComponent.md)

## Q&A
아직 없음
