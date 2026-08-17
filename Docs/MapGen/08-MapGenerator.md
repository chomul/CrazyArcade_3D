# IMapGenerator

> `MapGen/MapGenerator.h` · UINTERFACE

## 역할
- 생성기 계약: (시드·크기·룰셋) → (그리드·스폰 셀·아이템 배치)
- 결정론 요구를 계약으로 못박음 — 같은 입력 = 항상 같은 출력

## 주요 함수
| 이름 | 설명 |
|---|---|
| `Generate(Seed, Size, Rules, OutGrid, OutSpawns, OutItems)` | 유일한 함수. 성공 여부 bool 반환 — 실패 시 호출자가 폴백 |

## 왜
- **왜 인터페이스?** → 3주 일정 보험. 절차 생성기가 미완이어도 폴백으로 데모 가능.
  단 **다형성은 아직 미사용** — 호출부가 구체 타입으로 부른다 (아래 Q&A)
- **왜 결정론이 계약?** → 지형 동기화 전체가 "시드만 보내고 클라 재생성"에 걸려 있음
- **왜 Size를 호출자가?** → 크기는 서버 결정 값. 생성기는 순수 변환만
- **왜 스폰·아이템도 함께?** → 셋 다 결정론이어야 함. 따로 만들면 난수 소비 순서가 갈라짐

## 연결
구현: [09-ProcMapGenerator.md](09-ProcMapGenerator.md) · [10-FallbackMapGenerator.md](10-FallbackMapGenerator.md) · 호출: [03-VoxelWorld.md](../Voxel/03-VoxelWorld.md)

## Q&A
- **Q. 처음 생성하고 나면 쓸 일 없지 않나? 여러 군데서 쓰이니까 인터페이스로 한 건가?** →
  "여러 군데"는 맞는데 **인터페이스 때문은 아님.** `Generate` 호출 23곳 중 **게임 코드는 2곳뿐**
  (`VoxelWorld::InitGridFromSeed` — Proc 시도 → 실패 시 Fallback, 같은 한 경로)이고
  **나머지 21곳이 전부 테스트**다. 생성기는 "맵 한 장 만드는 함수"가 아니라 **테스트 전체의
  픽스처 공장** — 그리드가 필요한 테스트가 손으로 블록을 깔지 않고 여기를 부른다:
  생성기 자체 검증(`ProcMapGeneratorTests` 8 · `FallbackMapGeneratorTests` 3) · 생성 결과를
  그대로 검증기 입력으로(`MapValidatorTests`) · 이동 규칙 픽스처(`VoxelMovementTests`) ·
  아이템 배치 결정론(`ItemPickupTests` 5) · `AVoxelWorld` 대조용 기준 그리드(`VoxelWorldTests`).
  그리고 그 21곳이 전부 **구체 타입**으로 부른다
- **Q. 그럼 인터페이스는 실제로 뭘 하고 있나?** → **가상 디스패치는 0회.**
  `IMapGenerator*` 타입의 변수·파라미터·멤버가 코드에 하나도 없다. 실제로 번 건 둘:
  ① **두 구현체 시그니처를 컴파일 타임에 묶음** — 폴백 전환이 "인자를 그대로 넘겨 다시
  부르기"라 Proc에 파라미터가 하나 늘면 성립하지 않는데 순수 가상 함수가 그걸 막는다
  (Proc은 Task 22에서 나중에 붙었으니 공짜로 얻은 게 아님) ② **결정론 계약을 적을 자리** —
  불변식 4(정수 + `FRandomStream` 만)와 `Size`를 호출자가 넘기는 이유가 헤더 주석에 있고
  두 구현체가 그걸 상속. 한 줄: **다형성이 아니라 계약을 샀다**
- **Q. 그러면 과설계인가?** → **`UINTERFACE`까지는 과했다.** 필요한 건 시그니처 강제 +
  계약 문서 둘뿐이라 순수 추상 클래스로 충분하고, `UINTERFACE`가 더 주는 것
  (`Cast<IMapGenerator>` · BP 노출 · UHT 리플렉션)은 전부 미사용. `UObject` 파생 자체는 정당 —
  `LastAttemptCount` 같은 인스턴스 상태를 테스트가 읽는다. **값을 하기 시작하는 지점**은
  생성기 선택이 하드코딩 `if/else`를 벗어날 때 — 룰셋에 `TSubclassOf<UObject> MapGeneratorClass` 를
  두고 `Cast<IMapGenerator>` 로 받는 형태. 마침 `VoxelWorldTests`가 `VoxelWorld`의 **선택 로직을
  복제**하고 있어(Proc 실패 시 Fallback) "어느 생성기를 쓰나"가 두 군데인데, `PickGenerator()`
  하나로 뽑으면 테스트와 게임이 같은 경로를 타면서(불변식 1) 인터페이스가 처음으로 제 값을 한다.
  지금은 **보험은 들었는데 청구를 안 한 상태**
