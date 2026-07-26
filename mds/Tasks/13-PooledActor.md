# Task 13 — PooledActor (인터페이스)

> 선행: 없음 · 후행: Task 14(풀이 호출), 17/23(구현체)
> 체크리스트: `mds/Checklists/13-PooledActor.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `IPooledActor` (`UPooledActor` UINTERFACE 쌍) |
| 부모 클래스 | `UInterface` |
| 역할 | 풀링 대상 액터의 수명 콜백 계약. 풀에서 나올 때 초기화, 돌아갈 때 정리를 강제한다 |

## 생성 파일

- `Source/CrazyArcade3D/Core/PooledActor.h`

## 구현 명세

```cpp
// PooledActor.h
UINTERFACE() class UPooledActor : public UInterface { GENERATED_BODY() };

// 풀링 대상 액터가 구현하는 수명 콜백.
// 풀링은 "클라 시각 요소 전용" — 서버 권한 상태를 가진 액터(ABomb)는 풀링하지 않는다.
class IPooledActor
{
    GENERATED_BODY()
public:
    // 풀에서 꺼내질 때. 위치는 이미 세팅됨 — 상태 초기화·활성화만.
    virtual void OnAcquiredFromPool() = 0;

    // 풀로 돌아갈 때. ⚠️ 타이머·FX·사운드 정지 필수 —
    // 안 끄면 풀 안에서 계속 돌아 다음 사용자를 오염시킨다.
    virtual void OnReleasedToPool() = 0;
};
```

**주의**: `Core` 폴더는 아무것도 참조하지 않는다.

## 검증 원칙

- 공통 원칙. 인터페이스 컴파일 + 의존 규칙 확인이 전부다.

## 응답 원칙

- 공통 원칙. 순수 인터페이스 Task — "컴파일 검증만 수행"임을 명시한다.
