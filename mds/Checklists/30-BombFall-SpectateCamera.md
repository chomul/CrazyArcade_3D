# Checklist 30 — 폭탄 위치 복제 · 킥 낙하 · 관전 카메라 (PIE 실측 반영)

> 대응: 2026-08-09 사용자 PIE 피드백 3건. 서브에이전트 2개 병렬 → 병합 후 **직접** 재검증.
> 선행: 체크리스트 28(킥·봇) · 29(관전). 둘 다 이 문서로 일부 대체된다.
> **PIE 를 실제로 돌리지 않은 항목은 체크하지 않는다.**

## 사용자 실측 (2026-08-09)
> "킥 체감 했는데 잘 날아가고 벽 앞에서도 멈춰. 다만 ① 절벽에서 밀었을 때 문제가 있어서 낙하는
> 추가해야 할 것 같아. ② 킥할 때 폭발 범위만 이동하는 건지 폭탄 스태틱 메시의 움직임이 없어.
> ③ 관전은 시점은 넘어가는데(A/D 정상) 카메라 위치가 이상해 — 다른 플레이어가 보는 시점 그대로
> 받아서 하면 좋을 것 같아."

---

## ② 메시가 안 움직인다 — 진짜 원인은 Mobility 가 **아니었다**

- [x] **기각된 가설**: `UStaticMeshComponent` 기본 Mobility 가 Static 이라 못 움직인다
      → `USceneComponent::USceneComponent()` 가 `Mobility = Movable` 을 넣는다(`SceneComponent.cpp:124`).
      `UStaticMeshComponent`·`UBoxComponent` 는 재정의하지 않는다. `Static` 을 넣는 것은
      `AStaticMeshActor` 같은 **액터** 쪽이다
- [x] **정합성 반증**: Static 이었다면 `MoveComponentImpl` 첫 줄의 `CheckStaticMobilityAndWarn` 이
      **회전도 똑같이** 막았을 것이고, 기존 테스트 "② 셀 중심에 스냅"(액터 위치 비교)이 통과할 수 없다
- [x] **진짜 원인**: `AActor::bReplicateMovement` 가 **기본값 false**.
      `AActor::InitializeDefaults()` 에 대입이 없고(유일한 대입은 `SetReplicatingMovement()` 안),
      UObject 메모리는 생성자 전에 0 으로 채워지므로 CDO 값이 false 다.
      `APawn::APawn()` 은 `SetReplicatingMovement(true)` 를 직접 부르지만 **`AActor` 파생은 각자 켜야 한다**
- [x] 그 결과 `ReplicatedMovement` 가
      `DOREPCUSTOMCONDITION_ACTIVE_FAST(AActor, ReplicatedMovement, IsReplicatingMovement())` 로
      통째로 비활성화 → **클라에는 스폰 위치만 도착**. 반면 `Cell` 은 `ReplicatedUsing=OnRep_Cell` 로
      잘 가서 **위험 데칼만 옆 칸으로 옮겨 다니고 폭탄 메시는 제자리** — 보고된 증상과 정확히 일치
- [x] 폭탄은 무브먼트 컴포넌트 없이 킥·낙하가 액터를 직접 옮기는 구조라 **이 플래그 말고는
      위치를 알릴 경로가 없다**
- [x] 수정: `ABomb::ABomb()` 에 `SetReplicatingMovement(true)`
- [x] **같은 함정이 있는 다른 액터 전수 확인 — 없다.** `AItemPickup`(제자리 회전만, 이동 없음) ·
      `APredictedBombVisual`·`ADangerDecal`·`AWaterSegment`·서든데스 마커(전부 `bReplicates=false`
      클라 로컬 풀 액터) · `AVoxelWorld`·`AExplosionFXRelay`(복제되지만 이동 없음).
      **런타임에 이동하면서 복제되는 액터는 `ABomb` 하나뿐**이다

### 회귀 방벽 — 자동화 테스트로는 왜 안 잡히나
- [x] `BombKick` 스위트의 다른 모든 케이스는 **서버 한 곳**에서 돈다. 액터 위치를 직접 비교하는
      테스트가 전부 통과하면서도 클라 화면에서는 폭탄이 서 있을 수 있다
- [x] → 케이스 **⑫** 는 동작이 아니라 **플래그 자체**(`IsReplicatingMovement()`)를 검사한다.
      `EditDefaultsOnly` 라 BP 에서 끌 수 있으므로 코드로 못 박는다

---

## ① 킥 낙하 (2026-08-09 사용자 확정)

