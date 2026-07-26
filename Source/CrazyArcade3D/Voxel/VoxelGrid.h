#pragma once

#include "CoreMinimal.h"
#include "Voxel/VoxelTypes.h"

// 복셀 지형의 순수 데이터. 서버/클라 양쪽에서 동일하게 사용한다.
// UObject가 아니므로 값 복사·스택 생성이 자유롭다. 21×21×4 = 1,764바이트.
// 좌표↔월드 변환(FVector)은 셀 크기를 알아야 하므로 AVoxelWorld(Task 06) 소관 — 여기 두지 않는다.
struct FVoxelGrid
{
	FIntVector    Size = FIntVector(21, 21, 4); // 셀 개수 (X, Y, 층)
	TArray<uint8> Blocks;                       // Size.X*Y*Z 평탄화 배열, 1바이트/칸

	// 배열을 InSize에 맞게 할당하고 전부 Empty로 초기화한다.
	void Init(FIntVector InSize);

	// 좌표가 그리드 범위 안인가.
	FORCEINLINE bool IsValid(const FIntVector& C) const
	{
		return C.X >= 0 && C.X < Size.X
			&& C.Y >= 0 && C.Y < Size.Y
			&& C.Z >= 0 && C.Z < Size.Z;
	}

	// 3D 좌표 → 평탄화 인덱스. X 우선, 그 다음 Y, Z.
	FORCEINLINE int32 Index(const FIntVector& C) const
	{
		return C.X + C.Y * Size.X + C.Z * Size.X * Size.Y;
	}

	// 범위 밖은 Empty 반환 — 폭발 전파 루프의 경계 검사 부담을 없앤다.
	FORCEINLINE EBlockType Get(const FIntVector& C) const
	{
		return IsValid(C) ? static_cast<EBlockType>(Blocks[Index(C)]) : EBlockType::Empty;
	}

	// 범위 밖이면 무시.
	FORCEINLINE void Set(const FIntVector& C, EBlockType T)
	{
		if (IsValid(C))
		{
			Blocks[Index(C)] = static_cast<uint8>(T);
		}
	}

	// 그 칸 위에 서 있을 수 있는가 (Empty가 아니면 true).
	bool IsSolid(const FIntVector& C) const;

	// 폭발 전파를 막는가 (Immortal만 true — Destructible/Floor에서 멈출지는 Propagate 소관).
	bool BlocksExplosion(const FIntVector& C) const;
};
