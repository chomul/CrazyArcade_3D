# Task 10 — CA3DCharacter

> 선행: Task 06, 07, 09 · 후행: Task 11(조작), 12(상태 부착), 16(폭탄 설치 RPC)
> 체크리스트: `mds/Checklists/10-CA3DCharacter.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `ACA3DCharacter` |
| 부모 클래스 | `ACharacter` (CMC 그대로 활용 — 예측·보정·리플레이 무료 획득) |
| 역할 | 플레이어·봇 공용 폰. 이동·점프·낙사. **이 Task에서 셀 크기·이동속도·점프 높이를 몸으로 결정한다** (GDD 10장) |

## 생성 파일

- `Source/CrazyArcade3D/Gameplay/Character/CA3DCharacter.h/.cpp`
- (에디터) `Content/Blueprints/BP_CA3DCharacter` — 메시·애님 지정

## 구현 명세

```cpp
// CA3DCharacter.h
// 플레이어와 봇이 완전히 같은 코드 경로를 타는 공용 캐릭터.
// 이동은 CMC 기본 리플리케이션에 맡긴다 — 커스텀 이동 코드 금지.
UCLASS()
class ACA3DCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    ACA3DCharacter();   // 캡슐 크기·JumpZVelocity·MaxWalkSpeed 초기값 (전부 튜닝 대상)

    // 서버 전용: 낙사 검사. GetActorLocation().Z < KillZ → 사망 처리(Task 12에서 연결).
    virtual void Tick(float DeltaSeconds) override;

    // 캐릭터의 "발밑 셀" — 폭발 피격·폭탄 설치 위치의 기준 (GDD 2.3 "발판만이 안전하다").
    // ⚠️ 공중에 있을 때의 정의는 미결정 (설계서 3.2) — 1차 구현은
    // WorldToCell(ActorLocation - FVector(0,0,CapsuleHalfHeight)) 로 하되 튜닝에서 확정.
    FIntVector GetFootCell() const;

    // 이동·점프 입력 바인딩 대상 (호출은 Task 11의 컨트롤러가).
    void Move(const FVector2D& Axis);
    void DoJump();

protected:
    UPROPERTY() TObjectPtr<AVoxelWorld> VoxelWorld;  // BeginPlay에서 탐색·캐시
    float KillZ = -500.f;  // 임시 — 맵 최하층 아래. 룰셋 이동 여부는 튜닝 때 결정
};
```

**이 Task의 진짜 산출물 = 튜닝 확정.** 다음 값들은 **미결정**이다. 후보 값을 몇 개 만들어 PIE로 비교한 뒤 **사용자에게 물어서 확정**한다:
- 셀 크기 (`AVoxelWorld::CellSize`) — 100/120/150 등
- `MaxWalkSpeed` — 셀 크기와 묶어서 "1초에 몇 칸"으로 제시
- `JumpZVelocity` — **1블록 높이는 오르고 2블록은 못 오르는** 값 (GDD 2.1 층간 이동은 점프만)
- 공중 발밑 셀 정의

확정되면 이동속도 계수 외 값들의 저장 위치(룰셋 vs 캐릭터 기본값)를 정리해 보고한다.

## 검증 원칙

- 공통 원칙 + 아래.
- PIE: WASD 이동·점프로 Task 04의 계단식 접근로를 통해 **2층에 올라갈 수 있는가**.
- 2블록 수직 벽은 점프로 못 오르는가.
- 맵 밖으로 떨어지면 KillZ 로그가 찍히는가 (사망 처리는 Task 12에서).
- `stat unit` 유지.

## 응답 원칙

- 공통 원칙.
- **튜닝 후보와 체감 차이를 정리해 사용자에게 질문** — 임의 확정 금지. 확정 전 완료 보고 시 "값 임시" 명시.
- CMC 기본값을 벗어나게 만진 항목(마찰·가속 등)이 있으면 전부 나열한다.
