# Checklist 27 — 가림 디더 페이드 (카메라~캐릭터 사이 블록 투명화)

> GDD 5장 "가림 처리 = 반투명(디더 페이드)" · GDD 7.4 "트레이스는 0.1초 간격"
> Task 11 에서 카메라 붐 컬리전을 끄면서 미룬 항목 (2026-07-30 → 2026-08-06 구현)
> **PIE 를 실제로 돌리지 않은 항목은 체크하지 않는다.**

## 왜 붐을 당기지 않는가 (되풀이 방지)
- [x] 붐 컬리전을 켜면 벽에 닿을 때마다 팔 길이가 줄어 **화면 배율이 튄다** — 고정 시점
      아케이드에서 "몇 칸 떨어져 있나"를 눈으로 재는 감각이 무너진다 (2026-07-30 사용자 결정).
      그래서 카메라는 그대로 두고 **블록 쪽을 옅게** 만든다

## 빌드 (필수 게이트) — 2026-08-06 직접 실행
- [x] `CrazyArcade3DEditor` 빌드 통과 (`Result: Succeeded`, 번역 단위 병합 강제)
- [x] `CrazyArcade3DServer` 빌드 통과 (`Result: Succeeded`, 동일)
- [x] 프로젝트 파일 재생성 실행 (신규 .h/.cpp 3쌍)

## 구조 (정적 검증)
- [x] **동적 머티리얼 인스턴스를 만들지 않는다** (GDD 7.4 금지 항목) —
      HISM 인스턴스별 커스텀 데이터 float 1개(`NumCustomDataFloats = 1`)로 처리.
      블록 수천 개가 공유 머티리얼 하나를 그대로 쓴다
- [x] `bMarkRenderStateDirty = false` — true 면 매 프레임 프리미티브 프록시를 통째로
      다시 만든다. 커스텀 데이터는 엔진이 인스턴스 델타로 반영한다
      (`FPrimitiveInstanceDataManager::CustomDataChanged`)
- [x] **Voxel 이 Gameplay 를 참조하지 않는다** — `IVoxelRenderer::SetCellFade` 는 숫자만 받는다.
      "카메라가 나를 못 본다"는 개념은 Gameplay(`UOcclusionFadeComponent`)에만 있다
- [x] 격자 레이 마칭은 **순수 함수** (`VoxelRay::GatherSolidCells`) — 월드·CellSize·액터 접근 0.
      그래서 PIE 없이 테스트된다 (`Propagate` 를 순수 함수로 둔 것과 같은 이유)
- [x] 데디 서버는 `BeginPlay` 에서 컴포넌트 틱을 끈다 (불변식 5). 렌더러가 없으면
      `AVoxelWorld::SetCellFade` 가 false 로 빠진다 (이중 안전망)
- [x] 재계산은 0.1초 간격(`OcclusionTraceInterval`), **페이드 보간은 매 프레임** —
      틱 자체를 0.1초로 늦추면 초당 10단계로 끊겨 보인다
- [x] 수치 5개 전부 룰셋 (`OcclusionTraceInterval` · `OcclusionFadeSpeed` ·
      `OcclusionFadeAmount` · `OcclusionSampleCount` — 매직 넘버 0)

## 동작 검증 — 자동화 (2026-08-06 · **전체 19스위트 실패 0** 직접 실행)
`CrazyArcade3D.Voxel.VoxelRayCast` (신규):
- [x] 빈 그리드 → 0칸
- [x] 축 정렬 직선 → 사이 블록 전부 집고, **선분 너머(캐릭터 뒤) 블록은 안 집는다**
- [x] **대각선에서 칸을 건너뛰지 않는다** — 연속한 칸의 맨해튼 거리가 항상 1 (DDA 무결성).
      이게 깨지면 사이에 낀 블록만 안 옅어져 "가끔 안 된다"로 보인다
- [x] 격자 밖(상공) 출발에서도 죽지 않고 안쪽 블록을 집는다 — 카메라는 늘 맵 밖에 있다
- [x] 퇴화 입력(시작==끝, MaxSteps 0)에서 크래시·무한 루프 없음
- [x] 역방향도 같은 칸 집합

`CrazyArcade3D.Voxel.HISMVoxelRenderer` (배선 검증 추가):
- [x] ⓐ `WorldToCellFloat(CellToWorld(C)) == C + 0.5` (소수부 보존 — 잘리면 첫 칸이 어긋난다)
- [x] ⓑ 모든 HISM 의 `NumCustomDataFloats == 1`
- [x] ⓒ `SetCellFade(cell, 0.75)` → 그 인스턴스의 `PerInstanceSMCustomData` 가 실제로 0.75
- [x] ⓓ 파괴된 셀 → `false` (컴포넌트가 추적을 끊는 신호. 없으면 사라진 블록을 영원히 갱신)
- [x] ⓔ 빈 셀 → `false`
- [x] ⓕ **전체 사슬**: 맵 밖 카메라 → 캐릭터 레이 마칭 → 잡힌 칸이 전부 실제 solid →
      그중 최소 하나에 페이드가 실제로 적용됨

## `-game` 실전 세션 (2026-08-06)
- [x] 컴포넌트가 실제로 돈다 — `UOcclusionFadeComponent: 가림 블록 0칸 (샘플 5개)`.
      로컬 폰 확인·PlayerCameraManager·VoxelWorld·그리드 초기화 게이트를 전부 통과했다는 뜻
- [x] 에러·ensure 0

