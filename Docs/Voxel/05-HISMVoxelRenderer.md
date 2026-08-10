# UHISMVoxelRenderer

> `Voxel/HISMVoxelRenderer.h/.cpp` · UActorComponent + IVoxelRenderer

## 역할
- 표면 셀만 타입별 HISM 인스턴스로 생성·제거 · 셀↔인스턴스 양방향 맵 유지
- 페이드 값을 인스턴스 커스텀 데이터로 전달
- **치명적 부업**: 이 인스턴스들이 지형의 유일한 물리 컬리전

## 주요 변수·함수
| 이름 | 설명 |
|---|---|
| `BlockMeshes` (TMap) | 블록 타입 → 스태틱 메시 (BP에서 지정) |
| `HISMs` (TMap) | 타입별 HISM 컴포넌트 (lazy 생성) |
| `CellToInstance` / `InstanceToCell` | 타입별 양방향 셀↔인덱스 맵 — 스왑 보정의 근거 |
| `BuildFromGrid` | Clear 후 표면 셀만 인스턴스 생성 + 비율 로그 |
| `RemoveBlock` | 인스턴스 제거 + **스왑 보정** + 주변 6칸 표면 재검사 |
| `SetCellFade / GetCellFade` | 인스턴스 커스텀 데이터 float 쓰기/되읽기 |
| `AddInstanceForCell` (내부) | 중복 방지 + 트랜스폼 계산 + 맵 등록 |
| `IsSurface` (내부) | 6이웃 중 하나라도 Empty인가 (경계는 자동 표면) |
| `GetOrCreateHISM` (내부) | 타입별 HISM lazy 생성 — CustomData는 등록 전 설정 |

## 왜
- **왜 HISM?** → 같은 메시 수백 개를 타입당 1 드로우콜로
- **왜 표면 추출?** → 내부 블록은 안 보임. 전체의 20~40%만 인스턴스 생성.
  파괴 시 주변 6칸 재검사로 새 표면 노출
- **왜 스왑 보정?** → `RemoveInstance(i)`는 마지막 인스턴스를 i로 당겨옴.
  맵 갱신 안 하면 엉뚱한 블록이 사라짐 (이 클래스 최대 함정)
- **왜 타입별 이중 맵?** → 인스턴스 인덱스가 HISM별 공간이라 한 맵이면 충돌
- **⚠️ 데디 가드 금지** → 시각 전용인 줄 알고 끄면 서버에 바닥이 없음(실제 사고,
  지상 판정 3.6%→58%). PIE 데디는 에디터 프로세스라 못 잡음 — 진짜 exe로만
- **왜 커스텀 데이터 1 float?** → 동적 MI 금지(GDD 7.4). dirty=false로 프록시 재생성 회피
- **`NumCustomDataFloats`는 Register 전에** → 등록 후엔 기존 인스턴스 미적용

## 연결
계약: [04-VoxelRenderer.md](04-VoxelRenderer.md) · 페이드 출처: [27-OcclusionFadeComponent.md](../Gameplay/Character/27-OcclusionFadeComponent.md)

## Q&A
- **Q. Renderer 클래스들은 클라에서만 동작하나?** → 아니. 역할이 둘이라서:
  그리기(GPU)는 클라만 의미 있지만(데디는 RHI Null — 자연 생략),
  **HISM 인스턴스 = 지형의 유일한 컬리전**이라 `BuildFromGrid`/`RemoveBlock`은
  데디에서도 돌아야 함. 실제로 데디에서 껐다가 캐릭터가 지형을 통과한 사고
  (지상 판정 3.6%→58%). `SetCellFade`만 사실상 클라 전용(호출자가 카메라 기반).
  진짜 클라 전용 시각은 WaterSegment·DangerDecal·PredictedBombVisual·DropMarker 쪽.
  PIE 데디는 에디터 프로세스라 이 버그를 못 잡음 — 진짜 서버 exe로만 검증
