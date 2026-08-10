# IMapGenerator

> `MapGen/MapGenerator.h` · UINTERFACE

## 역할
- 생성기 계약: (시드·크기·룰셋) → (그리드·스폰 셀·아이템 배치)
- 결정론 요구를 계약으로 못박음 — 같은 입력 = 항상 같은 출력

## 왜
- **왜 인터페이스?** → 3주 일정 보험. 절차 생성기가 미완이어도 폴백으로 데모 가능.
  구현체 교체가 한 줄
- **왜 결정론이 계약?** → 지형 동기화 전체가 "시드만 보내고 클라 재생성"에 걸려 있음
- **왜 Size를 호출자가?** → 크기는 서버 결정 값. 생성기는 순수 변환만
- **왜 스폰·아이템도 함께?** → 셋 다 결정론이어야 함. 따로 만들면 난수 소비 순서가 갈라짐

## 연결
구현: [09-ProcMapGenerator.md](09-ProcMapGenerator.md) · [10-FallbackMapGenerator.md](10-FallbackMapGenerator.md) · 호출: [03-VoxelWorld.md](../Voxel/03-VoxelWorld.md)

## Q&A
아직 없음
