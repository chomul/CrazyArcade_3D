# ACA3DPlayerController

> `Gameplay/Character/CA3DPlayerController.h/.cpp` · APlayerController — 클라 로컬 전용

## 역할
- 입력 바인딩(Enhanced Input) + 캐릭터로 전달
- 카메라 yaw 스텝 소유·보간 · 바뀐 인덱스만 서버 통지(`ServerSetCamYawIndex`)
- 사망 후 관전: 생존자 자동 추적 · 좌/우 대상 순환 · 시점만 이동
- 게임 상태는 안 바꿈

## 주요 변수·함수
| 이름 | 설명 | 멀티 이유 |
|---|---|---|
| `IA_*` 5종 | 입력 액션 (BP는 에셋만 지정) | |
| `CamYawSteps` / `SmoothCamYaw` | Q/E 누적 스텝 · 보간 현재각 | **로컬 전용** — 내 카메라는 서버가 몰라도 됨 |
| `LastSentCamYawIndex` | 마지막 전송 인덱스 | 변화 감지 — 기본값(0)이 복제 기본값과 일치해 무회전 시 **RPC 0회** |
| `SpectateTarget` (PlayerState) | 관전 대상 | 로컬 — "누굴 보는가"는 복제하지 않음(남의 관전 대상 동기화는 순수 낭비) |
| `bSpectateAxisLatched` | 좌/우 연속 입력 래치 | |
| `On*` 입력 핸들러 | 전달만 | 검증을 여기 두면 봇(다른 컨트롤러)과 경로가 갈라짐 |
| `GetCamYawIndex()` / `GetSnappedCamYaw()` | 스텝 → 인덱스/스냅각 | 공식은 `CameraYawSnap` — 복제 값과 같은 좌표계 보장 |
| `PushCamYawIndex()` | 바뀔 때만 전송 | 매 틱 전송이면 8인 기준 초당 수백 개 무의미 RPC |
| `ServerSetCamYawIndex(uint8)` (Server RPC) | PlayerState에 기록 | **표현 값 복제용** — 관전자가 내 폰을 볼 때의 각. 내 카메라 자체는 여전히 로컬이 굴림 |
| `PlayerTick` | 보간 + 사망 중 관전 갱신 | |
| `UpdateSpectateView()` / `CycleSpectateTarget` / `CollectSpectateCandidates` | 자동 추적·순환·후보 수집 | 전부 로컬 — 필요한 서버 정보는 이미 복제된 `bAlive`·`PlayerArray`뿐. 새 복제·RPC 0개로 구현된 관전 |
| `SetSpectateTarget()` | 블렌드 이동 + 대상 각 수신 | `SetViewTargetWithBlend` = 로컬 카메라 조작 — 서버 왕복 없음. 시작각만 복제된 `CamYawIndex`에서 |
| `HandleSpectateAxis(X)` | 사망 중 좌/우 재해석 | |
| `IsLocalPawnDead()` / `IsMatchEnded()` | 관전 조건 / 종료 후 고정 | 복제된 `LifeState`·`bMatchEnded`를 읽기만 |

## 왜
- **왜 전달만?** → 검증을 컨트롤러에 두면 봇(다른 컨트롤러)과 경로가 갈라짐
- **왜 90도 상수가 여기 없나?** → 단일 출처는 `CameraYawSnap`. 사본 = 관전 각과 갈라짐
- **왜 인덱스 바뀔 때만 RPC?** → 매 틱이면 8인 기준 초당 수백 개 무의미 RPC.
  기본값 일치로 무회전 플레이어는 RPC 0회
- **왜 이동 기준이 보간각 아닌 스냅각?** → 회전 중에도 WASD 기준이 안 흔들림
- **왜 관전이 전부 로컬?** → 시점은 각 클라 사정. 복제하면 남의 관전 대상까지 동기화하는
  낭비. 생존 여부만 기존 `bAlive`에서 읽음
- **왜 폰 안 건드림?** → UnPossess·관전 폰이면 부활·입력·HUD 캐시 전부 깨짐
- **왜 Spectating 상태 없음?** → `IsLocalPawnDead()` 하나로 충분. 복제 상태 추가 =
  `== Dead` 12군데 전부 수정 + 회귀 위험
- **왜 대상이 PlayerState?** → 폰은 바뀔 수 있고 PlayerState는 매치 내내 동일 + bAlive 출처
- **왜 좌/우 래치?** → Triggered는 매 프레임 — 래치 없으면 한 번에 전원 통과
- **왜 대상 각을 시작각으로만?** → "그 사람 시점 그대로" 시작하되 이후엔 관전자 소유(Q/E)

## 멀티 처리
**카메라·관전은 전부 로컬이고, 네트워크로 나가는 것은 표현값 1바이트뿐.**
`CamYawIndex`(0~3)를 바뀔 때만 서버에 올려 PlayerState로 복제 — 관전자가 나를 볼 때
쓰는 값이다. 관전 자체는 이미 복제되는 값들(`bAlive` 등)을 읽기만 해서 추가 통신이 0.
RPC 1: `ServerSetCamYawIndex(uint8)`

## 연결
[25-CameraYawSnap.md](../../Core/25-CameraYawSnap.md) · [23-CA3DCharacter.md](23-CA3DCharacter.md) · [34-CA3DPlayerState.md](../../Framework/34-CA3DPlayerState.md)

## Q&A
아직 없음
