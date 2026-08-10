# ACA3DPlayerController

> `Source/CrazyArcade3D/Gameplay/Character/CA3DPlayerController.h/.cpp` · APlayerController

Enhanced Input 바인딩 + 90도 스냅 카메라 + 사망 후 관전. **클라 로컬 관심사 전용** —
게임 상태를 바꾸지 않는다(판정·상태 변경은 캐릭터의 서버 RPC 소관).

## 왜 이렇게 했는가

- **컨트롤러는 "전달"만** — `OnPlaceBomb`/`OnUseNeedle`은 캐릭터 함수를 부를 뿐 검증이 없다.
  검증을 컨트롤러에 두면 봇(컨트롤러가 다름)과 경로가 갈라진다.
- **카메라: 컨트롤러가 yaw 스텝 상태만 소유** — 스프링암은 `bUsePawnControlRotation`으로
  `GetViewRotation()`을 따른다. 스텝은 랩하지 않고 누적(보간이 FRotator 정규화로 최단
  경로를 택하므로 안전). **90도 상수를 여기 두지 않는다** — 스냅 공식의 단일 출처는
  `CameraYawSnap`. 사본을 두는 순간 관전 각과 갈라진다.
- **`PushCamYawIndex`는 인덱스가 바뀐 순간에만 RPC** — 매 틱 보내면 8인 기준 초당
  수백 개의 무의미한 RPC. 복제 기본값(0)과 초기 스텝(0)이 일치해 한 번도 회전하지 않은
  플레이어는 RPC를 **0회** 보낸다.
- **이동 기준각은 보간각이 아니라 스냅각** — 회전 보간 중에도 WASD 기준이 흔들리지 않아
  회전 중 이동이 끊기지 않는다.
- **관전은 전부 로컬 표시** — "지금 누구를 보는가"는 복제하지 않는다(남의 관전 대상까지
  동기화하는 순수 낭비). "누가 살아 있는가"만 기존 복제값(`bAlive`)에서 읽는다.
  폰은 건드리지 않는다(`SetViewTargetWithBlend`만) — UnPossess·관전 폰 스폰을 쓰면
  부활 여지·입력 바인딩·HUD 폰 캐시가 전부 깨진다.
- **관전 여부의 유일한 조건 = `IsLocalPawnDead()`** — `ELifeState::Spectating`을 추가하지
  않았다. 관전은 복제 상태가 아니라 로컬 시점이라 얻는 게 없고, `== Dead` 검사 12군데를
  전부 두 값 검사로 늘려야 해 회귀 위험만 크다.
- **관전 대상은 폰이 아니라 PlayerState** — 폰은 사망·재스폰으로 바뀔 수 있지만
  PlayerState는 매치 내내 같은 객체이고 `bAlive`의 출처이기도 하다.
- **좌/우 축 래치** — Enhanced Input의 Triggered는 누르는 동안 매 프레임 온다.
  래치 없이는 한 번 눌러 생존자 전원을 지나친다.
- **대상 전환 시 대상의 `CamYawIndex`를 시작각으로만 받는다** — "그 사람이 보던 시점
  그대로" 시작하되, 받은 뒤부터는 관전자가 Q/E로 돌린다(각의 주인 = 보는 사람).

## 네트워크 표면
RPC 1개: `ServerSetCamYawIndex(uint8)` — PlayerState 복제용 표현 값.

## 연결
- 각 공식: [25-CameraYawSnap.md](../../Core/25-CameraYawSnap.md) · 각 소비: `GetViewRotation`([23-CA3DCharacter.md](23-CA3DCharacter.md)) ·
  생존 출처: [34-CA3DPlayerState.md](../../Framework/34-CA3DPlayerState.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
