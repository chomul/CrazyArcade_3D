# ADangerDecal

> `Gameplay/Bomb/DangerDecal.h/.cpp` · AActor · 비복제 · IPooledActor

## 역할
- 폭발 예정 셀을 바닥에 표시 — "6방향 3D 폭발"의 직관성 장치
- 폭탄이 킥으로 움직이면 따라감(`OnRep_Cell`이 다시 깖)

## 주요 함수
| 이름 | 설명 |
|---|---|
| `SetCellSize(float)` | 데칼 크기를 셀 크기에 맞춤 |
| `OnAcquiredFromPool / OnReleasedToPool` | 풀 계약 구현 |

## 왜
- **왜 실제와 절대 안 어긋나나?** → 셀 목록을 실폭발과 같은 `Propagate`로 계산.
  프리뷰 전용 식이 따로 있으면 언젠가 갈라짐
- **왜 전송 없음?** → 복제된 Cell·Range + 자기 그리드로 각 클라가 같은 결과
- **왜 풀링?** → 설치·이동마다 깔았다 걷음. 순수 시각·비복제

## 연결
계산: [16-ExplosionSubsystem.md](16-ExplosionSubsystem.md) · 소유: [17-Bomb.md](17-Bomb.md)

## Q&A
아직 없음
