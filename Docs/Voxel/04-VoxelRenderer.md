# IVoxelRenderer

> `Source/CrazyArcade3D/Voxel/VoxelRenderer.h` · UINTERFACE

지형 렌더링 추상화. `BuildFromGrid / RemoveBlock / Clear` (순수가상) + `SetCellFade / GetCellFade` (기본 no-op).

## 역할

- "그리드 데이터 → 화면"의 **번역 계약**을 정의한다: 전체 빌드(`BuildFromGrid`) ·
  한 칸 제거(`RemoveBlock`) · 초기화(`Clear`) · 페이드 전달(`SetCellFade`).
- 구현체 교체 지점 — 게임 코드는 이 인터페이스만 알고, 그리는 방법은 모른다.

## 왜 이렇게 했는가

- **엔진은 "데이터"를 자동으로 그려주지 않는다** — 엔진이 그리는 건 씬 컴포넌트다.
  `FVoxelGrid`는 바이트 배열이라 누군가 "배열 → 컴포넌트" 번역을 해야 하고, 그 번역기가 렌더러다.
- **왜 번역기를 인터페이스로 한 겹 감쌌나 — 번역 전략 자체가 교체 대상이라서.**
  지금은 타입별 HISM + 표면 추출, 성능이 부족해지면 그리디 메싱(인접 블록 병합 메시)으로 승격.
  인터페이스가 없으면 이 교체가 `VoxelWorld.cpp` 재작성이 되고, 있으면 구현체 교체 한 줄이다.
  구조 설계서 리스크 표에 처음부터 "렌더링 성능 부족 → IVoxelRenderer 교체"로 박아둔 항목.
- **계약이 좁아서 지형의 독립성이 지켜진다** — 함수가 전부 "셀과 숫자"만 받는다.
  `SetCellFade(Cell, float)`도 숫자만 받는다 — "카메라가 가려졌다"는 개념은 Gameplay
  (`OcclusionFadeComponent`)의 것이고, 렌더러는 페이드 값이 왜 오는지 모른다.
- **렌더러가 없어도 로직이 돌아야 한다** — `VoxelWorld`의 렌더러 호출은 전부 `if (Renderer)`
  가드 안. 자동화 테스트가 렌더러 없이 그리드 로직만 검증할 수 있는 이유.
- **`SetCellFade`가 기본 구현(no-op)인 이유** — 페이드는 나중에 추가된 기능이다.
  순수가상으로 만들면 기존·미래의 모든 구현체가 강제로 구현해야 한다. 지원 안 하는 렌더러는
  false를 돌려주고, 호출부가 알아서 포기한다.

## 연결
- 구현: [05-HISMVoxelRenderer.md](05-HISMVoxelRenderer.md) · 소유: [03-VoxelWorld.md](03-VoxelWorld.md) ·
  페이드 호출자: [27-OcclusionFadeComponent.md](../Gameplay/Character/27-OcclusionFadeComponent.md)

## Q&A

**Q. IVoxelRenderer는 왜 있나? 원래 엔진이 자동으로 렌더링해주는 것 아닌가?**
엔진은 "존재하는 컴포넌트"를 그릴 뿐 "데이터"를 그려주지 않는다. 지형의 실체는
`TArray<uint8>`이라 엔진 입장에선 그릴 대상이 아예 아니다. 누군가 데이터→컴포넌트 번역을
해야 하고(렌더러), 그걸 인터페이스로 감싼 건 ① 번역 전략 교체(HISM→그리디 메싱) 대비
② 좁은 계약으로 지형 독립성 유지 ③ 렌더러 없이 로직 테스트 — 세 가지 이유다.
덤: 원래 "데디에서 렌더러 생략" 의도도 있었지만, HISM이 지형의 유일한 컬리전이라는 게
실전에서 드러나 지금은 데디에서도 돈다.