## ⚠️ 알아 둘 기하 — 지금 카메라에서는 가림이 드물다
- [ ] **현재 카메라(-55도, 12칸)와 폴백 맵(내부 블록 1칸 높이)에서는 가림이 거의 안 생긴다.**
      카메라는 캐릭터보다 약 9.8칸 위·6.9칸 뒤 → 시선이 1칸 뒤에서 이미 z≈3.3 이라
      **내부 블록(z=1) 위를 그냥 지나간다.** 실측 세션에서 0칸이 나온 것이 이 때문이며 버그가 아니다.
      실제로 걸리는 것은 **외곽 벽(z=1~2)** 이고, 그것도 캐릭터 발치 샘플에서 주로 걸린다
      (그래서 샘플을 5개로 둔다 — 중심 1개면 이 경우를 통째로 놓친다)
- [ ] 즉, 가림이 자주 보이려면 둘 중 하나가 필요하다:
      **① 카메라 피치를 얕게**(`CameraPitchDeg` -55 → -40 근처) 또는
      **② 맵에 2~3칸 높이 구조물** (Task 22 절차적 생성에서)

## 남은 작업 — 에디터 (이거 없이는 화면에 아무 변화가 없다)
- [x] 블록 머티리얼이 `PerInstanceCustomData` 0번을 읽어 디더 마스크로 쓰게 수정 (사용자 완료)
- [ ] **화면 마스크 추가 (2026-08-06 사용자 요청 "몸통이 가려진 그 부분만")** —
      `MPC_CA3DOcclusion` 생성 + 룰셋 지정 + 머티리얼에 마스크 곱 추가 (아래 "머티리얼 배선" 절)
- [ ] 구멍 크기·부드러움 체감 (`OcclusionMaskScale` 1.35 · `OcclusionMaskSoftness` 0.4)
- [ ] `ca3d.DebugOcclusionFade 1` 로 판정 위치 눈 확인 (머티리얼 전에도 확인 가능)
- [ ] 페이드 정도·속도 체감 튜닝 (`OcclusionFadeAmount` 0.8 → 1.0 이면 완전히 사라짐)
- [ ] (Listen+클라) 각자 자기 카메라 기준으로만 페이드되는지 — 남의 캐릭터 앞은 안 뚫려야 정상
- [ ] 데디 서버 exe 에서 컴포넌트가 아무것도 안 하는지 (PIE 로는 안 잡힌다)

## 머티리얼 배선 (에디터 작업 절차)

**두 값을 곱하는 것이 전부다.**

| 값 | 뜻 | 출처 |
|---|---|---|
| `PerInstanceCustomData` | **이 블록이** 가리는가 (0~0.8) | 인스턴스 커스텀 데이터 |
| 화면 마스크 | **이 픽셀이** 캐릭터를 덮는가 (0~1) | 파라미터 컬렉션 |

둘 다 필요하다. 화면 마스크만 쓰면 캐릭터 **뒤쪽** 벽과 발밑 바닥에도 구멍이 뚫리고,
블록 판정만 쓰면 벽 한 칸이 통째로 사라져 지형이 헷갈린다 (2026-08-06 사용자 지적).

### ① 파라미터 컬렉션 만들기
`Materials & Textures → Material Parameter Collection`, 이름 예 `MPC_CA3DOcclusion`.
**이름이 글자 그대로 일치해야 한다** (틀리면 엔진이 조용히 무시 — C++ 이 경고 1회를 찍는다):

| 종류 | 이름 | 내용 |
|---|---|---|
| Vector | `OcclusionMask` | R=중심u, G=중심v, B=반지름u, A=반지름v |
| Scalar | `OcclusionMaskSoftness` | 가장자리 흐림 폭 |

만든 뒤 `DA_Rules_Default` → Camera → `OcclusionMaskCollection` 에 지정.

### ② 블록 머티리얼
**Blend Mode = `Masked`**, `Opacity Mask Clip Value` 기본값(0.333) 유지.

```
ScreenPosition (ViewportUV) ─┐
                             ├─ (P - Center) / Radius  →  Length  →  D
MPC.OcclusionMask ───────────┘     (RG=Center, BA=Radius)

D ─→ SmoothStep(Min=1, Max=1+Softness) ─→ OneMinus ─→ Mask   (구멍 안=1, 밖=0)

Fade = PerInstanceCustomData × Mask
OneMinus(Fade) ─→ Dither Temporal AA ─→ Opacity Mask
```

노드로 풀면:
1. `ScreenPosition` (ViewportUV 출력) → `P`
2. `CollectionParameter`(OcclusionMask) → `ComponentMask(R,G)` = 중심, `ComponentMask(B,A)` = 반지름
3. `Subtract`(P, 중심) → `Divide`(반지름) → `Length` → `D`
4. `SmoothStep`(Value=D, Min=1, Max=1+Softness) → `OneMinus` → `Mask`
   · `1+Softness` 는 `CollectionParameter`(OcclusionMaskSoftness) + `Constant 1`
5. `Multiply`(`PerInstanceCustomData`, `Mask`) → `OneMinus` → `Dither Temporal AA` → **Opacity Mask**

**반지름을 나누는 것이 타원의 정체다** — u/v를 각각의 반지름으로 나누면 화면 비율·원근·카메라
피치가 전부 흡수된 타원이 나온다. 그래서 머티리얼은 화면 비율을 몰라도 된다 (C++ 이 머리·발·
옆구리를 각각 투영해 이미 화면 좌표로 재 놓았다).

### 왜 Masked + 디더인가
Translucent 로 하면 정렬(sorting) 문제가 생기고 블록 수천 개가 반투명이 되면 오버드로가
급증한다. 디더는 불투명 렌더링 그대로에 픽셀만 버리므로 사실상 비용이 없다.

### 안 해도 안 깨진다
- 컬렉션 미지정 → 화면 마스크 없이 **블록 단위** 페이드 (이전 동작)
- 머티리얼 미수정 → 커스텀 데이터가 놀 뿐, 에러도 성능 영향도 없음
