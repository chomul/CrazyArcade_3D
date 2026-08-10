# UExplosionSubsystem

> `Source/CrazyArcade3D/Gameplay/Bomb/ExplosionSubsystem.h/.cpp` · UWorldSubsystem

세 가지 역할: ① static 순수 함수 `Propagate`(폭발 전파 계산) ② 서버 연쇄 스케줄링
(`RequestDetonate`/`ProcessChainStep`/`ServerApplyExplosionAt`) ③ 폭탄·아이템 셀 레지스트리.

## 왜 이렇게 했는가

- **`Propagate`가 static 순수 함수인 것이 불변식 2** — 부작용 없이 그리드를 읽어
  `FExplosionResult`만 반환. 이 하나로 따라오는 것:
  - 위험 프리뷰 데칼이 실폭발과 **같은 함수**를 호출 → 표시와 실제가 구조적으로 일치
  - 봇이 "여기 놓으면 어디까지 터지나"를 부작용 없이 조회
  - 유닛 테스트 가능
  - 서든데스가 `bFloorDestructible` 인자 하나로 "낙하만 바닥을 부순다"를 **무수정** 구현
- **왜 연쇄를 타이머로 단계 분산하나(`ChainStepDelay`)** — 폭탄 10개 연쇄를 한 프레임에
  처리하면 히치가 생기고, 시각적으로도 "촤르륵" 퍼지는 연출이 원작의 맛이다.
  단계마다 그리드 스냅샷을 새로 뜨므로 앞 단계의 파괴가 뒷 단계 전파에 반영된다.
- **왜 원점 셀을 WaterCells에 먼저 넣고 연쇄 판정에서 제외하나** — 자기 자신을 연쇄
  대상으로 잡으면 무한 재폭발한다.
- **`ApplyExplosionCells`가 별도 함수인 이유** — 폭탄 연쇄(`ProcessChainStep`)와 서든데스
  (`ServerApplyExplosionAt`)가 **같은 적용 본체**(파괴→FX→갇힘→아이템)를 타야 한다.
  따로 구현했으면 "폭탄으로 부순 블록과 서든데스로 부순 블록이 다르게 동작"이 조용히 생겼다.
- **아이템은 소멸 먼저, 노출 나중** — 물줄기에 맞은 기존 아이템을 먼저 태우고,
  그 다음 부서진 블록에서 새 아이템을 꺼낸다. 순서를 바꾸면 방금 나온 아이템이
  같은 폭발에 즉시 타 버린다.
- **레지스트리가 셀을 캐시하지 않는 이유** — `FindBombAt`은 매번 각 폭탄의 `GetCell()`을
  되묻는다. 킥으로 폭탄이 움직여도 재등록이 필요 없다 — Cell 갱신이 곧 레지스트리 갱신.
  캐시를 두면 "이동한 폭탄이 옛 셀에서 검색되는" 어긋남이 생긴다.
- **왜 서브시스템인가** — 월드당 1개, 액터 배치 불필요, 수명 자동 관리.
  단 액터가 아니라 RPC를 못 쓰므로 Multicast는 `AExplosionFXRelay`에 위임.

## 연결
- 결과 타입: [15-ExplosionTypes.md](15-ExplosionTypes.md) · 파괴 적용: [03-VoxelWorld.md](../../Voxel/03-VoxelWorld.md) ·
  FX: [19-ExplosionFXRelay.md](19-ExplosionFXRelay.md) · 갇힘: [24-StatusComponent.md](../Character/24-StatusComponent.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
