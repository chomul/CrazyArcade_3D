#include "Voxel/VoxelGrid.h"

// 배열을 InSize에 맞게 할당하고 전부 Empty로 초기화한다.
// 재호출 시에도 이전 내용을 버리고 전 칸 Empty로 리셋한다.
void FVoxelGrid::Init(FIntVector InSize)
{
	Size = InSize;
	Blocks.Init(static_cast<uint8>(EBlockType::Empty), Size.X * Size.Y * Size.Z);
}

// 그 칸 위에 서 있을 수 있는가 — Empty가 아니면 true.
bool FVoxelGrid::IsSolid(const FIntVector& C) const
{
	return Get(C) != EBlockType::Empty;
}

// 폭발 전파를 막는가 — Immortal이면 true.
// Destructible/Floor에서 전파를 멈출지는 룰과 함께 ExplosionSubsystem::Propagate가 판정한다.
bool FVoxelGrid::BlocksExplosion(const FIntVector& C) const
{
	return Get(C) == EBlockType::Immortal;
}
