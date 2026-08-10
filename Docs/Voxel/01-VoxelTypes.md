# EBlockType (VoxelTypes.h)

> `Source/CrazyArcade3D/Voxel/VoxelTypes.h` · `UENUM(uint8)`

블록 1칸의 종류. `Empty=0 / Floor=1 / Destructible=2 / Immortal=3`.

## 역할

- 지형 데이터의 **어휘**를 정의한다 — 그리드의 모든 셀은 이 4값 중 하나다.
- 폭발 판정(`BlocksExplosion`)·파괴 가능 여부·이동 판정(`IsSolid`)의 분기 기준.
- 이 파일에는 로직이 없다 — 타입 정의만.

## 왜 이렇게 했는가

- **왜 4종뿐인가** — 게임 규칙이 요구하는 구분이 정확히 이만큼이다:
  폭발이 통과(Empty) / 바닥이라 원칙적으로 안 부서짐(Floor, 룰셋 플래그로 예외) /
  한 방에 부서짐(Destructible) / 절대 안 부서지고 폭발도 막음(Immortal).
  종류를 늘리는 순간 `FVoxelGrid`의 "셀 = 1바이트" 전제와 모든 판정 분기가 같이 늘어난다.
- **왜 `uint8` 기반 enum class인가** — `FVoxelGrid::Blocks`(TArray&lt;uint8&gt;)에 그대로 저장되기 위해.
  저장은 원시값, API 경계(`Get/Set`)에서만 enum으로 캐스트해 타입 안전을 얻는다.
- **왜 `Empty=0`인가** — `TArray::Init(0)` 한 번으로 "빈 공간"이 기본값이 된다.
  범위 밖 조회도 Empty를 돌려주므로(경계 검사 단순화) 0이 자연스러운 중립값이다.
- **Floor와 Destructible을 나눈 이유** — "바닥 파괴 허용"이 게임 디자인 미결정 항목이었기 때문.
  타입을 나눠 두면 정책이 룰셋 플래그(`bFloorDestructible`, 서든데스는 `bSuddenDeathDestroysFloor`)
  하나로 바뀐다. 실제로 서든데스(Task 24)가 이 분리 덕에 `Propagate` 무수정으로 구현됐다.

## 연결
- 저장: [02-VoxelGrid.md](02-VoxelGrid.md) · 정책 분기: `Propagate`([16-ExplosionSubsystem.md](../Gameplay/Bomb/16-ExplosionSubsystem.md))

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
