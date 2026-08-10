# ACA3DPlayerController

> `Gameplay/Character/CA3DPlayerController.h/.cpp` · APlayerController — 클라 로컬 전용

## 역할
- 입력 바인딩(Enhanced Input) + 캐릭터로 전달
- 카메라 yaw 스텝 소유·보간 · 바뀐 인덱스만 서버 통지(`ServerSetCamYawIndex`)
- 사망 후 관전: 생존자 자동 추적 · 좌/우 대상 순환 · 시점만 이동
- 게임 상태는 안 바꿈

## 주요 변수·함수
| 이름 | 설명 |
|---|---|
| `IA_Move / IA_Jump / IA_PlaceBomb / IA_RotateCam / IA_UseNeedle` | 입력 액션 (BP에서 에셋만 지정) |
| `CamYawSteps` | Q/E 누적 스텝 (랩 안 함) |
| `SmoothCamYaw` | 보간 현재각 (시각 전용) |
| `LastSentCamYawIndex` | 마지막 전송 인덱스 — 변화 감지용 |
| `SpectateTarget` (PlayerState) | 관전 대상 — 폰이 아니라 PlayerState |
| `bSpectateAxisLatched` | 좌/우 연속 입력 래치 |
| `OnMove / OnJump* / OnPlaceBomb / OnUseNeedle / OnRotateCam` | 입력 핸들러 — 전달만 |
| `GetCamYawIndex()` / `GetSnappedCamYaw()` | 스텝 → 인덱스/스냅각 (CameraYawSnap 공식) |
| `PushCamYawIndex()` | 인덱스 바뀔 때만 RPC |
| `ServerSetCamYawIndex(uint8)` (RPC) | PlayerState.CamYawIndex에 기록 |
| `PlayerTick` | yaw 보간 + 사망 중 `UpdateSpectateView()` |
| `UpdateSpectateView()` | 대상 없음/사망 시 다음 생존자로 |
| `CycleSpectateTarget(±1)` / `CollectSpectateCandidates()` | 대상 순환 (PlayerArray 고정 순서·bAlive만) |
| `SetSpectateTarget()` | 블렌드 이동 + 대상 각을 시작각으로 수신 |
| `HandleSpectateAxis(X)` | 사망 중 좌/우 재해석 (래치 포함) |
| `IsLocalPawnDead()` / `IsMatchEnded()` | 관전 조건 / 종료 후 시점 고정 |

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

## 네트워크
RPC 1: `ServerSetCamYawIndex(uint8)`

## 연결
[25-CameraYawSnap.md](../../Core/25-CameraYawSnap.md) · [23-CA3DCharacter.md](23-CA3DCharacter.md) · [34-CA3DPlayerState.md](../../Framework/34-CA3DPlayerState.md)

## Q&A
아직 없음