| 규칙 | 근거 |
|---|---|
| 새 칸 중심 도달 시 **발밑이 솔리드가 아니면 낙하** | 킥의 칸 단위 판정과 같은 순간에만 판단한다 |
| **수직 등속** `BombFallSpeedCellsPerSec` 12칸/초 | 중력이면 "프레임이 길 때 여러 칸" 계산이 킥과 두 벌이 된다. 킥(8)보다 빨라야 "밀려나간 뒤 곧바로 떨어진다"로 보인다 |
| **수평 이동은 그 순간 끊는다** (포물선 금지) | 칸 정렬이 깨지면 `Cell`(폭발 원점)과 실제 위치가 **영구히** 어긋난다 |
| 한 칸 내려갈 때마다 `ServerSetCell` | 위험 데칼(`OnRep_Cell`)·`FindBombAt` 조회 키가 전부 `Cell` 이다. 미루면 "보이는 곳은 아래, 터지는 곳은 위" |
| **착지하면 정지** — 남은 킥 거리 폐기 | 목적이 "떨어뜨린다"이고, 착지 후 계속 미끄러지면 협곡 바닥에서 어디까지 갈지 예측이 안 된다 |
| 착지할 솔리드가 없는 열이면 **폭발 없이 소멸** | 허공에서 터져봐야 아무것도 못 맞히고 위험 데칼만 맵 밖에 남는다 |

- [x] **낙하 판정이 정지 판정보다 먼저** — 뒤집으면 "마지막 10번째 칸이 절벽이면 공중에 그대로 선다"가
      되어 원래 버그가 그 한 칸만 남는다
- [x] **틱 관리는 `ServerRefreshMovementTick`(`bKicking || bFalling`) 한 곳**이 진다.
      `ServerStartFall` 이 `ServerStopKick` 을 부르는데 그것이 데디에서 틱을 끄면 **폭탄이 공중에 박힌다**
- [x] `Tick` 은 `if (bKicking) … else if (bFalling)` — 독립 `if` 두 개면 킥이 그 틱에 낙하를 시작할 때
      같은 `DeltaSeconds` 를 낙하가 한 번 더 먹어 **시간이 두 번 흐른다**
- [x] 맵 밖 소멸 시 **`ServerSetCell` 을 부르지 않는다** — `Cell` 이 한 프레임이라도 그리드 밖이 되면
      `Propagate`·`FindBombAt`·데칼이 유효하지 않은 원점을 받는다. `Cell` 은 마지막 유효 칸으로 두고
      액터만 없앤다
- [x] ⚠️ **맵 밖 소멸은 `ServerReleaseSlot()` 을 직접 부른다** — `EndPlay` 안전망에 맡기지 않는다.
      `AActor::Destroyed → RouteEndPlay` 는 `bActorInitialized && ActorHasBegunPlay` 일 때만 `EndPlay`
      를 부르고, **실제로 테스트 월드에서 안 돌아 소유자의 `ActiveBombCount` 가 1 로 남았다**(⑩ 실패로
      잡힘). 맵 밖 낙하는 이제 정상 종료 경로이고, 정상 경로가 안전망에 의존하면 안전망이 진짜 이상을
      덮는 순간을 알 수 없다. `bSlotReturned` 가 1회를 보장하므로 이중 반환은 없다
- [x] 퓨즈가 낙하 중 만료되면 `ServerForceDetonate → ServerStopFall` 이 **그 순간 셀 중심으로 스냅**한다.
      공중 셀이면 공중에서 터진다 — 원점을 임의로 땅으로 끌어내리면 위험 데칼과 실폭발이 갈린다(5장 9번)
- [x] **범위 밖**: 이미 설치돼 가만히 있는 폭탄의 **발판이 폭발로 사라졌을 때**의 낙하. 별개 규칙이라
      손대지 않았다

---

## ③ 관전 카메라 — "대상이 보는 시점 그대로"

- [x] **경로 확인(엔진 소스)**: `SpringArmComponent.cpp:58-68` — `bUsePawnControlRotation` 이면
      `Cast<APawn>(GetOwner())->GetViewRotation()` 을 그대로 쓴다. 관전 카메라 각의 출처는 **대상 폰**이다
- [x] **봇**: `AAIController` 가 계산한 "봇이 향한 방향"이 yaw 로 남아 45도 배수가 아니고 매 프레임
      연속으로 변한다 → 격자가 비스듬히 기울고 카메라가 계속 미끄러진다
