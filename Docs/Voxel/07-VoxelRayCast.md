# VoxelRay (namespace)

> `Source/CrazyArcade3D/Voxel/VoxelRayCast.h/.cpp` · 순수 함수

격자 위 3D DDA(Amanatides & Woo) — 시작 셀에서 끝 셀까지 광선이 지나는 솔리드 셀을 모은다.
`GatherSolidCells(Grid, StartCell, EndCell, MaxSteps, OutCells)`.

## 역할

- 두 셀 좌표 사이를 잇는 광선이 지나는 **솔리드 셀들을 수집**한다 — 가림 판정의 계산 엔진.
- 그리드만 읽는 순수 함수 — 월드 좌표·물리·카메라를 모른다.

## 왜 이렇게 했는가

- **왜 엔진 라인 트레이스를 안 쓰나** — 가림 판정(카메라↔캐릭터 사이 블록)은 "어느 셀들이
  가리는가"라는 **그리드 질문**이다. 물리 트레이스는 컬리전 지오메트리에 묻는 것이라
  결과를 다시 셀로 역변환해야 하고, 물리 상태(컬리전 켜짐 여부 등)에 의존하게 된다.
  그리드에 직접 물으면 판정이 데이터와 1:1이다.
- **왜 순수 함수 + 셀 좌표계인가** — 월드 좌표·CellSize를 모른다. 호출자
  (`OcclusionFadeComponent`)가 월드→셀 변환을 책임진다. Voxel 폴더의 다른 것들과 같은 이유:
  의존 없음 → 테스트 가능, 재사용 가능.
- **DDA인 이유** — 셀 크기가 균일한 격자에서 광선 통과 셀을 구하는 표준 알고리즘.
  한 스텝에 한 셀씩 정확히 방문하므로 놓치는 셀(대각 통과)이 없다.

## 연결
- 유일한 소비자: [27-OcclusionFadeComponent.md](../Gameplay/Character/27-OcclusionFadeComponent.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
