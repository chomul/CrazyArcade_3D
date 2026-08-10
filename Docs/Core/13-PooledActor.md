# IPooledActor

> `Source/CrazyArcade3D/Core/PooledActor.h` · UINTERFACE (`CannotImplementInterfaceInBlueprint`)

풀 수명 콜백 계약: `OnAcquiredFromPool()` / `OnReleasedToPool()` 순수 가상 2개.

## 왜 이렇게 했는가

- **왜 인터페이스가 필요한가** — 풀은 액터를 파괴하지 않고 재사용한다. 액터 입장에서
  "다시 태어남"(Acquire)과 "잠듦"(Release) 시점에 자기 정리를 할 훅이 필요하다 —
  타이머 정지, FX 정지, 내부 상태 리셋. 풀이 이걸 대신할 수 없다(액터 내부를 모른다).
- **왜 BP 구현을 차단했나** — `Cast<IPooledActor>`가 네이티브 구현만 찾도록 고정.
  BP 구현을 허용하면 C++ 캐스트가 BP 구현체를 못 보고 지나쳐, "구현했는데 호출이 안 되는"
  미궁이 생긴다. BP에 로직 금지라는 프로젝트 규칙과도 일치.
- **미구현 시 ensure로 조기 발견** — `Acquire`/`Release`가 인터페이스 미구현 액터를 받으면
  `ensureMsgf` 실패. 계약 위반을 쓰는 순간 잡는다(조용히 넘어가면 풀 오염으로 나중에 터진다).
- **계약의 전제** — `OnAcquiredFromPool`: 위치·표시·컬리전·틱은 풀이 이미 복원했다.
  `OnReleasedToPool`: 타이머·FX·사운드 정지는 액터 책임. 이 분담이 흐려지면
  "풀에서 나온 액터가 옛 타이머로 터지는" 오염이 생긴다.

## 연결
- 풀: [14-PoolSubsystem.md](14-PoolSubsystem.md) · 구현체: WaterSegment·DangerDecal·
  PredictedBombVisual·SuddenDeathDropMarker

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
