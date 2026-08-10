# FExplosionResult (ExplosionTypes.h)

> `Gameplay/Bomb/ExplosionTypes.h` · 평범한 struct — `WaterCells / BrokenCells / ChainedCells`

## 역할
- 폭발 계산 결과의 값 타입 — `Propagate`(계산)와 적용 사이의 운반체

## 왜
- **왜 존재?** → 순수 함수는 결과만 반환해야 함. 세 셀 목록을 하나로 묶어야
  계산/적용이 분리됨
- **왜 USTRUCT 아님?** → BP 노출·복제 불필요. 서버 내부 계산값
- **왜 Chained가 폭탄이 아니라 셀?** → `Propagate`는 액터를 모르는 순수 함수.
  셀→폭탄 해석은 적용 단계(`FindBombAt`) 소관

## 연결
생산: [16-ExplosionSubsystem.md](16-ExplosionSubsystem.md) · 소비: 적용부·프리뷰·봇

## Q&A
아직 없음