- [x] **원격 플레이어(더 나쁘다)**: `Pawn.cpp:360-382` — 컨트롤러가 null 이면 `GetActorRotation()` 으로
      떨어지기 **전에** "내가 이 폰을 관전 중인가"를 보고 `PlayerController->BlendedTargetViewRotation`
      을 돌려준다. 그런데 그 값을 채우는 서버 코드(`PlayerController.cpp:5632`)는 **서버 PC 의
      ViewTarget 이 자기 폰이 아닐 때만** 돈다. 우리 `SetViewTargetWithBlend` 는 **클라 로컬 호출**이라
      서버 PC 의 ViewTarget 은 계속 자기 폰 → 값이 영원히 0 → **카메라가 수평으로 눕는다**.
      즉 봇만의 문제가 아니라 구조적 결함이고, 라운드 B 에서 그대로 재현됐을 것이다

### 해결 구조
- [x] **`Core/CameraYawSnap`(신규) — 45도 스냅 공식의 단일 출처.** 쓰는 곳이 셋이 됐다:
      ① 컨트롤러(내 카메라) ② `ACA3DCharacter::GetViewRotation`(남이 나를 볼 때) ③ 봇(방향→인덱스).
      갈라지면 "내가 보는 각 ≠ 관전자가 나를 볼 때의 각"이 되는데 **관전에 들어가야만 보이는** 버그다.
      Core 에 둔 이유는 `Voxel/VoxelMovement` 와 같다 — 의존 없는 순수 계산
- [x] `NumSteps=8` · `StepDeg=45` 는 **룰셋이 아니라 구조 상수** — 런타임에 바뀌면 이미 복제된
      인덱스의 의미가 통째로 달라진다
- [x] 히스테리시스는 내부에서 `StepDeg/2` 미만으로 클램프 — 그보다 크면 한 칸 옆조차 넘어가지 못해
      카메라가 특정 각에 영원히 붙는다
- [x] **`ACA3DPlayerState::CamYawIndex`(uint8 0~7) 복제.** 로컬 플레이어는 **바뀔 때만**
      `ServerSetCamYawIndex` (Q/E 를 누를 때뿐 — 매 틱 보내지 않는다), 서버가 `StepsToIndex` 로 새니타이즈
- [x] **`ACA3DCharacter::GetViewRotation()` 오버라이드** — 로컬 플레이어가 조종 중인 폰이면 `Super`
      (내 화면의 진짜 카메라는 컨트롤러의 `ControlRotation` 이고 Q/E 보간까지 거기 있다).
      그 외(봇·원격)는 `(CameraPitchDeg, 보간된 스냅 yaw, 0)`
- [x] **보간은 컨트롤러가 아니라 폰**(`ACA3DCharacter::UpdateSpectateCamYaw`, `Tick` 최상단).
      ① `GetViewRotation()` 은 `const` 이고 한 프레임에 여러 번 불릴 수 있어 그 안에서 상태를 굴릴 수 없다
      ② 이 각은 "누가 보고 있는가"와 무관한 **폰의 표현**이다 — 관전자 쪽에 두면 관전자가 여럿일 때
      사람마다 다른 각을 보고, 대상을 바꿀 때마다 보간이 처음부터 다시 돈다
      ③ 속도가 로컬 카메라와 **같은 룰셋 값**(`CameraYawInterpSpeed`)이라 손맛이 같다
- [x] 데디 가드를 걸지 **않는다** — 결과가 `GetViewRotation()` 이고 그건 엔진이 넷모드와 무관하게 부르는
      폰 질의다. 서버에서만 값이 굳어 있으면 조용한 어긋남이 된다 (HISM 계열). 비용은 캐릭터당 틱당
      `RInterpTo` 하나. 첫 프레임은 보간 없이 스냅(스폰 직후 카메라가 한 바퀴 도는 것 방지)
- [x] **`ABotController::Tick` 의 pitch 덮어쓰기 제거** (체크리스트 29 의 1차 수정 폐기).
      안전한 근거: `AAIController::UpdateControlRotation → APawn::FaceRotation` 은
      `bUseControllerRotationPitch/Yaw/Roll` 이 **전부 false 면 즉시 반환**한다(`Pawn.cpp:1079-1082`).
      `ACharacter` 생성자가 `Yaw=true` 로 두는 것을 우리 생성자가 `false` 로 덮어 셋 다 false 다.
      프로젝트 전체에서 `ControlRotation` 을 읽는 곳은 `ACA3DPlayerController` 뿐이었다
