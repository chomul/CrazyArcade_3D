# FVoxelGrid

> `Voxel/VoxelGrid.h` · 평범한 struct (UObject 아님) — `Size` + `TArray<uint8> Blocks`

## 역할
- 지형 상태 보관 — **매치 내내 지형의 진실.** 블록은 영원히 객체로 안 만들어짐
- 셀 API: `Get / Set / IsSolid / BlocksExplosion / IsValid / Index`
- 안 하는 것: 렌더 · 복제 · 좌표 변환 · "누가 부쉈나"

## 주요 변수·함수
| 이름 | 설명 |
|---|---|
| `Size` (FIntVector) | 그리드 크기 (기본 21×21×4) |
| `Blocks` (TArray&lt;uint8&gt;) | 셀 배열 — X→Y→Z 평탄화. 이것이 지형 전부 |
| `Init(Size)` | 크기 지정 + 전부 Empty로 초기화 |
| `IsValid(Cell)` | 좌표가 범위 안인가 |
| `Index(Cell)` | 3D 좌표 → 1D 인덱스 (`X + Y*W + Z*W*H`) |
| `Get(Cell)` | 셀 타입 조회. **범위 밖 = Empty** |
| `Set(Cell, Type)` | 셀 쓰기 — 파괴 = `Set(Cell, Empty)` |
| `IsSolid(Cell)` | Empty가 아닌가 (발판·충돌 판정용) |
| `BlocksExplosion(Cell)` | 폭발을 막는가 (**Immortal만 true**) |

## 왜
- **왜 UObject 아님?** → 값 복사 자유 + 순수 함수 인자(`const FVoxelGrid&`) 가능.
  GC·복제·월드가 붙으면 둘 다 불가. 엔진 결합은 소유자 `AVoxelWorld` 담당
- **왜 셀이 uint8 하나?** → 필요한 정보가 타입 1바이트뿐. HP 없음(한 방),
  "누가 부쉈나"는 **있으면 안 됨**(의존 규칙), 내장 아이템은 별도 배열
- **왜 배열이 빠른가?** → 2,646셀 = 2.6KB = L1 캐시. 봇 BFS·Propagate·flood fill이
  매 틱 수천 셀 조회 — 객체 배열이면 셀마다 포인터 추적
- **왜 1D 평탄화?** → 할당 1번·연속 메모리. 중첩 TArray는 UPROPERTY 불가 +
  순회 순서를 코드에 명시(결정론, 불변식 4)
- **덤** → 연속 바이트라 CRC 해시 검증 공짜(1주차 게이트 해시 30건 일치)
- **dense/sparse 분할** → 많고 단순한 것(지형)=배열, 적고 복잡한 것(폭탄·아이템)=액터

## 연결
소유: [03-VoxelWorld.md](03-VoxelWorld.md) · 읽는 쪽: Propagate·MapValidator·VoxelMove·봇 BFS

## Q&A
- **Q. 블록 만들기 전의 배열?** → 아니. "만들기 전"이 아니라 **영원히 이게 실체.**
  파괴 = `Set(Cell, Empty)` 한 줄. HISM은 이 배열을 보고 그린 투영.
  모든 판정이 화면이 아니라 이 배열을 직접 읽음
- **Q. 보통 셀 객체 배열 아닌가?** → 셀에 개별 상태·행동이 많으면 그 방식이 맞음.
  여기는 셀 정보가 1바이트 + 대량 조회 매 틱 → 연속 배열 압승
