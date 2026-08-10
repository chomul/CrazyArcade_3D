# USuddenDeathSubsystem (+ ASuddenDeathRelay · ASuddenDeathDropMarker)

> `Source/CrazyArcade3D/Gameplay/SuddenDeath/SuddenDeathSubsystem.h/.cpp` · UWorldSubsystem + AInfo + 마커 액터
> ⚠️ 한 파일에 3클래스 — 1 Task = 1 Class 관례의 예외 (한 기능의 세 조각)

서든데스(150초 이후): 서버가 주기적으로 낙하 셀을 뽑아 예고(1.5초) 후 그 자리에 폭발을 일으킨다.
서든데스 낙하만 바닥을 부순다 — 초반은 발판 보장, 후반부터 구멍이 뚫린다.

## 왜 이렇게 했는가

- **⭐ `Propagate`를 한 줄도 안 고쳤다** — 불변식 2 덕에 `bFloorDestructible`이 이미
  인자였다. 서든데스는 자기 룰셋 값(`bSuddenDeathDestroysFloor=true`)을 넘기기만 한다.
  순수 함수로 둔 대가를 회수한 대표 사례.
- **폭발 적용부를 폭탄과 공유(`ServerApplyExplosionAt` → `ApplyExplosionCells`)** —
  따로 구현했으면 "폭탄으로 부순 블록과 서든데스로 부순 블록이 다르게 동작"하는 어긋남이
  조용히 생겼을 자리. 연쇄 유발은 적용을 끝낸 **뒤** — 순서가 뒤집히면 그 단계가 아직
  안 부서진 그리드를 읽는다.
- **웨이브가 배열인 이유** — `DropInterval`(1.0) < `DropWarningTime`(1.5)이라 예고 중인
  웨이브가 동시에 여러 개다. 핸들을 하나만 두면 뒤 웨이브가 앞 웨이브를 덮어써
  앞 웨이브가 영영 안 떨어진다(명세 변경으로 잡은 버그).
- **낙하 셀을 예고 시점에 확정해 보관** — 만료 시 재추첨하면 마커와 실제 낙하가 어긋나
  "예고를 보고 피한다"는 규칙 자체가 무너진다.
- **난수가 `FMath::Rand()`인데 괜찮은 이유** — 낙하 위치는 서버 단독 결정 후 Multicast로
  통지된다. 클라가 재현할 필요가 없으므로 결정론 제약(불변식 4)이 적용되지 않는다.
- **`ASuddenDeathRelay`가 따로 있는 이유** — UHT는 RPC를 액터에만 허용, 서브시스템은
  액터가 아니다(`AExplosionFXRelay`와 같은 사정). 경고 큐 재생이 마커 클래스 확인보다
  **위**에 있다 — 마커 에셋이 없어도 경고음은 나야 한다.
- **낙사 원인은 시각(時刻)으로 판정** — `bSuddenDeathActive`면 `SuddenDeath`, 아니면 `Fall`.
  "누가 이 구멍을 냈나"를 추적하려면 `FVoxelGrid`가 파괴 원인을 알아야 하고 그 순간
  Voxel 독립성이 무너진다. 통계용 분류에 그 비용을 치르지 않는다.
- **`ServerStop`/`Deinitialize`가 예고 중 웨이브까지 전부 해제** — 결과 화면 뒤에
  블록이 떨어지면 안 된다.
- **마커 타입이 `ASuddenDeathDropMarker`(AActor 아님)인 이유** — `UPoolSubsystem::Acquire`가
  `IPooledActor` 미구현이면 ensure로 실패한다. 순수 AActor면 낙하마다 터진다.

## 연결
- 폭발 공유: [16-ExplosionSubsystem.md](../Bomb/16-ExplosionSubsystem.md) · 시작/정지: [35-CA3DGameMode.md](../../Framework/35-CA3DGameMode.md) ·
  HUD 경고: [39-MatchWidget.md](../../UI/39-MatchWidget.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