- [x] **관전 대상(`SpectateTarget`)은 여전히 복제하지 않는다** — 시점은 로컬 표시다. 새로 복제하는 것은
      "그 사람의 카메라 yaw" 하나이고, 그것은 관전 상태가 아니라 **폰의 표현**이라 복제가 맞다
- [x] `ELifeState::Spectating` 은 이번에도 추가하지 않았다 (체크리스트 29 근거 그대로)

---

## 룰셋 추가
| 이름 | 기본값 | 근거 |
|---|---|---|
| `BombFallSpeedCellsPerSec` | 12칸/초 | 킥(8)보다 빨라야 "밀려나간 뒤 곧바로 떨어진다"로 보인다. 등속인 이유는 위 표 |
| `SpectateBotCamYawHysteresisDeg` | 12도 | 0 이면 경계(22.5도)에서 방향이 미세하게 흔들릴 때마다 카메라가 45도씩 튄다 — 관전 중 가장 눈에 띄는 멀미 요인. 크면 봇이 확실히 방향을 튼 뒤에도 한참 안 돈다 |

## 빌드·검증 (2026-08-09 — 서브에이전트 보고와 별개로 직접 재실행)
- [x] 프로젝트 파일 재생성(`CameraYawSnap.h/.cpp` 신규)
- [x] `CrazyArcade3DEditor` / `CrazyArcade3DServer` 양쪽 `Result: Succeeded`
- [x] **전체 24스위트 실패 0**
- [x] 1차 실행에서 **2건 실패 → 둘 다 진짜 결함이었다**:
      `PlayerState ⑦`(복제 프로퍼티 개수 3→4, 가드 테스트가 의도대로 발화) ·
      `BombKick ⑩`(맵 밖 소멸 시 슬롯이 반환되지 않음 — 위 `EndPlay` 항목)
- [x] `-game` 실전 (대형 맵, 봇 6, 230초): 킥 3회 발동/정지 · **낙하 2건**
      (`(16,19,2)`·`(17,18,2)` 발밑 없음 → 1200cm/s) · 관전 전환 1회(스팸 없음) ·
      매치 종료 후 전환 없음(시점 고정) · `LogCA3D` Error·ensure **0**

---

## 2차 PIE 실측 반영 (2026-08-09 · 같은 날 재확인 후)

> "낙하는 문제 없이 되는 것 같아. BP_Bomb Replicate Movement 도 켜져 있고 관전 카메라도 잘 되는데
> **회전도 가능하게 해줘.** 그리고 **회전 기능 45도 기준인데 90도로 변경**해줘."

### ① 관전 중 회전 — 각의 주인을 "보는 사람" 으로 바꿨다
- [x] 1차 설계는 관전 카메라 각을 **대상 폰의 표현**으로 고정했다. 그러면 "그 사람이 보는 각"은
      정확히 재현되지만 **관전자가 돌릴 수 없다** — 요청과 정면으로 충돌한다
- [x] `ACA3DCharacter::GetViewRotation()` 을 **"지금 이 폰을 보고 있는 로컬 컨트롤러가 있으면
      그 컨트롤러의 각"** 으로 바꿨다. 조종 중인 내 폰과 관전 중인 남의 폰이 **같은 가지**가 되어
      Q/E 가 그대로 먹는다. 한 머신에 로컬 컨트롤러는 하나뿐이라 "누구 각이냐" 는 모호함이 없다
- [x] 그래도 **"다른 플레이어가 보는 시점 그대로" 는 유지된다** — `SetSpectateTarget` 이 대상의
      복제된 `CamYawIndex` 를 **시작각으로 받아 온다**. 받은 뒤부터는 관전자 것이다
- [x] 시작각을 스냅하지 않고 **스텝만 바꾼다** — 컨트롤러의 기존 보간이 카메라 위치 블렌드
      (`SpectateBlendTime`)와 나란히 돌아 각과 위치가 함께 미끄러진다. 스냅하면 위치는 블렌드되는데
      각만 먼저 튄다
- [x] 판정에 `PendingViewTarget` 도 본다 — 블렌드 중에는 이전 대상과 새 대상이 **둘 다** 카메라
      계산을 탄다. 한쪽만 인정하면 전환 0.4초 동안 한 화면에 각이 두 개가 되어 카메라가 휘청인다
- [x] **폰의 보간 상태(`SpectateCamYaw`)를 제거했다.** 각의 주인이 보는 사람으로 정리되면서
      폰이 따로 보간할 이유가 사라졌다 — 관전자의 컨트롤러가 이미 자기 `SmoothCamYaw` 를 굴린다.
      캐릭터당 틱당 `RInterpTo` 하나가 함께 사라졌다
