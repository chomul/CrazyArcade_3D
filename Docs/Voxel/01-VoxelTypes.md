# EBlockType (VoxelTypes.h)

> `Voxel/VoxelTypes.h` · UENUM(uint8) — `Empty=0 / Floor=1 / Destructible=2 / Immortal=3`

## 역할
- 지형의 어휘 — 모든 셀은 이 4값 중 하나
- 폭발 차단·파괴 가능·이동 판정의 분기 기준. 로직 없음

## 왜
- **왜 4종뿐?** → 게임 규칙이 요구하는 구분이 딱 이만큼. 늘리면 판정 분기도 같이 늘어남
- **왜 uint8 기반?** → `FVoxelGrid`의 바이트 배열에 그대로 저장하기 위해
- **왜 Empty=0?** → `Init(0)` 한 번이면 빈 공간. 범위 밖 조회도 Empty 반환
- **왜 Floor≠Destructible?** → "바닥 파괴 허용"이 미결정이라 정책을 룰셋 플래그로 분리.
  덕분에 서든데스가 `Propagate` 무수정으로 구현됨

## 연결
[02-VoxelGrid.md](02-VoxelGrid.md) · [16-ExplosionSubsystem.md](../Gameplay/Bomb/16-ExplosionSubsystem.md)

## Q&A
아직 없음
