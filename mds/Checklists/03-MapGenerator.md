# Checklist 03 — MapGenerator (인터페이스)

> 대응 Task: `mds/Tasks/03-MapGenerator.md`
> 순수 인터페이스 — PIE 검증 대상 없음. 컴파일·정적 검증만으로 완료 가능.

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] `UMapGenerator`(UINTERFACE) + `IMapGenerator` 쌍 구조
- [ ] `Generate` 시그니처가 설계서 2.7과 일치 (Seed, Rules, OutGrid, OutSpawns, OutItems)
- [ ] 결정론 계약이 주석으로 명시됨 (같은 Seed+Rules ⇒ 같은 출력, FRandomStream·정수만)
- [ ] `ItemTypes.h`에 `EItemType` 5종(풍선·물약·롤러·니들·킥) + `FItemPlacement`
- [ ] `ItemTypes.h`가 다른 Gameplay 헤더를 include하지 않는다 (enum/struct만)
- [ ] `MapGenerator.h`는 전방 선언 우선 — 무거운 include 없음
