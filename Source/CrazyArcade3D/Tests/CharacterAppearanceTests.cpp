// 선택 캐릭터 외형 적용 + AnimInstance 값 계산 자동화 테스트 (Task 37).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Gameplay.CharacterAppearance" 로 실행.
//
// 검증 항목:
//   ① UCA3DAnimInstance static 순수 함수 — 속도→Speed/bMoving, LifeState→bTrapped/bDead 매핑
//   ② 인스턴스 갱신 경로(NativeUpdateAnimation)가 캐릭터의 CMC·Status 를 올바르게 읽는다
//   ③ ApplyCharacterAppearance — 메시·트랜스폼 적용, 같은 인덱스 재적용 안 함(스냅샷 비교),
//      인덱스 범위 밖·Mesh 미지정 안전(스킵 후 스냅샷 기록), 인덱스 변경 시 재적용
//   ④ 데디 가드는 자동화 월드에서 검증 불가 — IsRunningDedicatedServer() 가 에디터 프로세스에서
//      항상 false 라 가드 분기를 태울 방법이 없다 (CLAUDE.md HISM 함정 주석과 같은 한계).
//      PIE 아닌 진짜 서버 exe 검증 항목으로 남긴다.
//
// 월드를 직접 만들므로 BeginPlay 는 돌지 않는다 — ApplyCharacterAppearance 는 friend 로 직접
// 호출한다 (CA3DCharacterTests 의 TryApplyMovementTuning 관례). 룰셋은 CDO 를 건드리지 않고
// ACA3DGameState 에 인스턴스를 주입한다 (TrappedPopTests 관례).
//
// ⚠️ 무명 네임스페이스 헬퍼 이름은 번역 단위 병합에서 모듈 전체와 합쳐진다 —
// 접두사 Cap~ 으로 고유하게 유지할 것 (mds/build.md "번역 단위 병합 빌드" 절).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/CA3DAnimInstance.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DPlayerState.h"
#include "Framework/CA3DRuleSet.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterAppearanceTest, "CrazyArcade3D.Gameplay.CharacterAppearance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// 빈 스켈레탈 메시 생성 — 렌더 데이터가 없어도 컴포넌트는 널 체크로 통과한다(데디와 같은
	// 경로). 스켈레톤을 붙여 두는 이유: SetSkeletalMesh 의 애님 재초기화가 스켈레톤을 본다.
	USkeletalMesh* CapMakeMesh(UObject* Outer, const TCHAR* Name)
	{
		USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Outer, Name);
		USkeleton* Skeleton = NewObject<USkeleton>(Mesh, TEXT("CapSkeleton"));
		Mesh->SetSkeleton(Skeleton);
		return Mesh;
	}
}

