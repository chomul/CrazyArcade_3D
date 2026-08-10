# UHISMVoxelRenderer

> `Source/CrazyArcade3D/Voxel/HISMVoxelRenderer.h/.cpp` · UActorComponent + IVoxelRenderer

타입별 HISM(Hierarchical Instanced Static Mesh)으로 지형을 그린다.
동시에 **지형의 유일한 물리 컬리전**이다.

## 왜 이렇게 했는가

- **왜 HISM인가** — 같은 메시 수백 개를 개별 StaticMeshComponent로 두면 드로우콜이 수백 개.
  HISM은 타입당 1 드로우콜로 묶는다. 블록 종류가 3개(Floor/Destructible/Immortal)라
  HISM도 타입별 3개.
- **왜 표면 추출인가** — 속이 꽉 찬 내부 블록은 어차피 안 보인다. `IsSurface`(6이웃 중
  하나라도 Empty)인 셀만 인스턴스를 만든다 → 전체의 20~40%만 생성. 블록이 부서지면
  주변 6칸을 재검사해 새로 노출된 표면을 추가한다.
- **RemoveInstance 스왑 보정 — 이 클래스 최대의 함정** — HISM의 `RemoveInstance(i)`는
  마지막 인스턴스를 i번으로 **당겨온다**. 셀↔인스턴스 인덱스 맵을 같이 갱신하지 않으면
  다음 파괴 때 엉뚱한 블록이 사라진다. 그래서 양방향 맵(`CellToInstance`/`InstanceToCell`)을
  타입별로 들고, 제거 때마다 이동한 인스턴스의 매핑을 다시 쓴다.
- **왜 타입별 이중 맵인가** — 인스턴스 인덱스는 HISM 컴포넌트별 공간이다.
  타입 구분 없이 한 맵에 담으면 인덱스가 충돌한다.
- **⚠️ 데디 가드 금지** — "렌더러 = 시각 전용"으로 보고 데디에서 끄면 서버에 바닥이 없어져
  캐릭터가 지형을 통과한다(실제 사고: 지상 판정 3.6%→수정 후 58%). HISM 인스턴스가
  이 게임 지형의 **유일한 컬리전 형상**이다. PIE 데디 모드는 에디터 프로세스라
  `IsRunningDedicatedServer()`가 false — 이 버그는 진짜 서버 exe로만 잡힌다.
- **`SetCellFade`가 커스텀 데이터 float 1개인 이유** — 동적 머티리얼 인스턴스는 GDD 7.4 금지
  (인스턴싱 이점 파괴). `PerInstanceCustomData` float 하나로 머티리얼에 페이드 값을 전달하고,
  `bMarkRenderStateDirty=false`로 프록시 재생성을 피한다.
- **`NumCustomDataFloats`는 `RegisterComponent()` 전에** — 등록 후 바꾸면 기존 인스턴스에
  적용되지 않는 엔진 동작.

## 연결
- 계약: [04-VoxelRenderer.md](04-VoxelRenderer.md) · 데이터: [02-VoxelGrid.md](02-VoxelGrid.md) ·
  페이드 값 출처: [27-OcclusionFadeComponent.md](../Gameplay/Character/27-OcclusionFadeComponent.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
