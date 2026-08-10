# AWaterSegment

> `Source/CrazyArcade3D/Gameplay/Bomb/WaterSegment.h/.cpp` · AActor, 비복제, IPooledActor

물줄기 1칸의 시각 표현. `StartLinger(Seconds)` 후 스스로 풀에 반납.

## 왜 이렇게 했는가

- **왜 판정이 없나** — 갇힘 판정은 서버가 `ApplyExplosionCells`에서 발밑 셀로 이미 끝냈다.
  물줄기 액터에 오버랩 판정을 넣으면 판정이 두 벌이 된다(셀 판정 vs 물리 판정 —
  반드시 어긋난다). 이 액터는 "판정 결과를 보여주는 그림"일 뿐이다.
- **왜 복제하지 않나** — 물줄기 셀 목록이 `MulticastWaterCells`로 가고 각 클라가 로컬
  스폰한다. 셀당 액터를 복제하면 폭발마다 수십 개 액터 복제 비용 — 목록 하나면 충분하다.
- **왜 풀링인가** — 폭발마다 범위×방향만큼 생겼다 사라진다. 가장 빈번한 시각 액터라
  풀링 이득이 가장 큰 곳(설계서의 "물줄기 200개 스트레스" 검증 항목).
- **스스로 반납하는 이유** — 수명이 "표시 시간 경과" 하나뿐이라 소유자가 따로 관리할
  이유가 없다. `OnReleasedToPool`에서 타이머 정지(풀 계약).

## 연결
- 스폰: [19-ExplosionFXRelay.md](19-ExplosionFXRelay.md) · 풀: [14-PoolSubsystem.md](../../Core/14-PoolSubsystem.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
