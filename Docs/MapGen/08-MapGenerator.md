# IMapGenerator

> `Source/CrazyArcade3D/MapGen/MapGenerator.h` · UINTERFACE

맵 생성기 계약: `Generate(Seed, Size, Rules, OutGrid, OutSpawns, OutItems)`.
**같은 입력 ⇒ 항상 같은 출력**(결정론)이 문서화된 계약이다.

## 왜 이렇게 했는가

- **왜 인터페이스인가 — 3주 일정의 보험** — 절차 생성기가 3주차까지 미완이어도
  폴백 생성기로 데모가 가능해야 한다(GDD 리스크 표 1번). 구현체 2개
  (`UProcMapGenerator` / `UFallbackMapGenerator`)의 교체가 호출부 한 줄이다.
- **왜 결정론이 계약 수준인가** — 지형 동기화 전체가 "시드만 보내고 클라가 재생성"에
  걸려 있다. 생성기가 비결정적이면 이 프로젝트의 넷코드 설계가 통째로 무너진다.
  그래서 구현 규칙(불변식 4: 정수 연산·`FRandomStream`만, TMap 순회 금지)이
  헤더 주석에 계약으로 명시돼 있다.
- **왜 `Size`를 호출자가 넘기나** — 맵 크기는 인원수 티어로 **서버가** 정하는 값이다.
  생성기가 스스로 정하면 크기 결정 로직이 생성기마다 복제되고, 클라 재생성 시
  같은 크기를 보장할 방법이 복잡해진다. 크기는 입력, 생성기는 순수 변환.
- **왜 스폰·아이템도 생성기가 내나** — 스폰 위치와 아이템 배치도 지형과 함께
  결정론이어야 한다(클라가 재생성 시 동일해야 함). 지형 따로 스폰 따로 만들면
  난수 소비 순서가 갈라질 여지가 생긴다.

## 연결
- 구현: [09-ProcMapGenerator.md](09-ProcMapGenerator.md) · [10-FallbackMapGenerator.md](10-FallbackMapGenerator.md) ·
  호출자: `AVoxelWorld::InitGridFromSeed`([03-VoxelWorld.md](../Voxel/03-VoxelWorld.md))

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
