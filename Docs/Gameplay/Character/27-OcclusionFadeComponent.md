# UOcclusionFadeComponent

> `Gameplay/Character/OcclusionFadeComponent.h/.cpp` · UActorComponent — 클라 시각 전용

## 역할
- 카메라↔캐릭터 사이 가림 셀 판정(0.1초 주기, `VoxelRay`) → 페이드 값을 렌더러에 전달
- 캐릭터의 화면 투영 타원을 머티리얼 파라미터 컬렉션에 기록(화면 마스크)

## 주요 변수·함수
| 이름 | 설명 |
|---|---|
| `CurrentTargets` (TSet) | 지금 가리고 있는 셀들 |
| `FadeValues` (TMap) | 셀별 현재 페이드 값 (보간 상태) |
| `TraceInterval=0.1` / `FadeSpeed` / `FadeAmount` | 판정 주기·보간 속도·목표 페이드 |
| `SampleCount` / `MaskScale` / `MaskSoftness` | 몸 샘플 수·마스크 타원 크기·경계 부드러움 |
| `TickComponent` | 주기 도달 시 판정 + 매 프레임 보간 |
| `RefreshOccluders()` | 카메라→몸 샘플들 DDA로 가림 셀 갱신 |
| `AdvanceFades(Delta)` | 페이드 값 보간 → `SetCellFade` (실패 셀은 추적 제거) |
| `UpdateMaskParameters()` | 화면 타원(중심·반지름 uv)을 MPC에 기록 |

## 왜
- **왜 여기?(Voxel 아님)** → "카메라·폰을 아는 쪽". 지형은 숫자(`SetCellFade`)만 받음
- **왜 VoxelRay?** → 답이 "어느 셀"이라 그리드 질문. 물리 상태 의존 없음
- **왜 동적 MI 안 씀?** → GDD 7.4 금지 — 인스턴싱 파괴. 커스텀 데이터 float 1개로
- **왜 마스크×블록 페이드 둘 다?** → 마스크만: 뒤쪽 벽·바닥에도 구멍 /
  블록만: 벽 한 칸 통째로 사라짐. 곱해야 "가려진 픽셀만" 뚫림
- **왜 판정 0.1초·보간 매 프레임?** → DDA는 비쌈, 보간은 쌈 — 비용·부드러움 절충
- **진단 도구** → `ca3d.DebugOcclusionFade 2` = 인스턴스에서 **되읽은** 값 —
  C++/머티리얼 어느 쪽 문제인지 이분 (MI 오버라이드 함정을 이걸로 잡음)

## 연결
[07-VoxelRayCast.md](../../Voxel/07-VoxelRayCast.md) · [04-VoxelRenderer.md](../../Voxel/04-VoxelRenderer.md) · [05-HISMVoxelRenderer.md](../../Voxel/05-HISMVoxelRenderer.md)

## Q&A
아직 없음
