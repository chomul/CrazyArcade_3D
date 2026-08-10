# EItemType · FItemPlacement (ItemTypes.h)

> `Source/CrazyArcade3D/Gameplay/Item/ItemTypes.h`

`EItemType` 5종: `Balloon`(폭탄 수+1) · `Potion`(범위+1) · `Roller`(속도+) ·
`Needle`(갇힘 탈출, 수동) · `Kick`(폭탄 차기 해금).
`FItemPlacement`: `{ Cell, Type }` — "이 셀의 파괴 블록 안에 이 아이템이 들었다".

## 역할

- 아이템의 **어휘**(`EItemType` 5종)와 **배치 예약 데이터**(`FItemPlacement` = 셀+종류)를
  정의한다.
- MapGen(배치 생산) · Voxel(배치 보관) · Gameplay(액터·효과)가 공유하는 최소 공통 타입.

## 왜 이렇게 했는가

- **왜 타입 파일이 따로 있나** — 맵 생성기(MapGen)가 아이템 배치를 출력해야 하는데,
  MapGen이 `AItemPickup`(액터)을 알면 의존 방향이 꼬인다. 순수 데이터 타입만 별도 헤더로
  분리해 MapGen·Voxel·Gameplay가 모두 가볍게 공유한다.
- **`FItemPlacement`가 "배치 예약"인 이유** — 아이템은 블록이 부서지기 전까지 액터로
  존재하지 않는다(dense/sparse 분할 — 숨은 아이템은 데이터, 노출된 아이템만 액터).
  블록 파괴 시 `ConsumeItemPlacement`로 데이터를 꺼내 그때 액터를 스폰한다.
- **왜 5종인가** — 원작 핵심만. 효과가 전부 `UStatusComponent`의 기존 필드에 1:1 대응 —
  아이템 종류가 상태 필드를 새로 만들지 않는다.

## 연결
- 배치 생산: [12-MapGenUtil.md](../../MapGen/12-MapGenUtil.md) · 보관: [03-VoxelWorld.md](../../Voxel/03-VoxelWorld.md) ·
  액터: [29-ItemPickup.md](29-ItemPickup.md) · 효과 적용: [24-StatusComponent.md](../Character/24-StatusComponent.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
