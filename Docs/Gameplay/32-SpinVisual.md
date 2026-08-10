# CA3DSpin (SpinVisual.h)

> `Gameplay/SpinVisual.h` · namespace 헬퍼

## 역할
- 시각 컴포넌트 제자리 회전: `ResolveDegreesPerSecond`(룰셋) + `ApplyYaw`
- 폭탄·예측 폭탄·아이템의 회전 규칙 단일 출처

## 왜
- **왜 셋이 공유?** → 속도가 갈리면 예측→진짜 교체가 **각도 점프**로 보임
- **왜 판정 컴포넌트는 안 돌리나?** → 컬리전이 돌면 판정이 프레임마다 미세 변동
- **왜 데디·속도0 등 no-op?** → 호출부가 조건 검사 안 해도 되게 헬퍼가 삼킴
- **왜 namespace?** → 상태 없음 — UObject 비용 불필요

## 연결
[17-Bomb.md](Bomb/17-Bomb.md) · [18-PredictedBombVisual.md](Bomb/18-PredictedBombVisual.md) · [29-ItemPickup.md](Item/29-ItemPickup.md)

## Q&A
아직 없음
