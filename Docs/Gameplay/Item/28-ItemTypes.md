# EItemType · FItemPlacement (ItemTypes.h)

> `Gameplay/Item/ItemTypes.h` — 5종: Balloon·Potion·Roller·Needle·Kick / `FItemPlacement` = 셀+종류

## 역할
- 아이템 어휘 + 배치 예약 데이터. MapGen·Voxel·Gameplay가 공유하는 최소 공통 타입

## 주요 값
| 이름 | 설명 |
|---|---|
| `Balloon` | 최대 폭탄 수 +1 |
| `Potion` | 폭발 범위 +1 |
| `Roller` | 이동 속도 배수 + |
| `Needle` | 갇힘 탈출 (별도 키, 수동, 1회 소모) |
| `Kick` | 폭탄 차기 해금 (플래그) |
| `FItemPlacement { Cell, Type }` | "이 셀의 파괴 블록 안에 이 아이템" 예약 |

## 왜
- **왜 타입 파일 분리?** → MapGen이 `AItemPickup`(액터)을 알면 의존 방향이 꼬임.
  순수 데이터만 별도 헤더로
- **왜 "배치 예약"?** → 숨은 아이템 = 데이터, 노출된 것만 액터 (dense/sparse 분할).
  블록 파괴 시 `ConsumeItemPlacement`로 꺼내 그때 스폰
- **왜 5종?** → 원작 핵심만. 효과가 전부 StatusComponent 기존 필드에 1:1 —
  아이템이 상태 필드를 새로 만들지 않음

## 연결
생산: [12-MapGenUtil.md](../../MapGen/12-MapGenUtil.md) · 보관: [03-VoxelWorld.md](../../Voxel/03-VoxelWorld.md) · 액터: [29-ItemPickup.md](29-ItemPickup.md)

## Q&A
아직 없음