- [x] `GetLocalViewingController` 는 **"내가 로컬 폰인가" 를 먼저** 본다. 가장 흔한 경우를 반복 없이
      끝내고, `PlayerCameraManager` 가 없는 환경(자동화 테스트 월드)에서도 성립한다

### ② 45도 → 90도 스냅 (사용자 확정)
- [x] `CameraYawSnap::NumSteps` **8 → 4** 하나만 바꾸면 끝난다 — 스텝 크기·인덱스 범위·히스테리시스
      경계가 전부 이 값에서 파생되기 때문이다. **단일 출처를 만들어 둔 것이 여기서 값을 했다**
- [x] 복제 인덱스 범위가 0~7 → **0~3** 으로 바뀐다. `uint8` 폭은 그대로라 복제 비용 변화 없음
- [x] 부수 효과(의도한 것): 카메라 축이 **항상 그리드 축과 나란해진다.** 45도에서는 절반의 각도에서
      카메라 기준 W 가 격자를 대각선으로 가로질러 "한 칸 앞"이 어디인지 흐려졌다
- [x] 테스트를 **스텝 크기에 무관하게** 고쳐 썼다 (22.5/45/135/315 같은 상수 제거 → `StepDeg` 파생).
      값을 박아두면 테스트가 규칙이 아니라 옛 값을 지키게 된다
- [x] 문서 동기화: GDD 5장 표("45도 단위" → "90도 단위") · `CLAUDE.md` 확정된 값 · 체크리스트 11

### 검증 (2026-08-09 2차)
- [x] 두 타깃 빌드 `Result: Succeeded`
- [x] **전체 24스위트 실패 0** (`Spectate` 에 ⑬ "관전 시작각을 대상에게서 받아 온다" 추가)
- [x] `-game` 실전(대형 맵, 봇 6, 200초): 관전 대상 자동 전환 **3회**(대상이 죽을 때마다) ·
      매치 종료 후 전환 없음 · `LogCA3D` Error·ensure **0**

## 남은 검증 (미실행 — 체크 금지)
- [ ] **관전 중 Q/E 회전을 사람이 직접** 눌러 확인 (자동화는 배선만 덮는다)
- [ ] 90도 스냅에서 **가림 페이드**가 여전히 자연스러운가 — 구멍 폭이 카메라 우측 기준이라
      45도 스냅에서 있던 √2 출렁임이 이제 원리적으로 사라진다(스냅각이 항상 월드 축). 확인만
- [ ] 90도가 실제로 더 나은가 — 45도에서는 볼 수 있던 각이 사라진다 (사각지대 체감)
- [x] ⚠️ **리슨+클라에서 킥 한 번** — ② 는 멀티에서만 드러난다 →
      **2026-08-09 사용자 재확인 완료** (`BP_Bomb` Replicate Movement 확인 + 증상 소멸, 낙하 체감 정상)
- [ ] 리슨+클라에서 **남의 캐릭터를 관전**했을 때 90도 스냅 고정 내려보기가 유지되는가
      (2026-08-09 45도 → 90도 변경 반영)
- [ ] 절벽으로 폭탄을 밀어 **협곡 바닥에서 터뜨리는 플레이**가 실제로 성립하는가 (체감)
- [ ] 낙하 속도 12칸/초 체감 — 너무 빠르면 "밀었는데 사라진 것처럼" 보인다
- [ ] 진짜 데디 exe 에서 낙하 중 틱이 유지되는가 (`ServerRefreshMovementTick` — PIE 로는 안 잡힌다)

## 에디터에서 할 일
1. **`BP_Bomb` Class Defaults → Replication → "Replicate Movement" 가 켜져 있는지 확인.**
   현재 BP 는 이 값을 오버라이드하지 않으므로 C++ 변경이 그대로 먹지만, `EditDefaultsOnly` 라
   **BP 에서 끄면 같은 버그가 그대로 재발한다**
2. (선택) `DA_Rules_Default` 의 `BombFallSpeedCellsPerSec`(12) · `SpectateBotCamYawHysteresisDeg`(12) —
   신규 프로퍼티라 에셋에 값이 없어 C++ 기본값이 쓰인다. 체감 후 조정하려면 여기서
3. Mobility 관련 작업 **없음** — BP 가 Mobility 를 오버라이드하고 있지 않다
