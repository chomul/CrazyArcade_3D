# FExplosionResult (ExplosionTypes.h)

> `Source/CrazyArcade3D/Gameplay/Bomb/ExplosionTypes.h` · 평범한 struct (USTRUCT 아님)

폭발 계산의 결과 값 타입. `WaterCells`(물줄기가 지나는 셀) / `BrokenCells`(부서질 셀) /
`ChainedCells`(연쇄 유발되는 폭탄 셀) 3개 TArray.

## 역할

- 폭발 전파 계산의 **결과를 담는 값 타입**: 물줄기 셀 · 부서질 셀 · 연쇄 유발 셀.
- `Propagate`(계산)와 `ApplyExplosionCells`(적용) 사이를 오가는 유일한 운반체.

## 왜 이렇게 했는가

- **존재 이유 = `Propagate`를 순수 함수로 만들기 위한 반환 타입** — 순수 함수는
  "부작용 없이 결과만 반환"해야 하는데, 폭발의 결과는 세 종류 셀 목록이다.
  이걸 하나의 값으로 묶어야 "계산"과 "적용"이 분리된다.
- **왜 USTRUCT가 아닌가** — 리플렉션(BP 노출·복제)이 필요 없다. 폭발 결과는 서버 내부
  계산값이고, 네트워크로는 결과 중 필요한 목록(파괴 셀·물줄기 셀)만 각각 나간다.
  리플렉션 없는 struct가 더 가볍고 의도가 명확하다.
- **왜 Chained가 폭탄 포인터가 아니라 셀인가** — `Propagate`는 `FVoxelGrid`와 셀 목록만
  아는 순수 함수라 액터를 모른다. 셀 → 폭탄 해석(`FindBombAt`)은 적용 단계
  (`ProcessChainStep`)의 일이다. 순수 계산 계층에 액터 참조가 스며들지 않게 하는 경계.

## 연결
- 생산자: `Propagate`([16-ExplosionSubsystem.md](16-ExplosionSubsystem.md)) ·
  소비자: `ApplyExplosionCells`, 위험 프리뷰, 봇 위험 판단

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