bool FCharacterAppearanceTest::RunTest(const FString& Parameters)
{
	// ─── ① static 순수 함수 — 인스턴스 없이 검증 (MatchWidget 관례) ───

	// 수평 속도: Z 성분(점프·낙하)은 빠진다. 3-4-5 직각삼각형으로 평면 크기만 확인.
	TestTrue(TEXT("수평 속도: Z 제외 (3,4,100 → 5)"),
		FMath::IsNearlyEqual(UCA3DAnimInstance::ComputeHorizontalSpeed(FVector(3.f, 4.f, 100.f)), 5.f, 0.001f));

	// bMoving: 정지(0)와 부동소수 잔여는 false, 실제 이동 속도는 true.
	TestFalse(TEXT("bMoving: 속도 0 → false"), UCA3DAnimInstance::ComputeMoving(0.f));
	TestFalse(TEXT("bMoving: 부동소수 잔여(임계 미만) → false"),
		UCA3DAnimInstance::ComputeMoving(KINDA_SMALL_NUMBER * 0.5f));
	TestTrue(TEXT("bMoving: 갇힘 미세 이동 속도(60)도 이동이다 → true"),
		UCA3DAnimInstance::ComputeMoving(60.f));
	TestTrue(TEXT("bMoving: 일반 이동 속도(400) → true"), UCA3DAnimInstance::ComputeMoving(400.f));

	// LifeState 매핑: Trapped/Dead 가 각자 정확히 자기 상태만 본다 (Spectating 은 Dead 묶음).
	TestFalse(TEXT("bTrapped: Alive → false"), UCA3DAnimInstance::ComputeTrapped(ELifeState::Alive));
	TestTrue(TEXT("bTrapped: Trapped → true"), UCA3DAnimInstance::ComputeTrapped(ELifeState::Trapped));
	TestFalse(TEXT("bTrapped: Dead → false"), UCA3DAnimInstance::ComputeTrapped(ELifeState::Dead));
	TestFalse(TEXT("bDead: Alive → false"), UCA3DAnimInstance::ComputeDead(ELifeState::Alive));
	TestFalse(TEXT("bDead: Trapped → false (갇힘은 사망이 아니다)"), UCA3DAnimInstance::ComputeDead(ELifeState::Trapped));
	TestTrue(TEXT("bDead: Dead → true"), UCA3DAnimInstance::ComputeDead(ELifeState::Dead));
	TestTrue(TEXT("bDead: Spectating → true (Dead 묶음 — StatusComponent.cpp 판정과 일치)"),
		UCA3DAnimInstance::ComputeDead(ELifeState::Spectating));

	// ─── 월드 구성 — 넷드라이버 없는 게임 월드 (TrappedPopTests 관례) ───
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("테스트 월드 생성"), World))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 룰셋 주입 — CDO 를 건드리지 않는다 (다른 스위트로 새면 원인 찾기가 매우 어렵다).
	ACA3DGameState* GameState = World->SpawnActor<ACA3DGameState>(
		ACA3DGameState::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("ACA3DGameState 스폰"), GameState))
	{
		World->DestroyWorld(false);
		return false;
	}
	UCA3DRuleSet* Rules = NewObject<UCA3DRuleSet>(GameState);
	GameState->Rules = Rules;

	// 캐릭터 정의 3종: [0]=풀 지정, [1]=기본 보정값, [2]=Mesh 미지정 (스킵 검증용).
	USkeletalMesh* MeshA = CapMakeMesh(Rules, TEXT("CapMeshA"));
	USkeletalMesh* MeshB = CapMakeMesh(Rules, TEXT("CapMeshB"));

	FCA3DCharacterDef DefA;
	DefA.DisplayName = FText::FromString(TEXT("A"));
	DefA.Mesh = MeshA;
	DefA.MeshOffset = FVector(5.f, 0.f, -2.f);
	DefA.MeshYawOffset = -90.f;
	DefA.MeshScale = 1.25f;

	FCA3DCharacterDef DefB;
	DefB.DisplayName = FText::FromString(TEXT("B"));
	DefB.Mesh = MeshB; // 나머지는 struct 기본값 (Offset 0 / Yaw -90 / Scale 1)

	FCA3DCharacterDef DefNoMesh;
	DefNoMesh.DisplayName = FText::FromString(TEXT("NoMesh"));

	Rules->Characters = { DefA, DefB, DefNoMesh };

	ACA3DCharacter* Character = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	ACA3DPlayerState* PlayerState = World->SpawnActor<ACA3DPlayerState>();
	if (!TestNotNull(TEXT("ACA3DCharacter 스폰"), Character)
		|| !TestNotNull(TEXT("ACA3DPlayerState 스폰"), PlayerState))
	{
		World->DestroyWorld(false);
		return false;
	}
	USkeletalMeshComponent* MeshComp = Character->GetMesh();
	TestNotNull(TEXT("전제: 메시 컴포넌트 존재"), MeshComp);

	// ─── ③-a PlayerState 미도착 → 아무것도 안 한다 (복제 순서 비보장 대비) ───
	Character->ApplyCharacterAppearance(); // friend
	TestTrue(TEXT("PlayerState 미도착: 스냅샷 그대로 (다음 틱 재시도 상태)"),
		Character->AppliedCharacterIndex == INDEX_NONE);

	Character->SetPlayerState(PlayerState); // SpectateTests 관례 — 역참조 배선

	// ─── ③-b 미선택(INDEX_NONE) → 적용 없음 ───
	Character->ApplyCharacterAppearance();
	TestTrue(TEXT("CharacterIndex 미확정: 적용 없음"), Character->AppliedCharacterIndex == INDEX_NONE);
	TestTrue(TEXT("CharacterIndex 미확정: 메시 그대로 (C++ 기본 = 없음)"),
		MeshComp->GetSkeletalMeshAsset() == nullptr);

	// ─── ③-c 정상 적용 — 메시·트랜스폼(캡슐 바닥 기준 + 보정값) ───
	PlayerState->CharacterIndex = 0;
	Character->ApplyCharacterAppearance();
	TestTrue(TEXT("적용: 스냅샷 == 0"), Character->AppliedCharacterIndex == 0);
	TestTrue(TEXT("적용: 메시 A 지정"), MeshComp->GetSkeletalMeshAsset() == MeshA);

	const float HalfHeight = Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	TestTrue(TEXT("적용: 상대 위치 == 캡슐 바닥(-반높이) + MeshOffset"),
		MeshComp->GetRelativeLocation().Equals(FVector(0.f, 0.f, -HalfHeight) + DefA.MeshOffset, 0.01f));
	TestTrue(TEXT("적용: yaw == MeshYawOffset"),
		FMath::IsNearlyEqual(MeshComp->GetRelativeRotation().Yaw, DefA.MeshYawOffset, 0.01f));
	TestTrue(TEXT("적용: 스케일 == MeshScale"),
		MeshComp->GetRelativeScale3D().Equals(FVector(DefA.MeshScale), 0.001f));

	// CMC 스무딩 캐시 (2026-08-14 Task 39) — 적용 직후 CacheInitialMeshOffset 이 같은 값으로
	// 갱신됐는가. 안 갱신하면 시뮬레이티드 프록시의 SmoothClientPosition_UpdateVisuals 가
	// 매 프레임 PostInitializeComponents 시점의 낡은 캐시 기준으로 메시 트랜스폼을 되돌린다 —
	// **원격 프록시에서만** 어긋나는 버그라 단일 프로세스 실행으로는 화면에서 안 잡힌다.
	TestTrue(TEXT("적용: BaseTranslationOffset == 적용한 상대 위치 (CMC 스무딩 캐시 갱신)"),
		Character->GetBaseTranslationOffset().Equals(FVector(0.f, 0.f, -HalfHeight) + DefA.MeshOffset, 0.01f));
	TestTrue(TEXT("적용: BaseRotationOffset == 적용한 회전 (〃)"),
		Character->GetBaseRotationOffset().Equals(FRotator(0.f, DefA.MeshYawOffset, 0.f).Quaternion(), 0.001f));

	// ─── ③-d 같은 인덱스 재적용 안 함 — 스냅샷 비교의 증명 ───
	// 스케일을 일부러 어긋내고 다시 부른다: 재적용했다면 1.25 로 되돌아갔을 것이다.
	MeshComp->SetRelativeScale3D(FVector(9.f));
	Character->ApplyCharacterAppearance();
	TestTrue(TEXT("같은 인덱스: 재적용 안 함 (어긋낸 스케일 유지)"),
		MeshComp->GetRelativeScale3D().Equals(FVector(9.f), 0.001f));

	// ─── ③-e 인덱스 변경 → 재적용 ───
	PlayerState->CharacterIndex = 1;
	Character->ApplyCharacterAppearance();
	TestTrue(TEXT("인덱스 변경: 메시 B 로 교체"), MeshComp->GetSkeletalMeshAsset() == MeshB);
	TestTrue(TEXT("인덱스 변경: 스케일도 정의값(1.0)으로"),
		MeshComp->GetRelativeScale3D().Equals(FVector(DefB.MeshScale), 0.001f));
	TestTrue(TEXT("인덱스 변경: 스무딩 캐시도 새 정의값 추종 (Task 39)"),
		Character->GetBaseTranslationOffset().Equals(FVector(0.f, 0.f, -HalfHeight) + DefB.MeshOffset, 0.01f));

	// ─── ③-f 범위 밖 인덱스 — 크래시 없이 스킵, 기존 외형 유지, 스냅샷 기록(로그 1회) ───
	PlayerState->CharacterIndex = 99;
	Character->ApplyCharacterAppearance();
	TestTrue(TEXT("범위 밖: 기존 메시 유지"), MeshComp->GetSkeletalMeshAsset() == MeshB);
	TestTrue(TEXT("범위 밖: 스냅샷 기록 (매 틱 재시도·로그 반복 방지)"),
		Character->AppliedCharacterIndex == 99);

	// ─── ③-g Mesh 미지정 정의 — 같은 스킵 경로 ───
	PlayerState->CharacterIndex = 2;
	Character->ApplyCharacterAppearance();
	TestTrue(TEXT("Mesh 미지정: 기존 메시 유지"), MeshComp->GetSkeletalMeshAsset() == MeshB);
	TestTrue(TEXT("Mesh 미지정: 스냅샷 기록"), Character->AppliedCharacterIndex == 2);

	// 스킵 뒤에도 유효 인덱스로 돌아오면 복구된다.
	PlayerState->CharacterIndex = 0;
	Character->ApplyCharacterAppearance();
	TestTrue(TEXT("스킵 후 유효 인덱스: 다시 적용된다"), MeshComp->GetSkeletalMeshAsset() == MeshA);

	// ─── ② AnimInstance 갱신 경로 — 캐릭터의 CMC·Status 를 읽는다 ───
	// 엔진 애님 파이프라인 전체를 돌리지 않고 outer 배선(TryGetPawnOwner 경로)만 만들어
	// Native* 를 직접 부른다 (월드 손구성 테스트의 관례 — BeginPlay 없이 수동 배선).
	UCA3DAnimInstance* Anim = NewObject<UCA3DAnimInstance>(MeshComp);
	Anim->NativeInitializeAnimation();

	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Walking); // 테스트 월드엔 바닥 컬리전이 없다
	Movement->Velocity = FVector(300.f, 400.f, 0.f);
	Anim->NativeUpdateAnimation(0.016f);
	TestTrue(TEXT("애님: Speed == 수평 500"), FMath::IsNearlyEqual(Anim->Speed, 500.f, 0.01f));
	TestTrue(TEXT("애님: bMoving"), Anim->bMoving);
	TestFalse(TEXT("애님: 지상 — bInAir false"), Anim->bInAir);
	TestFalse(TEXT("애님: Alive — bTrapped false"), Anim->bTrapped);
	TestFalse(TEXT("애님: Alive — bDead false"), Anim->bDead);

	// 공중 — bInAir. 낙하 수직 속도는 Speed 에 섞이지 않는다.
	Movement->SetMovementMode(MOVE_Falling);
	Movement->Velocity = FVector(0.f, 0.f, -900.f);
	Anim->NativeUpdateAnimation(0.016f);
	TestTrue(TEXT("애님: 공중 — bInAir true"), Anim->bInAir);
	TestFalse(TEXT("애님: 수직 낙하 — bMoving false (Z 는 수평 속도가 아니다)"), Anim->bMoving);

	// 갇힘·사망 — LifeState 매핑이 인스턴스 경로에서도 성립.
	Movement->SetMovementMode(MOVE_Walking);
	Movement->Velocity = FVector::ZeroVector;
	Character->GetStatus()->LifeState = ELifeState::Trapped; // 서버 진입점 생략 — 값 매핑만 본다
	Anim->NativeUpdateAnimation(0.016f);
	TestTrue(TEXT("애님: Trapped — bTrapped true"), Anim->bTrapped);
	TestFalse(TEXT("애님: Trapped — bDead false"), Anim->bDead);

	Character->GetStatus()->LifeState = ELifeState::Dead;
	Anim->NativeUpdateAnimation(0.016f);
	TestFalse(TEXT("애님: Dead — bTrapped false"), Anim->bTrapped);
	TestTrue(TEXT("애님: Dead — bDead true"), Anim->bDead);
	TestFalse(TEXT("애님: 정지 — bMoving false"), Anim->bMoving);

	// ─── 정리 ───
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
