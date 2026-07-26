# Task 11 — CA3DPlayerController

> 선행: Task 10 · 후행: Task 16/17(폭탄 설치 입력)
> 체크리스트: `mds/Checklists/11-CA3DPlayerController.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `ACA3DPlayerController` |
| 부모 클래스 | `APlayerController` |
| 역할 | 입력(Enhanced Input) 바인딩 + **45도 스냅 회전 고정 각도 카메라** (GDD 5장). 클라 로컬 전용 관심사 — 게임 상태를 바꾸지 않는다 |

## 생성 파일

- `Source/CrazyArcade3D/Gameplay/Character/CA3DPlayerController.h/.cpp`
- (에디터) `Content/UI` 또는 `Content/Blueprints/Input/` — `IMC_Default`, `IA_Move/IA_Jump/IA_PlaceBomb/IA_RotateCam` 에셋

## 구현 명세

```cpp
// CA3DPlayerController.h
// 입력과 카메라만 담당한다. 판정·상태 변경은 캐릭터(서버 RPC) 소관.
UCLASS()
class ACA3DPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;          // IMC 등록 (로컬 컨트롤러만)
    virtual void SetupInputComponent() override;

protected:
    // BP 서브클래스에서 에셋만 지정.
    UPROPERTY(EditDefaultsOnly, Category="Input") TObjectPtr<UInputMappingContext> DefaultIMC;
    UPROPERTY(EditDefaultsOnly, Category="Input") TObjectPtr<UInputAction> IA_Move;
    UPROPERTY(EditDefaultsOnly, Category="Input") TObjectPtr<UInputAction> IA_Jump;
    UPROPERTY(EditDefaultsOnly, Category="Input") TObjectPtr<UInputAction> IA_PlaceBomb;   // 핸들러는 Task 16에서 구현
    UPROPERTY(EditDefaultsOnly, Category="Input") TObjectPtr<UInputAction> IA_RotateCam;   // Q/E 등 45도 스냅

    void OnMove(const FInputActionValue& V);
    void OnJump();
    void OnRotateCam(const FInputActionValue& V);  // ±45도 스냅, 보간 회전

private:
    // 카메라 상태 (SpringArm은 캐릭터 쪽에 두거나 여기서 ViewTarget 관리 — 구현 시 택1 보고).
    int32 CamYawSteps = 0;   // 45도 단위 스텝
};
```

**⚠️ 미결정 — 카메라 입력 기준 (설계서 7장)**: WASD가 **월드 축 기준**인지 **카메라 기준**인지 미정. 45도 스냅 회전과 맞물리므로 **둘 다 구현하고 토글**(콘솔 변수 등)을 만들어 PIE로 비교 → 사용자에게 물어 확정한다. 임의 확정 금지.

**연결 작업**: GameMode의 `PlayerControllerClass` 지정, IMC/IA 에셋 생성·연결.

## 검증 원칙

- 공통 원칙 + 아래.
- PIE: 45도 스냅 회전이 8방향 모두 돌아가는가. 회전 중 이동이 끊기지 않는가.
- 두 입력 기준 모드가 토글로 전환되는가.
- 가림 처리(디더 페이드)는 이 Task 범위 아님 — 3주차 아트 패스. 끼워 넣지 말 것.

## 응답 원칙

- 공통 원칙.
- 두 입력 기준의 체감 차이를 정리해 **사용자에게 질문**한다.
- 카메라 구조(ViewTarget vs 캐릭터 SpringArm) 선택과 이유를 한 줄 보고.
