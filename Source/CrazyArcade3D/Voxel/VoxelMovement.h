#pragma once

#include "CoreMinimal.h"

struct FVoxelGrid;

// 격자 위 "한 걸음"의 기하를 정의하는 순수 함수 모음.
//
// **이 파일이 존재하는 이유는 하나다: 이동 규칙이 두 벌 있으면 반드시 갈라지기 때문이다.**
// 맵 검증기(FMapValidator)는 "이 맵은 걸어서 다 돌 수 있다"고 판정하고, 봇(ABotController)은
// 그 판정을 믿고 경로를 찾는다. 두 곳이 각자 인접 규칙을 들고 있으면 어느 한쪽만 고쳐졌을 때
// **검증은 통과했는데 봇은 못 가는 지형**이 조용히 생산된다 (실제로 그랬다 — 봇의 BFS 가
// dz ∈ {0,±1} 고정이라 검증기가 허용하는 2칸 낙하를 계획하지 못했다).
//
// 여기 있는 것은 게임 규칙이 아니라 **격자 기하**다 — 폭탄도 아이템도 캐릭터도 모른다.
// 그래서 Voxel 에 두어도 폴더 의존 규칙(`Voxel ──▶ 없음`)을 어기지 않는다.
// VoxelRayCast.h 가 같은 성격의 선례다 (월드 좌표도 CellSize 도 모르는 순수 격자 함수).
//
// ⚠️ **정수 연산과 고정 순서 루프만.** 검증기가 리롤 여부를 이 판정으로 가르므로 결과가
// 흔들리면 같은 시드가 실행마다 다른 맵이 된다 (불변식 4). float 비교·FMath::Rand·
// TMap/TSet 순회 금지.
namespace VoxelMove
{
	// 점프로 오를 수 있는 최대 높이(칸). **이 게임 이동 규칙의 유일한 출처.**
	//
	// 1 인 근거: 점프 정점이 (1, 2) 열린 구간에 클램프돼 있다 (UCA3DRuleSet::JumpApexCellFactor,
	// 기본 1.4) — 1칸은 오르고 2칸은 못 오른다. 룰셋 값이 아니라 상수인 이유는 두 가지다:
	//   ① Voxel 은 Framework(룰셋)를 참조할 수 없다 (폴더 의존 규칙).
	//   ② 점프 높이는 "튜닝 값"이 아니라 **맵 생성·검증·봇이 공유하는 계약**이다. 런타임에
	//      바뀌면 이미 검증을 통과한 맵의 도달성 판정이 뒤집힌다.
	// JumpApexCellFactor 의 클램프 구간을 넓히려면 여기도 같이 바꿔야 한다 — 그때 이 주석이
	// 두 값이 짝이라는 사실을 알려 준다.
	constexpr int32 MaxClimbCells = 1;

	// 낙하에는 상한이 없다. 아래로는 얼마든지 떨어지고, **처음 만난 발판에서 멈춘다.**
	// (상수로 두지 않은 이유: 상한이 없다는 것 자체가 규칙이라 값이 없다.)

	// 이동 주체의 물리적 제약. 기본값은 **순수 격자 기하** — 검증기가 쓰는 값이다.
	//
	// 봇은 여기서 bRequireHeadroom 만 켠다. 캡슐 높이(176)가 셀(100)보다 커서 1칸 높이 틈에는
	// 몸이 안 들어가기 때문이다. 검증기가 이걸 켜지 않는 이유는 **맵 생성 시점에는 차이가
	// 없기 때문**이다: 생성기는 기둥을 아래에서 위로 연속으로 채우므로(웨딩케이크 인셋·평지
	// 기둥 격자) 오버행 자체가 만들어지지 않는다. 1칸 틈은 폭발이 기둥 중간을 뚫은 **런타임에만**
	// 생기고, 그때는 검증기가 돌지 않는다. 그래서 두 모델은 검증기가 도는 순간에는 정확히 같고,
	// 봇만 런타임의 물리 제약을 하나 더 얹는다 — 규칙이 갈라진 것이 아니라 조건이 하나 더 붙은 것이다.
	struct FMoveCaps
	{
		// 머리 위 칸(Cell + Z1)도 비어 있어야 설 수 있다고 볼 것인가.
		bool bRequireHeadroom = false;
	};

	// 수평 인접 4방향 — **고정 순서**. 이 순서가 BFS 확장 순서를 정하고, 확장 순서가 곧
	// 경로 선택이다. 배열을 바꾸면 같은 지형에서 다른 길이 나온다 (불변식 4 준용).
	extern CRAZYARCADE3D_API const FIntVector PlanarDirs[4];

	// 캐릭터가 "설 수 있는 칸"인가 — 그 칸이 비었고 **바로 아래가 solid** 여야 한다.
	// 발판 위 공중 칸이 곧 플레이 공간이다 (GDD 2.1 "발판만이 안전하다").
	// z=0 은 아래가 없으므로 설 수 없다.
	CRAZYARCADE3D_API bool IsStandable(const FVoxelGrid& Grid, const FIntVector& Cell,
	                                   const FMoveCaps& Caps = FMoveCaps());

	// From 에서 Dir 방향 기둥으로 한 걸음 옮겼을 때 **실제로 착지하는 칸**을 찾는다.
	//
	// 위에서부터 훑는다: From.Z + MaxClimbCells 가 오르기 상한, 0 이 바닥. 처음 만난 발판에서
	// 멈추는 것이 핵심이다 — 그게 실제 착지 칸이고, 그 아래는 이 발판에 막혀 갈 수 없다.
	// 계속 훑으면 벽 안쪽 공간까지 도달 가능으로 세어 고립 구역 검사가 무력해진다.
	// 도달할 칸이 없으면(그 방향이 통째로 막혔으면) false.
	CRAZYARCADE3D_API bool FindLandingInColumn(const FVoxelGrid& Grid, const FIntVector& From,
	                                            const FIntVector& Dir, FIntVector& OutCell,
	                                            const FMoveCaps& Caps = FMoveCaps());

	// From 에서 한 걸음에 갈 수 있는 칸 전부 (방향당 최대 1칸이므로 최대 4개).
	// Out 은 Reset 후 채운다. 순서는 PlanarDirs 순서 그대로 — 결정론.
	CRAZYARCADE3D_API void GatherReachableNeighbors(const FVoxelGrid& Grid, const FIntVector& From,
	                                                 TArray<FIntVector>& Out,
	                                                 const FMoveCaps& Caps = FMoveCaps());

	// From 에서 한 걸음 나갈 수 있는 **방향의 수** (0~4).
	// 같은 방향에서 여러 높이가 나와도 1로 센다 — 폭발은 방향으로 퍼지므로
	// "몇 방향으로 도망칠 수 있나"가 실제 생존 조건이다 (검증기 ③ 탈출로).
	CRAZYARCADE3D_API int32 CountEscapeDirections(const FVoxelGrid& Grid, const FIntVector& From,
	                                               const FMoveCaps& Caps = FMoveCaps());
}
