# IVoxelRenderer

> `Voxel/VoxelRenderer.h` · UINTERFACE

## 역할
- "그리드 데이터 → 화면" 번역 계약: `BuildFromGrid / RemoveBlock / Clear / SetCellFade`
- 구현 교체 지점 — 게임 코드는 그리는 방법을 모름

## 주요 함수
| 이름 | 설명 |
|---|---|
| `BuildFromGrid(Grid)` (순수가상) | 그리드 전체 → 인스턴스 일괄 생성 |
| `RemoveBlock(Cell, Grid)` (순수가상) | 이 칸이 비워짐 → 제거 + 주변 표면 노출 |
| `Clear()` (순수가상) | 전부 제거 |
| `SetCellFade(Cell, float)` (기본 no-op) | 페이드 값 전달 — 숫자만 받음. 미지원 시 false |
| `GetCellFade(Cell)` (기본 -1) | 실제 들어간 값 되읽기 (진단용) |

## 왜
- **엔진이 자동으로 안 그려주나?** → 엔진은 컴포넌트를 그림. 바이트 배열은 그릴 대상이
  아님 — 누군가 번역해야 함
- **왜 인터페이스 한 겹?** → 번역 전략이 교체 대상(HISM → 그리디 메싱).
  없으면 VoxelWorld 재작성, 있으면 구현체 교체 한 줄
- **왜 계약이 셀·숫자뿐?** → 지형 독립성. `SetCellFade`도 숫자만 —
  "카메라에 가려짐" 개념은 Gameplay 것
- **왜 `if (Renderer)` 가드?** → 렌더러 없이 로직 테스트 가능해야 함
- **왜 SetCellFade는 기본 no-op?** → 나중에 추가된 기능. 순수가상이면 전 구현체 강제

## 연결
구현: [05-HISMVoxelRenderer.md](05-HISMVoxelRenderer.md) · 소유: [03-VoxelWorld.md](03-VoxelWorld.md)

## Q&A
- **Q. 엔진이 자동 렌더링해주는 거 아냐?** → "존재하는 컴포넌트"만 그려줌. 데이터는 안 그림.
  번역기(렌더러)가 필요하고, 인터페이스로 감싼 건 교체·독립성·테스트 3가지.
  덤: "데디에서 생략" 의도였지만 HISM=컬리전이라 지금은 데디에서도 돎
