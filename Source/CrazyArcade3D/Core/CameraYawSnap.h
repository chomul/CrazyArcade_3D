#pragma once

#include "CoreMinimal.h"

// 45도 스냅 카메라 yaw 의 **인덱스 ↔ 각도 변환 단일 출처**.
//
// 이 파일이 존재하는 이유는 Voxel/VoxelMovement.h 와 똑같다: 같은 공식이 두 벌 있으면 반드시
// 갈라진다. 스냅 공식을 쓰는 곳이 이제 셋이다 —
//   ① ACA3DPlayerController — Q/E 로 고른 스텝을 각도로 (내 카메라)
//   ② ACA3DCharacter::GetViewRotation — 복제된 인덱스를 각도로 (남이 나를 볼 때의 각)
//   ③ ABotController — 봇의 이동 방향을 인덱스로 (봇에게는 "보는 시점"이 없어 방향으로 대신한다)
// 한쪽만 바뀌면 "내가 보는 각"과 "관전자가 나를 볼 때의 각"이 어긋나는데, 그건 **관전에
// 들어가야만 보이는** 버그라 발견이 아주 늦다.
//
// 순수 함수 · 의존 없음(Core). 게임 규칙도 룰셋도 폰도 모르고 각도(도)만 다룬다 —
// 그래서 Gameplay·AI·Framework 어디서 불러도 폴더 의존 규칙을 어기지 않는다.
namespace CameraYawSnap
{
	// 8방향 고정 (GDD 5장) — **튜닝 값이 아니라 구조 상수**다. 복제되는 인덱스가
	// uint8 0~7 인 것도, 스텝이 45도인 것도 전부 이 값 하나에서 나온다.
	// 룰셋에 두지 않는 이유: 런타임에 바뀌면 이미 복제된 인덱스의 의미가 통째로 달라진다.
	inline constexpr int32 NumSteps = 8;
	inline constexpr float StepDeg = 360.f / static_cast<float>(NumSteps); // 45

	// 누적 스텝 → 인덱스 0~7. 컨트롤러는 스텝을 랩하지 않고 계속 더하거나 빼므로
	// (보간이 FRotator 정규화로 최단 경로를 택한다) 음수도 들어온다.
	uint8 StepsToIndex(int32 Steps);

	// 인덱스 → 각도(도). 0, 45, … 315.
	float IndexToYawDeg(uint8 Index);

	// 임의 각도 → 가장 가까운 인덱스. IndexToYawDeg 와 정확히 왕복한다.
	uint8 YawDegToIndex(float YawDeg);

	// 히스테리시스판 — 현재 인덱스에서 벗어나려면 경계(22.5도)를 HysteresisDeg 만큼 **더**
	// 넘어야 한다. 봇처럼 목표각이 연속적으로 미세하게 흔들리는 입력에 쓴다:
	// 히스테리시스가 없으면 경계에 걸친 채 떨리는 방향이 매 프레임 카메라를 45도씩 왕복시킨다.
	//
	// HysteresisDeg 는 내부에서 한 스텝의 절반 미만으로 클램프된다 — 그보다 크면 정확히
	// 한 칸 옆(45도)조차 넘어가지 못해 카메라가 특정 각에 붙어버린다.
	uint8 YawDegToIndexWithHysteresis(float YawDeg, uint8 CurrentIndex, float HysteresisDeg);
}
