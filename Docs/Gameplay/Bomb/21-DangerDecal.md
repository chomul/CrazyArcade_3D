# ADangerDecal

> `Source/CrazyArcade3D/Gameplay/Bomb/DangerDecal.h/.cpp` · AActor, 비복제, IPooledActor

위험 구역 프리뷰 데칼 1칸. 폭탄이 터지면 어디까지 물줄기가 갈지 바닥에 표시.

## 역할

- 폭탄이 터질 때 물줄기가 지날 **셀들을 바닥에 미리 표시**한다 — "6방향 3D 폭발"을
  직관적으로 만드는 장치(GDD의 직관성 리스크 대응).
- 폭탄이 킥으로 이동하면 따라간다(`OnRep_Cell`이 다시 깐다).

## 왜 이렇게 했는가

- **표시가 실제와 어긋날 수 없는 구조** — 데칼을 깔 셀 목록은 실폭발과 **같은**
  `Propagate`를 로컬에서 불러 얻는다(불변식 2의 회수처). 프리뷰 전용 계산식이
  따로 있으면 언젠가 실폭발과 달라진다.
- **왜 전송이 없나** — 위험 프리뷰는 권한 매트릭스에서 "클라 로컬 계산·전송 없음"이다.
  각 클라가 복제된 `Cell`·`Range`와 자기 그리드로 같은 결과를 얻는다.
- **킥 대응** — `ABomb::OnRep_Cell`이 데칼을 다시 깐다. 폭탄이 굴러가면 위험 표시가
  따라간다 — `Cell` 복제 하나로 메시(bReplicateMovement)와 데칼이 모두 정합.
- **왜 풀링인가** — 폭탄 설치·이동마다 범위만큼 깔았다 걷는다. 순수 시각·비복제라 풀링 안전.

## 연결
- 계산 출처: `Propagate`([16-ExplosionSubsystem.md](16-ExplosionSubsystem.md)) · 소유: [17-Bomb.md](17-Bomb.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
