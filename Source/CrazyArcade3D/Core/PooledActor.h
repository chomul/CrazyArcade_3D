// IPooledActor — 풀링 대상 액터의 수명 콜백 계약 (Task 13).
//
// 풀링은 **클라 시각 요소 전용**이다 — 서버 권한 상태를 가진 액터(ABomb)는
// 풀링하지 않는다. 재사용 시 상태 오염 위험이 이득보다 크다 (설계서 2.8).
//
// Core 폴더는 아무것도 참조하지 않는다 — 이 파일은 엔진 헤더만 include 한다.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PooledActor.generated.h"

// 순수 가상 C++ 함수만 있는 네이티브 인터페이스 — BP 구현을 막아
// Cast<IPooledActor>가 항상 네이티브 구현을 찾도록 고정한다.
UINTERFACE(meta = (CannotImplementInterfaceInBlueprint))
class UPooledActor : public UInterface
{
	GENERATED_BODY()
};

// 풀링 대상 액터가 구현하는 수명 콜백.
// 풀에서 나올 때 초기화, 돌아갈 때 정리를 강제한다.
class IPooledActor
{
	GENERATED_BODY()

public:
	// 풀에서 꺼내질 때. 위치·표시·컬리전·틱은 풀이 이미 복원했다 —
	// 여기서는 상태 초기화·활성화(FX 재생 등)만 한다.
	virtual void OnAcquiredFromPool() = 0;

	// 풀로 돌아갈 때. ⚠️ 타이머·FX·사운드 정지 필수 —
	// 안 끄면 풀 안에서 계속 돌아 다음 사용자를 오염시킨다.
	virtual void OnReleasedToPool() = 0;
};
