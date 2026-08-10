# UOcclusionFadeComponent

> `Source/CrazyArcade3D/Gameplay/Character/OcclusionFadeComponent.h/.cpp` · UActorComponent

카메라↔캐릭터 사이를 가리는 블록을 디더 페이드로 옅게 만든다. **클라 시각 전용, 게임 상태 무접촉.**

## 왜 이렇게 했는가

- **역할 분담: "카메라·폰을 아는 쪽"은 여기, 지형은 숫자만 받는다** — 가림 판정
  (카메라 위치, 캐릭터 위치, 사이 셀)은 Gameplay의 관심사다. 지형(`IVoxelRenderer::
  SetCellFade`)은 "이 셀 페이드 값이 얼마다"만 받는다 — Voxel이 "카메라가 나를 못 본다"는
  개념을 모른 채로 남는 의존 규칙의 실천.
- **판정은 `VoxelRay`(3D DDA) 순수 함수** — 물리 트레이스가 아니라 그리드에 직접 묻는다.
  가리는 "셀"이 답이므로 그리드 질문이 자연스럽고 물리 상태 의존이 없다.
- **동적 머티리얼 금지(GDD 7.4) 준수** — HISM 인스턴스별 커스텀 데이터 float 1개로 전달.
  동적 머티리얼 인스턴스는 인스턴싱 이점을 파괴한다.
- **화면 마스크와 블록 페이드를 곱한다 (둘 다 필요)** — 마스크만 쓰면 캐릭터 뒤쪽 벽·
  발밑 바닥에도 구멍이 뚫리고, 블록 판정만 쓰면 벽 한 칸이 통째로 사라진다.
  C++이 캐릭터를 화면에 투영해 타원(중심·반지름 uv)을 머티리얼 파라미터 컬렉션에 쓰고,
  머티리얼이 `PerInstanceCustomData × 화면마스크`로 곱한다(컬렉션은 동적 MI가 아니라 7.4 무위반).
- **재계산 0.1초 간격, 보간은 매 프레임** — 셀 판정(DDA)은 비싸므로 간격을 두고,
  페이드 값 보간만 매 프레임 — 비용과 부드러움의 절충(GDD 7.4 명시).
- **진단 도구를 처음부터** — `ca3d.DebugOcclusionFade 1`(판정 박스) / `2`(인스턴스에서
  **되읽은** 실제 페이드 값). 2번이 존재하는 이유: "C++ 문제인지 머티리얼 문제인지"를
  가르는 이분 도구 — 실제로 머티리얼 인스턴스 오버라이드 함정을 이걸로 잡았다.

## 연결
- 레이캐스트: [07-VoxelRayCast.md](../../Voxel/07-VoxelRayCast.md) · 페이드 전달: [04-VoxelRenderer.md](../../Voxel/04-VoxelRenderer.md) ·
  구현: [05-HISMVoxelRenderer.md](../../Voxel/05-HISMVoxelRenderer.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
