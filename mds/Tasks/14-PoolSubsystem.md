# Task 14 — PoolSubsystem

> 선행: Task 13 · 후행: Task 16(물줄기 FX·데칼), 17, 23
> 체크리스트: `mds/Checklists/14-PoolSubsystem.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `UPoolSubsystem` |
| 부모 클래스 | `UWorldSubsystem` |
| 역할 | 제네릭 액터 풀. 물줄기 세그먼트(최대 부하 ~700)·파편 FX·위험 데칼·아이템 픽업을 통일 API로 재사용. **클라 시각 요소 전용** |

## 생성 파일

- `Source/CrazyArcade3D/Core/PoolSubsystem.h/.cpp`

## 구현 명세

```cpp
// PoolSubsystem.h
// 월드당 1개 자동 생성되는 제네릭 액터 풀.
// Release된 액터는 숨김+컬리전 off+틱 off 상태로 Free 리스트에 보관된다.
UCLASS()
class UPoolSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    // 매치 시작 전 미리 스폰해 히치를 없앤다 (GDD 7.3: 100~200개 선확보).
    void Prewarm(TSubclassOf<AActor> Class, int32 Count);

    // 풀에서 꺼낸다. 비어 있으면 새로 스폰. 꺼낸 후 IPooledActor::OnAcquiredFromPool 호출.
    AActor* Acquire(TSubclassOf<AActor> Class, const FTransform& T);

    // 풀로 돌려보낸다. IPooledActor::OnReleasedToPool 호출 후 비활성화.
    void Release(AActor* Actor);

    // 타입 안전 헬퍼.
    template<typename T>
    T* Acquire(TSubclassOf<T> Class, const FTransform& X)
    { return Cast<T>(Acquire(TSubclassOf<AActor>(Class), X)); }

private:
    // 클래스별 프리 리스트. UPROPERTY로 GC 수거 방지.
    UPROPERTY() TMap<TObjectPtr<UClass>, FPooledActorArray> Free;  // TArray 직접 못 넣으므로 래퍼 struct
};
```

**규칙**
- Acquire/Release 시 액터가 `IPooledActor`를 구현하면 콜백 호출, 아니면 `ensure` (계약 위반 조기 발견).
- 서버 권한 액터(`ABomb`) 풀링 금지 — 상태 오염 위험이 이득보다 크다.
- `TMap<UClass*, ...>`는 **순회하지 않는다** (조회만) — 순서 비결정 문제와 무관하게 유지.

## 검증 원칙

- 공통 원칙 + 아래.
- 스트레스: 임시 테스트 액터로 **200개 Acquire → 전부 Release → 다시 200개 Acquire** 를 수 회 반복. `stat unit` 스파이크 없음, 액터 수가 누수 없이 일정.
- Release된 액터가 화면에 안 보이고 컬리전이 꺼져 있는가.
- Prewarm 후 첫 Acquire가 스폰 없이 반환되는가 (로그로 확인).

## 응답 원칙

- 공통 원칙.
- 스트레스 테스트 수치(개수·반복·프레임 타임)를 보고에 포함한다.
- 테스트용 임시 액터/코드를 남겼다면 위치를 보고한다.
