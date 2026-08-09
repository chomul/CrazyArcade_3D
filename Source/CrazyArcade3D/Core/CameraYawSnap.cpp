#include "Core/CameraYawSnap.h"

namespace CameraYawSnap
{
	uint8 StepsToIndex(int32 Steps)
	{
		// 음수 나머지 보정 — Q 를 계속 눌러 스텝이 음수로 내려가도 인덱스는 0~(NumSteps-1).
		// (C++ 의 % 는 음수 피연산자에서 음수를 돌려주므로 한 번 더 더한다.)
		return static_cast<uint8>(((Steps % NumSteps) + NumSteps) % NumSteps);
	}

	float IndexToYawDeg(uint8 Index)
	{
		// 복제로 넘어온 값이 범위를 벗어나 있어도(악의적 클라·버전 불일치) 여기서 접힌다.
		return static_cast<float>(Index % NumSteps) * StepDeg;
	}

	uint8 YawDegToIndex(float YawDeg)
	{
		// [0, 360) 정규화 후 반올림. 359.9 는 NumSteps 가 나오므로 StepsToIndex 가 다시 랩한다.
		// FRotator::ClampAxis 를 쓰지 않는 이유: UE5 의 FRotator 는 double 기반이라 float ↔ double
		// 을 오가게 되는데, 이 파일은 각도(도) float 만 다루는 순수 계산이라 그럴 이유가 없다.
		float Normalized = FMath::Fmod(YawDeg, 360.f);
		if (Normalized < 0.f)
		{
			Normalized += 360.f;
		}
		return StepsToIndex(FMath::RoundToInt(Normalized / StepDeg));
	}

	uint8 YawDegToIndexWithHysteresis(float YawDeg, uint8 CurrentIndex, float HysteresisDeg)
	{
		// 현재 칸의 중심에서 얼마나 벗어났는가. 원래 절반(StepDeg/2)을 넘으면 다음 칸이지만,
		// 히스테리시스만큼 더 벗어나야 실제로 넘긴다.
		const float MaxHysteresis = StepDeg * 0.5f - UE_KINDA_SMALL_NUMBER;
		const float Threshold = StepDeg * 0.5f + FMath::Clamp(HysteresisDeg, 0.f, MaxHysteresis);

		// FindDeltaAngleDegrees 로 ±180 랩을 처리한다 — 350도와 5도가 15도 차이임을 알아야
		// 0/7 인덱스 경계에서만 히스테리시스가 안 먹는 사고가 없다.
		const float Delta = FMath::Abs(FMath::FindDeltaAngleDegrees(IndexToYawDeg(CurrentIndex), YawDeg));
		if (Delta <= Threshold)
		{
			return static_cast<uint8>(CurrentIndex % NumSteps);
		}
		return YawDegToIndex(YawDeg);
	}
}
