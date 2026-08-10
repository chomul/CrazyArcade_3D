# CameraYawSnap (namespace)

> `Core/CameraYawSnap.h/.cpp` · 순수 함수 + 구조 상수 — `NumSteps=4` · `StepDeg=90`

## 역할
- 스냅 각 변환 공식의 단일 출처: 스텝 ↔ 인덱스(0~3) ↔ 각도, 임의 각 → 인덱스(+히스테리시스)

## 주요 변수·함수
| 이름 | 설명 |
|---|---|
| `NumSteps=4` / `StepDeg=90` | 구조 상수 — 모든 파생값의 뿌리 |
| `StepsToIndex(int32)` | 누적 스텝 → 0~3 (음수 보정 `((s%N)+N)%N`) |
| `IndexToYawDeg(uint8)` | 인덱스 → 각도 (`% NumSteps`로 방어적 접기) |
| `YawDegToIndex(float)` | 임의 각 → 가장 가까운 인덱스 |
| `YawDegToIndexWithHysteresis(yaw, prev, hys)` | 경계 진동 방지 버전 (봇용, ±180 랩 처리) |

## 왜
- **왜 단일 출처?** → 사용처 3곳(Q/E·GetViewRotation·봇). 한쪽만 바뀌면
  **관전에 들어가야만 보이는** 각도 어긋남 — 발견이 가장 늦은 부류의 버그
- **왜 룰셋이 아니라 구조 상수?** → 스텝 수가 바뀌면 이미 복제된 인덱스의 **의미**가
  바뀜. 실증: 45→90도 변경이 `NumSteps` 8→4 하나로 끝남 (파생값 전부 자동)
- **왜 `Index % NumSteps` 접기?** → 악의적 클라·버전 불일치 방어
- **왜 `((s%N)+N)%N`?** → C++ %는 음수 유지. Q 연타 음수 스텝 보정
- **왜 히스테리시스?** → 봇 이동 방향이 경계에 걸치면 매 틱 튐. 여유각 12도
- **왜 ClampAxis 안 씀?** → UE5 FRotator는 double — float 왕복 회피

## 연결
[26-CA3DPlayerController.md](../Gameplay/Character/26-CA3DPlayerController.md) · [23-CA3DCharacter.md](../Gameplay/Character/23-CA3DCharacter.md) · [37-BotController.md](../AI/37-BotController.md)

## Q&A
아직 없음
