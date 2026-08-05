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

## 에디터 작업 (2026-08-06 완료 — 화면에서 동작 확인됨)
- [x] 블록 머티리얼이 `PerInstanceCustomData` 0번을 읽어 디더 마스크로 쓰게 수정
- [x] **화면 마스크** — `MPC_CA3DOcclusion` 생성 + 룰셋 지정 + 머티리얼에 마스크 곱
- [x] **머티리얼 인스턴스 3개의 `Material Property Overrides` 해제** (아래 함정 ③ — 이게 진짜 원인이었다)
- [ ] 구멍 크기·부드러움 체감 튜닝 (`OcclusionMaskScale` 1.35 · `OcclusionMaskSoftness` 0.4 ·
      `OcclusionFadeAmount` 0.8). 지금 값으로 "잘 보인다" 확인 — 더 만질지는 플레이하며 결정
- [ ] `MPC_CA3DOcclusion` 의 `OcclusionMask` **기본값을 `(0.5, 0.5, 0.2, 0.3)`** 으로.
      지금 (0,0,0,0) 이라 반지름 0 → 0으로 나누기. C++ 이 매 프레임 덮어쓰므로 게임에선 안 드러나지만,
      머티리얼 프리뷰와 첫 프레임이 깨진다

## 🔥 이번에 실제로 밟은 함정 (같은 길로 다시 들어가지 않기 위해)

에디터 작업 후에도 화면이 그대로여서 6라운드를 썼다. 원인은 **머티리얼 인스턴스 오버라이드**였고,
C++ 은 처음부터 정상이었다. 다음에 비슷한 증상이 나오면 이 순서로 가른다.

### ① 먼저 C++ 과 머티리얼의 경계를 가른다 — `ca3d.DebugOcclusionFade 2`
박스 위에 **인스턴스에서 되읽은 실제 페이드 값**이 뜬다 (`IVoxelRenderer::GetCellFade`).
- `0.80` → C++ 은 할 일을 다 했다. **머티리얼만 남았다**
- `0.00` → 페이드가 안 올라감 (C++ 문제)
- `-1.00` → 그 셀에 렌더 인스턴스 없음 (내부에 묻힌 블록)

이 진단이 없었으면 계속 양쪽을 동시에 의심했을 것이다. **경계를 먼저 긋는다.**

### ② "고치고 있는 머티리얼이 정말 그 블록 건가"
`M_Base_Platform` 이 맞았지만, 확인 없이 두 번 헛짚었다. 디스크에서 계보를 추적할 수 있다:
```bash
grep -a -o "/Game/[A-Za-z0-9_/]*" Content/Blueprints/BP_VoxelWorld.uasset | sort -u   # 어떤 메시?
grep -a -o "Materials/Color_[0-9]*/MI_[A-Za-z0-9_]*" Content/Meshes/SM_....uasset      # 어떤 MI?
grep -a -o "/Game/PLATFORMER.../[A-Za-z0-9_/]*" .../MI_....uasset                      # 부모는?
```
⚠️ **메시 에셋 자체의 머티리얼 할당은 옛 정보일 수 있다** — 실제 배선은 `BP_VoxelWorld` 의
`BlockMeshes` 다. 나는 옛 `SM_Block_*` 을 읽고 `M_Floor` 라고 잘못 결론지었다.

### ③ 머티리얼 인스턴스가 부모를 이긴다 ← **진짜 원인**
MI 의 `Material Property Overrides`(`BasePropertyOverrides`)에 `OpacityMaskClipValue` /
`BlendMode` 오버라이드가 켜져 있으면 **부모를 Masked 로 바꿔도 오파시티 마스크가 통째로 무시된다.**
계산은 전부 정상이라(베이스 컬러에 꽂으면 값이 잘 나온다) 증상이 "아무 일도 안 일어남"뿐이다.
```bash
grep -a -o "BasePropertyOverrides\|OpacityMaskClipValue\|BlendMode" .../MI_....uasset
```
그리고 **메시 하나가 슬롯을 여럿 갖고, 메시마다 다른 MI 를 쓴다.** 실제로 A/B/D 세 메시가
MI 3개(`Grass_Clr_01_A`, `Sand_Clr_01_A`, `Sand_Clr_01_B`)를 썼다 — 하나만 고치면
"일부 블록만 안 뚫린다"가 되고 그건 원인 찾기가 더 어렵다.

### ④ 노드 이름이 문서 표기와 다르다
- MPC 를 읽는 노드는 **`Collection Param`** (`MPC`·파라미터 이름으로는 검색해도 안 나옴).
  Details 에서 **`Collection` 을 먼저** 지정해야 `Parameter Name` 드롭다운이 채워진다
- 채널 분리는 **`Component Mask`** (그래프 표시는 `Mask (R,G)`)
- `SmoothStep` 핀은 위에서부터 **Min / Max / Value** — **Value 가 맨 아래**
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
