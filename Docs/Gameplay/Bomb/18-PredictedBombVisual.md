# APredictedBombVisual

> `Gameplay/Bomb/PredictedBombVisual.h/.cpp` · AActor · bReplicates=false · IPooledActor

## 역할
- 설치 즉시 보이는 임시 그림 — 서버 확정까지의 RTT를 시각으로 메움
- 진짜가 오면 회수, 거부되면 삭제. 그 외의 일 없음

## 왜
- **왜 로직 0?(불변식 3)** → 타이머·판정 없음 = 되돌릴 상태 없음 = 거부 시 지우면 끝.
  예측 넷코드의 어려움(되감기)을 "되감을 것을 안 만들어서" 제거
- **왜 존재?** → 입력→확정 왕복 동안 아무것도 안 보이면 조작감 사망
- **왜 Cell이 매칭 키?** → ID 발급·왕복 없이 "같은 셀에 진짜 도착 = 내 예측 확정"으로 충분
- **왜 풀링?** → 비복제 순수 시각 + 고빈도 = 풀링 최적
- **회전 틱은 위반 아님?** → 판정 무관 시각 회전뿐. 테스트가 "틱 100회, 회전 외 변화 0" 검사
- **리슨 호스트는 예측 생략** → 서버에서 만들면 진짜와 둘 다 뜸

## 연결
생성·회수: [23-CA3DCharacter.md](../Character/23-CA3DCharacter.md) · 교체 상대: [17-Bomb.md](17-Bomb.md) · 풀: [14-PoolSubsystem.md](../../Core/14-PoolSubsystem.md)

## Q&A
아직 없음
