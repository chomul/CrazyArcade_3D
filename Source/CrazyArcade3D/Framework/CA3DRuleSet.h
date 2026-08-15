#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h"
#include "CA3DRuleSet.generated.h"

class AWaterSegment;
class ADangerDecal;
class APredictedBombVisual;
class AItemPickup;
class ASuddenDeathDropMarker;
class UMaterialParameterCollection;
class USoundBase;      // 아래 Feedback 카테고리 — 전방 선언으로 충분 (헤더 최소화)
class UNiagaraSystem;  // 〃 (Niagara 는 Private 의존 — 공개 헤더에 나이아가라 헤더를 끌어오지 않는다)
class USkeletalMesh;   // 아래 CharacterSelect 카테고리 — 전방 선언으로 충분
class UAnimInstance;   // 〃
class UAnimMontage;    // 〃 (AttackMontage — Task 38)

// 선택 가능한 캐릭터 1종의 정의 (Task 36). **이번 Task 는 데이터 정의까지만** —
// 실제 외형 적용(폰 메시·애님 교체)은 Task 37 이 ACA3DPlayerState::CharacterIndex 로
// 이 배열을 인덱싱해 수행한다.
//
// 오프셋·yaw·스케일이 코드가 아니라 **캐릭터 정의 데이터**에 있는 이유: 외부 에셋
// (마켓플레이스) 캐릭터마다 원점·정면 축·크기가 제각각이라, 보정값을 데이터로 들고
// 있어야 8종을 한 벌의 코드로 붙일 수 있다 (종별 분기 코드 금지).
USTRUCT(BlueprintType)
struct FCA3DCharacterDef
{
	GENERATED_BODY()

	// 선택 UI·결과 화면에 표시할 이름 (화면 표시용이므로 FText — 코딩 규칙).
	UPROPERTY(EditAnywhere, Category="CharacterSelect")
	FText DisplayName;

	UPROPERTY(EditAnywhere, Category="CharacterSelect")
	TObjectPtr<USkeletalMesh> Mesh;

	UPROPERTY(EditAnywhere, Category="CharacterSelect")
	TSubclassOf<UAnimInstance> AnimClass;

	// 폭탄 설치 순간 재생하는 몽타주 (Task 38). 캐릭터마다 스켈레톤이 달라 애님도 정의
	// 데이터에 있어야 한다 (Mesh·AnimClass 와 같은 근거). 미지정(nullptr)이면 재생 생략 —
	// 에셋 연결 전에도 설치 동작 자체는 그대로 돌아야 한다.
	UPROPERTY(EditAnywhere, Category="CharacterSelect")
	TObjectPtr<UAnimMontage> AttackMontage;

	// 사망 순간 재생하는 몽타주 (Task 39). 캐릭터 8종의 Die 애님 길이가 제각각이라 전역
	// DeathHideDelay 하나로는 숨김 시점이 안 맞는다 — **재생하는 것이 곧 타이머의 출처**:
	// C++ 이 이 몽타주를 직접 재생하고 반환된 실측 길이(재생 속도 반영)로 숨긴다.
	// 미지정(nullptr)이면 DeathHideDelay 폴백 — 에셋 연결 전에도 사망 처리는 그대로 돈다
	// (AttackMontage 와 같은 근거).
	UPROPERTY(EditAnywhere, Category="CharacterSelect")
	TObjectPtr<UAnimMontage> DeathMontage;

	// 캡슐 기준 메시 위치 보정.
	UPROPERTY(EditAnywhere, Category="CharacterSelect")
	FVector MeshOffset = FVector::ZeroVector;

	// 정면 축 보정(도). 언리얼 스켈레탈 메시 관례가 -90 (메시 Y+ 정면 → 캡슐 X+ 정면).
	UPROPERTY(EditAnywhere, Category="CharacterSelect")
	float MeshYawOffset = -90.f;

	UPROPERTY(EditAnywhere, Category="CharacterSelect")
	float MeshScale = 1.f;
};

// 매치 규칙·튜닝 값의 단일 출처. 로직 없음 — 순수 데이터.
// 인스턴스를 여러 개 만들어 룰셋 프리셋(기본전/스피드전 등)으로 쓴다.
// GameMode(서버)가 소유하되, GameState에 에셋 "포인터"를 복제해야 클라 프리뷰가 맞는다
// (UE는 에셋 참조를 경로로 복제하므로 포인터 복제 비용은 문자열 하나).
UCLASS(BlueprintType)
class CRAZYARCADE3D_API UCA3DRuleSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ── Bomb ──────────────────────────────────────────────

	// 설치 → 폭발까지 시간(초).
	UPROPERTY(EditAnywhere, Category="Bomb")
	float BombFuseTime = 3.f;

	// 물줄기(폭발 판정) 잔존 시간(초).
	UPROPERTY(EditAnywhere, Category="Bomb")
	float WaterLingerTime = 1.f;

	// 연쇄 폭발 1단계당 간격(초).
	UPROPERTY(EditAnywhere, Category="Bomb")
	float ChainStepDelay = 0.07f;

	// 폭탄 개수 스택 상한. (값 미확정)
	UPROPERTY(EditAnywhere, Category="Bomb")
	int32 MaxBombCountCap = 8;

	// 폭발 범위 스택 상한. (값 미확정)
	UPROPERTY(EditAnywhere, Category="Bomb")
	int32 MaxBombRangeCap = 6;

	// ─── Bomb (Task 16) ───

	// 시작 시 동시 설치 가능 폭탄 수 — 초기값도 데이터로 노출 (GDD 7.5, 매직 넘버 금지).
	// 아이템(Balloon)이 +1 씩, MaxBombCountCap 이 상한 (StatusComponent 가 서버에서 적용).
	UPROPERTY(EditAnywhere, Category="Bomb")
	int32 InitialBombCount = 1;

	// 시작 시 폭발 전파 범위(칸) — 아이템(Potion)이 +1 씩, MaxBombRangeCap 이 상한.
	UPROPERTY(EditAnywhere, Category="Bomb")
	int32 InitialBombRange = 1;

	// 폭탄이 플레이어를 막는 박스의 반경(셀 단위). 실제 extent = 이 계수 × AVoxelWorld::CellSize —
	// 셀 크기를 바꿔도 "폭탄 한 칸이 막힌다" 가 유지된다 (ItemPickupRadiusCells 와 같은 관례).
	// 0.5 미만이어야 한다: 정확히 0.5 면 박스가 셀을 꽉 채워 옆 칸을 지나갈 때 모서리에 걸린다.
	UPROPERTY(EditAnywhere, Category="Bomb", meta=(ClampMin="0.05", ClampMax="0.49"))
	float BombBlockExtentCells = 0.45f;

	// 폭발 FX 클래스 지정 — BP 서브클래스(메시·머티리얼 에셋만 지정, BP 로직 금지)를 연결한다.
	// 미지정이면 코드가 C++ 기본 클래스로 폴백한다 (에셋 없이도 판정·동작은 정상 — 경고 로그만).

	// 물줄기 세그먼트 클래스 (클라 FX — 풀링 대상). 예: BP_WaterSegment.
	UPROPERTY(EditAnywhere, Category="Bomb")
	TSubclassOf<AWaterSegment> WaterSegmentClass;

	// 위험 구역 프리뷰 데칼 클래스 (클라 시각 — 풀링 대상). 예: BP_DangerDecal.
	UPROPERTY(EditAnywhere, Category="Bomb")
	TSubclassOf<ADangerDecal> DangerDecalClass;

	// 폭탄 설치 로컬 예측 비주얼 클래스 (클라 시각 — 풀링 대상, Task 17).
	// 예: BP_PredictedBombVisual — BP_Bomb 과 같은 메시를 지정한다 (구분 어려워야 정상).
	UPROPERTY(EditAnywhere, Category="Bomb")
	TSubclassOf<APredictedBombVisual> PredictedBombVisualClass;

	// ─── Bomb — 킥 (2026-08-06 사용자 확정: 방향키로 밀기, 별도 키 없음) ───
	// 킥 아이템을 든 채로 폭탄에 걸어 들어가면 자동으로 밀린다. 킥이 없으면 지금처럼 벽이다.

	// 한 번 차면 최대 몇 칸을 미끄러지는가. 막는 것이 없으면 정확히 이만큼 가고 멈춘다.
	// 10칸 = 21×21 맵의 절반 — "판 반대편까지 보내지는 못한다" 가 상한의 의미다.
	UPROPERTY(EditAnywhere, Category="Bomb", meta=(ClampMin="1"))
	int32 BombKickMaxCells = 10;

	// 미끄러지는 속도(칸/초).
	//
	// **차는 사람보다 빨라야 한다** — 이게 기본값의 근거다. 느리면 찬 사람이 그대로 따라붙어
	// 매 프레임 다시 접촉하고, 폭탄은 사실상 발에 붙어 끌려다니는 물건이 된다 (차서 멀리
	// 보낸다는 규칙 자체가 사라진다). 사람의 최대 속도는 MoveSpeedCellsPerSec 4 ×
	// MoveSpeedMulCap 1.6 = 6.4칸/초이므로 8 이면 롤러를 최대로 먹어도 따라잡히지 않는다.
	// 8칸/초면 최대 거리 10칸에 1.25초 — 퓨즈(BombFuseTime 3초)의 절반이 안 되어
	// 차고 나서 피할 시간이 남는다.
	UPROPERTY(EditAnywhere, Category="Bomb", meta=(ClampMin="0.1"))
	float BombKickSpeedCellsPerSec = 8.f;

	// 킥 접촉 판정의 여유 거리(셀 단위). 사거리 = 캡슐 반지름 + BombBlockExtentCells×CellSize + 이 값.
	// "걸어 들어가면" 이므로 실제로 닿아야 차진다 — 여유가 0 이면 CMC 가 남기는 미세한 접촉 간격
	// 때문에 정면으로 밀고 있어도 안 차지는 프레임이 생기고, 반대로 크면 한 칸 앞에서 폭탄이
	// 먼저 도망가 손도 안 댄 것처럼 보인다. 0.1칸 = 10cm.
	UPROPERTY(EditAnywhere, Category="Bomb", meta=(ClampMin="0.0", ClampMax="0.5"))
	float BombKickReachToleranceCells = 0.1f;

	// 발판이 없는 칸에 들어간 폭탄이 떨어지는 속도(칸/초). 2026-08-09 사용자 확정:
	// "절벽에서 밀었을 때 공중을 가로질러 미끄러지는" 문제를 낙하로 해결한다.
	// 중력(CMC)이 아니라 등속인 이유: 낙하도 킥과 같은 **칸 단위 판정**을 타야 하고
	// (한 칸 내려갈 때마다 Cell.Z 갱신 → 폭발 원점·위험 데칼이 따라온다), 가속을 넣으면
	// 프레임이 길 때 한 틱에 여러 칸을 건너뛰는 계산이 킥 쪽과 두 벌이 된다.
	// 킥 속도(8)보다 빠르게 잡아 "밀려나간 뒤 곧바로 떨어진다" 로 보이게 한다.
	UPROPERTY(EditAnywhere, Category="Bomb", meta=(ClampMin="1.0"))
	float BombFallSpeedCellsPerSec = 12.f;

	// ── Life ──────────────────────────────────────────────

	// 물방울에 갇힌 상태 지속 시간(초). 3~5초 권장.
	UPROPERTY(EditAnywhere, Category="Life")
	float TrappedDuration = 4.f;

	// 갇힌 상태에서의 미세 이동 속도.
	UPROPERTY(EditAnywhere, Category="Life")
	float TrappedMoveSpeed = 60.f;

	// 스폰 직후 무적 시간(초). (유무 미확정)
	UPROPERTY(EditAnywhere, Category="Life")
	float SpawnInvulnTime = 0.f;

	// 사망 후 액터를 숨기기까지의 시간(초) — **DeathMontage 미지정/재생 불가 시의 폴백**
	// (Task 39 에서 역할 변경). 캐릭터 정의에 DeathMontage 가 있고 재생에 성공하면 그 몽타주의
	// 실측 길이가 숨김 시점이 되고 이 값은 쓰이지 않는다. 폴백일 때 0 이하 = 즉시 숨김(기존 동작).
	// 컬리전 해제·이동 정지는 이 값과 무관하게 사망 즉시다 — 지연되는 것은 **보이는 것**뿐이다.
	// (EditAnywhere 인 이유: 데이터 에셋 인스턴스는 템플릿이 아니라 EditDefaultsOnly 필드가
	//  에셋 에디터에 안 뜬다 — 이 클래스의 다른 튜닝 값 전부와 같은 사정.)
	UPROPERTY(EditAnywhere, Category="Life")
	float DeathHideDelay = 2.0f;

	// ─── Life — 갇힌 상대 터뜨리기 (2026-08-10 사용자 확정: 원작 크레이지 아케이드 규칙) ───
	//
	// 물방울에 갇힌(ELifeState::Trapped) 캐릭터에 **갇히지 않은 살아 있는(Alive)** 캐릭터가
	// 몸으로 닿으면 즉시 사망한다(EDeathCause::Popped). 유예·경고 없음.
	// 팀이 없으므로(라스트맨 스탠딩) 자기 자신만 아니면 누구든 터뜨릴 수 있고,
	// 갇힌 사람끼리·시체는 서로 못 터뜨린다.
	//
	// **접촉 여유 값은 새로 만들지 않았다** — 위 BombKickReachToleranceCells 를 그대로 쓴다.
	// 두 판정이 같은 물리적 원인(CMC 가 남기는 미세한 접촉 간격)을 보정하는 같은 값이라,
	// 나눠 두면 "차지는 거리" 와 "터지는 거리" 가 조용히 갈라진다. 값을 늘리는 것도 비용이다.
	UPROPERTY(EditAnywhere, Category="Life")
	bool bPopTrappedOnContact = true;

	// ── Character (Task 10) ───────────────────────────────
	// 이동·점프 파생 값은 전부 "셀 단위 계수 × AVoxelWorld::CellSize" 로 계산한다 —
	// 셀 크기를 바꿔도 게임 감각이 유지된다 (하드코딩 금지).
	// ⚠️ 아래 계수 전부 임시 — PIE 튜닝 후 사용자 확정 (Task 10 완료 조건).

	// 이동속도: 1초에 몇 칸. CMC MaxWalkSpeed = 이 값 × CellSize.
	UPROPERTY(EditAnywhere, Category="Character")
	float MoveSpeedCellsPerSec = 4.f;

	// 정지 → 최대속도까지 걸리는 시간(초). CMC MaxAcceleration = MaxWalkSpeed ÷ 이 값.
	// 속도(cm/s)가 아니라 **시간**으로 두는 이유: 셀 크기·아이템 속도 배율이 바뀌어도
	// "얼마나 즉각적인가" 라는 감각이 그대로 유지된다 (가속도로 두면 빨라질수록 굼떠 보인다).
	// 크레이지 아케이드류는 그리드 칸 맞추기가 핵심이라 거의 즉시(0.05초 ≈ 3프레임)가 기본.
	UPROPERTY(EditAnywhere, Category="Character", meta=(ClampMin="0.01", ClampMax="1.0"))
	float MoveAccelTime = 0.05f;

	// 최대속도 → 정지까지 걸리는 시간(초). CMC BrakingDecelerationWalking = MaxWalkSpeed ÷ 이 값.
	// 제동 마찰(BrakingFriction)은 0 으로 두고 이 감속도 하나만 쓴다 — 정지 거리가 예측 가능해야
	// 폭탄을 놓을 칸에 정확히 설 수 있다 (미끄러지면 위험 구역 계산이 어긋난 것처럼 느껴진다).
	UPROPERTY(EditAnywhere, Category="Character", meta=(ClampMin="0.01", ClampMax="1.0"))
	float MoveBrakeTime = 0.05f;

	// 공중 수평 속도 = 지상 속도 × 이 계수. 체공 시간(점프 높이)은 그대로 두고 속도만 줄이므로
	// **점프 이동 거리가 정확히 이 배율**이 된다. 1.0 이면 지상과 동일(관성 그대로).
	// 점프는 한 칸 위로 오르는 수단이지 빠르게 가로지르는 수단이 아니어야 한다 —
	// 지상과 같으면 계속 점프해 이동하는 편이 이득이 되어 칸 단위 위치 싸움이 흐려진다.
	UPROPERTY(EditAnywhere, Category="Character", meta=(ClampMin="0.1", ClampMax="1.0"))
	float JumpAirSpeedFactor = 0.7f;

	// 점프 정점 높이(셀 단위, 캡슐 바닥 기준). JumpZVelocity = sqrt(2 × g × 계수 × CellSize).
	// (1, 2) 열린 구간이어야 "1칸은 오르고 2칸은 못 오른다" (GDD 2.1 — 층간 이동은 점프만).
	// 기본 1.4 = 1칸 + 여유 0.4칸: 턱 위 높이에 머무는 시간을 확보해 수평 이동으로
	// 턱에 올라설 수 있게 한다 (1.0 초과 빠듯하면 정점 순간에만 턱 높이라 못 올라감).
	UPROPERTY(EditAnywhere, Category="Character", meta=(ClampMin="1.05", ClampMax="1.95"))
	float JumpApexCellFactor = 1.4f;

	// CMC MaxStepHeight = 계수 × CellSize. 1.0 미만이어야 1블록을 점프 없이
	// 걸어 오르는 사고가 없다 (엔진 기본 45 는 셀이 작아지면 1칸을 넘어버림).
	UPROPERTY(EditAnywhere, Category="Character", meta=(ClampMin="0.0", ClampMax="0.95"))
	float StepHeightCellFactor = 0.3f;

	// ── Camera (Task 11) ──────────────────────────────────
	// 클라 시각 전용 값이지만 튜닝 값은 룰셋에 모은다 (코딩 규칙 — 매직 넘버 금지).
	// ⚠️ 전부 임시 — PIE 튜닝 후 확정.

	// 카메라 붐 길이(셀 단위). TargetArmLength = 계수 × CellSize.
	UPROPERTY(EditAnywhere, Category="Camera")
	float CameraDistanceCells = 12.f;

	// 고정 내려보기 각도(도). 90도 스냅 회전은 yaw 만 돈다 (GDD 5장).
	UPROPERTY(EditAnywhere, Category="Camera")
	float CameraPitchDeg = -55.f;

	// 90도 스냅 보간 속도 (RInterpTo 지수 계수).
	UPROPERTY(EditAnywhere, Category="Camera")
	float CameraYawInterpSpeed = 8.f;

	// ─── 가림 디더 페이드 (2026-08-06) ───
	// 카메라와 내 캐릭터 사이를 막는 블록을 옅게 만든다. 카메라 붐을 당겨 해결하지 않는
	// 이유는 Task 11 에서 정했다 — 벽에 붙을 때마다 화면이 확대돼 칸 감각이 무너진다.

	// 가림 블록을 다시 계산하는 간격(초). 매 프레임 하지 않는다 (GDD 7.4 — "0.1초 간격").
	// 그 사이 페이드 자체는 매 프레임 부드럽게 이어진다 (아래 속도 값).
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin="0.0"))
	float OcclusionTraceInterval = 0.1f;

	// 페이드 진행 속도(초당 값). 6 이면 0 → 최대까지 약 0.17초.
	// 계산 간격(0.1초)보다 느리게 두면 계단이 보이지 않는다.
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin="0.1"))
	float OcclusionFadeSpeed = 6.f;

	// 최대 페이드 값 — 머티리얼의 PerInstanceCustomData 0번으로 들어간다.
	//
	// **1.0 으로 확정** (2026-08-06 사용자 체감). 1 미만이면 디더가 그 비율만큼 픽셀을 남기는데,
	// 그 잔존 픽셀이 구멍 안에 흩뿌려져 **노이즈로 보인다** (0.8 = 20% 잔존). 게다가 마스크가
	// 캐릭터를 따라 매 프레임 움직여서 TSR 이 누적으로 매끈하게 만들어 주지도 못한다.
	// 1.0 이면 중심부가 완전히 비고 디더는 가장자리 띠에만 남아 경계를 부드럽게 해준다.
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin="0.0", ClampMax="1.0"))
	float OcclusionFadeAmount = 1.f;

	// 캐릭터 쪽 샘플 지점 개수(1·3·5). 광선 하나만 쏘면 블록 모서리를 스쳐 지나가
	// 캐릭터가 반쯤 가린 채로 남는다. 5 = 중심 + 머리 + 발 + 좌우(캡슐 반지름만큼).
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin="1", ClampMax="5"))
	int32 OcclusionSampleCount = 5;

	// ─── 가림 구멍의 모양 (2026-08-06 사용자 확정: "몸통이 가려진 그 부분만") ───
	//
	// 블록을 통째로 지우지 않고, **화면에서 캐릭터를 덮는 픽셀만** 뚫는다.
	// 머티리얼이 "이 픽셀이 캐릭터 위인가"를 알아야 하므로 C++ 이 캐릭터의 화면 위치를
	// 매 프레임 여기에 써 넣는다. 동적 머티리얼 인스턴스가 아니므로 GDD 7.4 금지에 걸리지 않는다
	// (컬렉션 하나를 모든 블록 머티리얼이 공유한다 — 인스턴스가 몇 천 개든 파라미터는 한 벌).
	//
	// **미지정이어도 안전하다** — 그 경우 화면 마스크 없이 예전처럼 블록 단위로 페이드된다.
	// 만들 파라미터: 벡터 `OcclusionMask`(중심u, 중심v, 반지름u, 반지름v) · 스칼라 `OcclusionMaskSoftness`
	UPROPERTY(EditAnywhere, Category="Camera")
	TObjectPtr<UMaterialParameterCollection> OcclusionMaskCollection;

	// 구멍 크기 = 캡슐 크기 × 이 배수. 1.0 이면 몸에 딱 붙어 실루엣이 잘려 보인다.
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin="1.0", ClampMax="3.0"))
	float OcclusionMaskScale = 1.35f;

	// 구멍 가장자리가 흐려지는 폭 (반지름 대비 비율). 0 이면 칼로 자른 듯한 원이 된다.
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin="0.0", ClampMax="2.0"))
	float OcclusionMaskSoftness = 0.4f;

	// ─── 관전 (사망 후 생존자 추적 — 2026-08-06 사용자 확정: 자유 비행 없음, 좌/우로 순환) ───
	// 카메라 값이라 Camera 카테고리에 둔다. 관전은 **로컬 시점**일 뿐이라 서버가 읽지 않는다.

	// 관전 대상을 바꿀 때의 카메라 블렌드 시간(초). 0 이면 즉시 튄다.
	// 짧게 잡는 이유: 관전은 "다음 사람을 본다" 는 행동이라 이동 연출이 길면 정작 보려던
	// 장면을 놓친다. 반대로 0 이면 8명을 넘길 때 화면이 순간이동해 어디를 보는지 잃는다.
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin="0.0"))
	float SpectateBlendTime = 0.4f;

	// 좌/우 이동 축이 "한 번 눌렀다" 로 인정되는 크기(0~1). 사망 중에는 기존 이동 축을
	// 관전 전환으로 재해석하므로(새 IA 를 만들지 않는다) 눌림/뗌 판정이 필요하다.
	// 절반보다 크게 잡아 대각 입력의 약한 축이 한 칸을 더 넘기지 않게 한다.
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin="0.05", ClampMax="1.0"))
	float SpectateCycleAxisThreshold = 0.5f;

	// 봇의 관전 카메라 yaw 가 다음 90도 칸으로 넘어가기까지 필요한 초과 각도(도).
	// 2026-08-09: 봇에게는 "보고 있는 시점" 이 없어 이동 방향을 90도로 스냅해 대신 쓴다.
	// 히스테리시스가 0 이면 경계(45도)에서 방향이 미세하게 흔들릴 때마다 카메라가 45도씩
	// 튄다 — 관전 중 가장 눈에 띄는 멀미 요인이다. 크게 잡으면 봇이 확실히 방향을 튼 뒤에도
	// 카메라가 한참 안 돈다.
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin="0.0", ClampMax="22.0"))
	float SpectateBotCamYawHysteresisDeg = 12.f;

	// ── Map ───────────────────────────────────────────────

	// 바닥 블록 파괴 허용 여부. (미확정)
	UPROPERTY(EditAnywhere, Category="Map")
	bool bFloorDestructible = false;

	// 그리드 크기 (X, Y, 층).
	// ⚠️ **레거시 기본 경로 전용** (Task 22 이후) — 크기를 명시하지 않는 호출
	// (기존 테스트·AVoxelWorld::ServerInitFromSeed 단일 인자)이 계속 이 값을 쓴다.
	// 정식 매치는 GameMode 가 아래 인원별 티어(MapSizeSmall/Large)에서 골라 넘긴다.
	UPROPERTY(EditAnywhere, Category="Map")
	FIntVector MapSize = FIntVector(21, 21, 4);

	// ─── Map — 절차 생성 (Task 22, 2026-08-06 사용자 확정: 산·협곡 Z 6층 + 인원별 티어) ───
	// 아래 수치 전부 정수 — **맵 생성기가 소비**하므로 float 금지 (불변식 4, ItemDropPercent 주석과 같은 근거).

	// 이 인원 이하면 소형 맵. 예상 인원 = max(접속 인원, 봇 충원 목표) — GameMode 가 판정.
	UPROPERTY(EditAnywhere, Category="Map", meta=(ClampMin="1", ClampMax="8"))
	int32 SmallMatchMaxPlayers = 4;

	// 인원별 티어 크기 (X, Y, 층). Z 6층 — 계단식 고원(산)과 그 사이 틈(협곡)이 들어갈 높이.
	UPROPERTY(EditAnywhere, Category="Map")
	FIntVector MapSizeSmall = FIntVector(17, 17, 6);

	UPROPERTY(EditAnywhere, Category="Map")
	FIntVector MapSizeLarge = FIntVector(21, 21, 6);

	// 구조물(계단식 고원) 최대 높이(블록 수). 생성기가 층수에 맞게 Size.Z-2 로 클램프한다.
	UPROPERTY(EditAnywhere, Category="Map", meta=(ClampMin="1"))
	int32 ProcStructureMaxHeight = 3;

	// 맵 100칸당 구조물 개수 — 면적 비례 정수 스케일 (21×21=441칸 → 8개, 17×17=289칸 → 5개).
	UPROPERTY(EditAnywhere, Category="Map", meta=(ClampMin="0"))
	int32 ProcStructureCountPer100Cells = 2;

	// 복도 칸에 파괴 블록이 놓일 확률(정수 %). 구조물 꼭대기 스캐터도 같은 값을 쓴다.
	UPROPERTY(EditAnywhere, Category="Map", meta=(ClampMin="0", ClampMax="100"))
	int32 ProcDestructiblePercent = 35;

	// 생성→검증(Task 21) 불통과 시 파생 시드 리롤 상한. 초과하면 Generate 가 false 를 반환하고
	// 호출부(AVoxelWorld)가 폴백 생성기로 전환한다 (GDD 4.2 안전장치 3).
	UPROPERTY(EditAnywhere, Category="Map", meta=(ClampMin="1"))
	int32 ProcRerollMaxAttempts = 16;

	// ─── Map — 검증 임계값 (Task 21 FMapValidator 에 넘기는 값 — 하드코딩 금지) ───

	// 스폰 간 최소 맨해튼 거리. **21×21 기준값** — 생성기가 (Size.X+Size.Y)/42 정수 비례로
	// 스케일해 적용한다 (17×17 이면 6). 그대로 쓰면 소형 맵이 전부 리롤된다 (ProcMapGenerator 주석).
	UPROPERTY(EditAnywhere, Category="Map", meta=(ClampMin="0"))
	int32 ValidatorSpawnMinManhattan = 8;

	// 스폰 주변 탈출로 최소 방향 수 (수평 4방향 중).
	UPROPERTY(EditAnywhere, Category="Map", meta=(ClampMin="0", ClampMax="4"))
	int32 ValidatorSpawnMinEscapeDirs = 2;

	// 사분면 간 아이템 개수 최대 편차 — 한쪽 쏠림 방지.
	UPROPERTY(EditAnywhere, Category="Map", meta=(ClampMin="0"))
	int32 ValidatorItemQuadrantMaxDiff = 4;

	// ── Item (Task 23) ─────────────────────────────────────
	// ⚠️ 아래 수치 전부 잠정 — 아이템 스택 상한과 함께 밸런스 패스에서 확정 (CLAUDE.md "아직 안 정한 값").
	// 여기 값들은 **맵 생성기가 소비**한다 → 정수여야 한다 (불변식 4).

	// 파괴 블록 하나에 아이템이 숨겨질 확률(정수 퍼센트 0~100).
	// float(예전 ItemDropRate 0~1)가 아니라 정수 퍼센트인 이유: 맵 생성기 안에서는 float 연산이
	// 금지돼 있다 (불변식 4) — 부동소수 비교는 플랫폼·컴파일 옵션마다 경계값이 갈릴 수 있어
	// 리눅스 서버와 윈도우 클라의 아이템 배치가 어긋나면 진단이 매우 어렵다.
	UPROPERTY(EditAnywhere, Category="Item", meta=(ClampMin="0", ClampMax="100"))
	int32 ItemDropPercent = 30;

	// 종류별 추첨 가중치(정수). 합계에서 하나를 뽑는다 — 0 이면 그 종류는 안 나온다.
	// 기본값 합계 100 이라 그대로 "퍼센트"로 읽힌다 (30/30/20/5/15).
	// 니들만 낮게 잡은 것은 GDD 3장 "낮은 드랍률" — 갇힘 탈출은 판을 되돌리는 카드라
	// 흔하면 물방울에 가두는 플레이 자체가 무의미해진다. 실효 드랍률은
	// ItemDropPercent 30% × 5/100 = 파괴 블록 100개당 1.5개 수준.
	UPROPERTY(EditAnywhere, Category="Item", meta=(ClampMin="0"))
	int32 ItemWeightBalloon = 30;

	UPROPERTY(EditAnywhere, Category="Item", meta=(ClampMin="0"))
	int32 ItemWeightPotion = 30;

	UPROPERTY(EditAnywhere, Category="Item", meta=(ClampMin="0"))
	int32 ItemWeightRoller = 20;

	UPROPERTY(EditAnywhere, Category="Item", meta=(ClampMin="0"))
	int32 ItemWeightNeedle = 5;

	UPROPERTY(EditAnywhere, Category="Item", meta=(ClampMin="0"))
	int32 ItemWeightKick = 15;

	// 서버가 스폰할 아이템 픽업 클래스 — BP_ItemPickup(종류별 메시 지정용) 연결.
	// 미지정이면 C++ 기본 클래스로 폴백한다 (에셋 없이도 획득 판정은 정상 — 경고 로그만).
	// PredictedBombVisualClass 와 같은 관례.
	UPROPERTY(EditAnywhere, Category="Item")
	TSubclassOf<AItemPickup> ItemPickupClass;

	// 획득 판정 구체 반지름(셀 단위). 실제 반지름 = 이 계수 × AVoxelWorld::CellSize —
	// 셀 크기를 바꿔도 "그 칸을 밟으면 먹는다" 는 감각이 유지된다 (하드코딩 금지).
	// 0.4 = 셀 중심에서 0.4칸. 캐릭터 캡슐 반지름(34)과 합쳐 약 0.74칸 안에 들면 획득.
	UPROPERTY(EditAnywhere, Category="Item", meta=(ClampMin="0.05", ClampMax="1.0"))
	float ItemPickupRadiusCells = 0.4f;

	// ── Visual (2026-08-06) ───────────────────────────────

	// 맵에 놓인 물건(폭탄·예측 폭탄·아이템) 메시가 제자리에서 도는 속도(초당 도, yaw).
	// 0 이면 안 돈다. 카메라 각도가 바뀌어도 눈에 띄게 하려는 값이라 세 종류가 **같은 값**을
	// 쓴다 — 폭탄과 예측 폭탄의 회전이 갈리면 서버 확정 순간 각도가 튄다 (Gameplay/SpinVisual.h).
	// 45 = 8초에 한 바퀴 (설치→폭발 3초 동안 약 3/8바퀴 — "천천히 계속").
	UPROPERTY(EditAnywhere, Category="Visual", meta=(ClampMin="0.0"))
	float PickupSpinDegreesPerSecond = 45.f;

	// ── Match (Task 18) ───────────────────────────────────

	// 승패 판정을 시작할 최소 참가 인원. 1인 PIE 테스트에서 매치가 즉시 끝나는 것을 막는다.
	UPROPERTY(EditAnywhere, Category="Match")
	int32 MinPlayersForMatchEnd = 2;

	// ── Bot (Task 20) ─────────────────────────────────────
	// 봇은 "인원을 채워 한 판을 완주시키는" 수단이다 (헤드리스 데디에서 사람 8명은 모을 수 없다).

	// 인원 부족분을 봇으로 채울지. **기본 false** — 켜져 있으면 기존 PIE·자동화 테스트의
	// 인원수가 조용히 바뀌어(승패 판정 게이트·스폰 셀 배정) 원인 찾기 어려운 차이를 만든다.
	// 켜는 방법은 두 가지: 룰셋 에셋에서 체크, 또는 콘솔 변수 `ca3d.BotFill <N>`
	// (헤드리스 데디는 -ExecCmds="ca3d.BotFill 4" — 콘솔 변수가 이 값을 덮어쓴다).
	UPROPERTY(EditAnywhere, Category="Bot")
	bool bFillWithBots = false;

	// 봇까지 포함해 맞출 총 인원. (⚠️ 잠정 — 4인 기준으로 잡았다. 8인 완주 테스트 때 재확인)
	UPROPERTY(EditAnywhere, Category="Bot", meta=(ClampMin="0", ClampMax="8"))
	int32 BotFillTargetPlayers = 4;

	// 매치 시작 후 봇을 채우기까지의 대기(초). 사람이 먼저 들어와야 "부족분"이 정해진다.
	// -ExecCmds 로 넘긴 콘솔 변수가 반영될 시간을 버는 역할도 겸한다.
	// (⚠️ 잠정 — 실제 로비(Task 19)가 들어오면 "전원 준비 완료" 신호로 대체될 값이다.)
	UPROPERTY(EditAnywhere, Category="Bot", meta=(ClampMin="0.0"))
	float BotFillDelaySeconds = 3.f;

	// FSM 재계획 주기(초). **매 틱 BFS 를 돌리지 않기 위한 값** — 봇 8대가 매 틱 탐색하면
	// 서버 틱이 튄다. 이 주기 사이에는 캐시된 경로를 따라간다.
	// (⚠️ 잠정 — 짧을수록 똑똑해 보이고 길수록 싸다. PIE stat unit 으로 확정)
	UPROPERTY(EditAnywhere, Category="Bot", meta=(ClampMin="0.05", ClampMax="2.0"))
	float BotReplanInterval = 0.4f;

	// 봇의 폭탄 설치 최소 간격(초). 난이도 조절이 아니라 **연속 설치로 자기 탈출로를 스스로
	// 막는 것**을 줄이는 값이다 (설치 판단은 매번 탈출로를 확인하지만, 놓는 순간 지형은
	// 아직 안 바뀌었고 다음 폭탄이 그 길을 덮을 수 있다). (⚠️ 잠정)
	UPROPERTY(EditAnywhere, Category="Bot", meta=(ClampMin="0.0"))
	float BotBombCooldown = 2.f;

	// BFS 탐색 노드 상한. 상한이 있어야 지형이 뚫려 탐색 공간이 커져도 서버 비용이 예측 가능하다.
	//
	// **512 → 1024 상향 (봇 층간 이동 개선).** 근거가 둘 다 Task 22 에서 바뀌었다:
	//   ① 맵이 Z 6층이 되면서(MapSizeSmall/Large) "설 수 있는 칸"이 한 층에 갇히지 않는다 —
	//      21×21 절차 맵의 도달 가능 칸은 평지 + 산 표면을 합쳐 400칸을 넘긴다.
	//   ② 봇이 **낙하 무제한**으로 걷게 되면서 고지대에서 한 번에 맵 반대편까지 이어진다.
	// 512 로 두면 그 지형에서 탐색이 중간에 잘려 먼 상대를 "도달 불가"로 보고 Wander 로 떨어진다
	// (상한에 걸린 BFS 는 실패와 구분되지 않는다). 노드당 비용은 방향 4개 × 기둥 스캔(≤6칸)이라
	// 두 배로 늘려도 한 자릿수 마이크로초다. (⚠️ 잠정 — stat unit 실측으로 확정)
	UPROPERTY(EditAnywhere, Category="Bot", meta=(ClampMin="16"))
	int32 BotMaxPathNodes = 1024;

	// ─── Bot — 갇힌 상대 노리기 (2026-08-10 사용자 확정) ───
	//
	// 봇이 갇힌 상대(ELifeState::Trapped)를 터뜨리러 가는 최대 거리(칸).
	// **실제로 걸어야 하는 걸음 수**로 잰다 — 직선 거리가 아니다. 벽을 빙 돌아야 하는 상대는
	// 가까워 보여도 도착 전에 갇힘이 끝난다.
	//
	// 무제한이면 21×21 맵에서 살아 있는 봇 전원이 갇힌 한 명에게 수렴한다 — 갇히는 순간이
	// 사실상 사형 선고가 된다(사용자에게 이 우려를 전했고, 그래도 넣는 것으로 확정됐다).
	// 그래서 노브를 둔다.
	//
	// **기본 16 의 근거**: TrappedDuration(4초) × MoveSpeedCellsPerSec(4칸/초) = 16칸 —
	// 그보다 먼 목표는 **도착하기 전에 물방울이 스스로 끝난다**(익사하거나 니들로 탈출한다).
	// 즉 기본값은 "물리적으로 닿을 수 있는 최대치"이고, 이 노브는 실제로 플레이해 보고
	// **줄이기 위한** 손잡이다 (몰려드는 게 과하면 8 → 5 순으로 내린다). 기능을 죽이는 값이 아니다.
	UPROPERTY(EditAnywhere, Category="Bot", meta=(ClampMin="1"))
	int32 BotPopTrappedMaxCells = 16;

	// ─── Bot — 아이템 줍기 (2026-08-10 사용자 확정: "가까이 있을 때만 줍기") ───
	//
	// 봇이 아이템을 **목표로 삼아** 주우러 가는 최대 거리(칸). 위와 같이 **실제 걸음 수**로
	// 잰다 — 벽 너머로 3칸이어도 돌아서 15걸음이면 가까운 것이 아니다.
	// 이 거리를 넘는 아이템은 목표가 되지 않을 뿐이고, 지나가다 밟으면 여전히 먹는다.
	//
	// **기본 6 의 근거** — "가까이" 라는 의도가 살아야 한다:
	//   · 6칸 ÷ MoveSpeedCellsPerSec(4) = 1.5초. 폭탄 퓨즈(3초)의 절반이라 "하나 줍고
	//     제자리로 돌아올" 수 있는 거리다. 이보다 멀면 파밍하러 간 사이에 판이 바뀐다.
	//   · 21×21 맵 대각선이 40걸음쯤이므로 6칸은 그 15% — 화면 안, 눈앞이다.
	//   · 초반에는 파괴 블록이 사방이라 6칸 안에 아이템이 자주 뜨고(자연스러운 파밍),
	//     후반에는 드물어 자연히 싸움이 중심이 된다. 사용자가 원한 흐름이 이 값에서 나온다.
	// 파밍이 너무 소극적으로 느껴지면 올리는 손잡이다 (8 → 10). 20 을 넘기면 "가까이"가 아니다.
	UPROPERTY(EditAnywhere, Category="Bot", meta=(ClampMin="1"))
	int32 BotSeekItemMaxCells = 6;

	// ── SuddenDeath ───────────────────────────────────────

	// 서든데스 발동 시각(초).
	UPROPERTY(EditAnywhere, Category="SuddenDeath")
	float SuddenDeathStart = 150.f;

	// 블록 낙하 예고 시간(초).
	UPROPERTY(EditAnywhere, Category="SuddenDeath")
	float DropWarningTime = 1.5f;

	// 외곽 셀 낙하 선택 가중치. 중심 셀을 1.0 으로 보고 **최외곽 셀이 몇 배로 잘 뽑히는가** —
	// 중심 거리에 비례해 선형 보간된다 (USuddenDeathSubsystem::PickDropCell).
	// 1.0 이면 균등, 2.0 이면 최외곽이 중심의 2배. 바깥부터 무너뜨려 판을 좁히기 위한 값이다.
	UPROPERTY(EditAnywhere, Category="SuddenDeath")
	float OuterWeightBias = 2.f;

	// ── SuddenDeath (Task 24, 2026-08-04 사용자 확정) ─────────

	// **서든데스 낙하만** 바닥(z=0)을 부순다. 일반 폭탄은 위의 bFloorDestructible(=false)을 그대로 따른다.
	// 이 둘을 분리한 이유가 서든데스의 전부다: 초반 SuddenDeathStart 초 동안은 발판이 보장되어
	// 폭탄으로는 절대 구멍이 나지 않고, 서든데스가 시작돼야 비로소 바닥이 뚫려 낙사가 시작된다.
	// 한 값으로 합치면 "폭탄으로 발판이 사라지는 판" 이 되어 GDD 2.3(발판만이 안전하다)이 무너진다.
	UPROPERTY(EditAnywhere, Category="SuddenDeath")
	bool bSuddenDeathDestroysFloor = true;

	// 낙하 주기(초) — 한 웨이브에서 다음 웨이브까지.
	// DropWarningTime 보다 짧아도 된다 (웨이브가 겹쳐 예고 중인 웨이브가 여러 개가 될 뿐).
	UPROPERTY(EditAnywhere, Category="SuddenDeath", meta=(ClampMin="0.05"))
	float DropInterval = 1.f;

	// 주기당 낙하 개수. 맵 축소 속도를 올리려면 주기를 줄이는 대신 이 값을 올린다 —
	// 같은 웨이브의 예고가 한 번에 보여서 "피할 수 있는가" 라는 요건이 유지된다.
	UPROPERTY(EditAnywhere, Category="SuddenDeath", meta=(ClampMin="1"))
	int32 DropsPerWave = 1;

	// 낙하 폭발 범위(칸). 폭탄 기본(InitialBombRange=1)보다 넓게 두어 낙하 한 발이
	// 기둥을 관통해 바닥까지 닿게 한다 (원점 위 칸 → 아래로 2칸이면 블록 1장 + 바닥).
	UPROPERTY(EditAnywhere, Category="SuddenDeath", meta=(ClampMin="0"))
	int32 DropExplosionRange = 2;

	// 낙하 예고 마커 클래스 (클라 시각 — 풀링 대상). 예: BP_DropMarker.
	// 미지정이면 마커 없이 진행한다 — 예고가 안 보일 뿐 낙하·판정은 정상 (경고 로그 1회).
	// WaterSegmentClass·DangerDecalClass 와 같은 관례.
	UPROPERTY(EditAnywhere, Category="SuddenDeath")
	TSubclassOf<ASuddenDeathDropMarker> DropMarkerClass;

	// ─── Status (Task 12) ───
	// ⚠️ 아이템 스택 상한은 미확정 항목 — 아래 두 값은 임시. 사용자 확정 시 갱신.

	// 롤러 1개당 이동속도 배율 증가량. (값 미확정 — 임시)
	UPROPERTY(EditAnywhere, Category="Status")
	float RollerSpeedStep = 0.15f;

	// 이동속도 배율 상한 (롤러 스택 상한 역할). (값 미확정 — 임시)
	UPROPERTY(EditAnywhere, Category="Status")
	float MoveSpeedMulCap = 1.6f;

	// ── CharacterSelect (Task 36, 2026-08-14 사용자 확정) ──────────
	// 매치 시작 전 캐릭터 선택 페이즈. 8종 중 택1, **중복 불가(선착순)**, 페이즈 중 재선택 허용.
	// 미선택자·봇·페이즈 종료 후 입장자는 남은 풀에서 자동 배정 (ACA3DGameMode 가 구동).

	// 선택 가능한 캐릭터 목록 — 배열 인덱스가 곧 ACA3DPlayerState::CharacterIndex 다.
	// **비어 있으면 Duration 이 있어도 페이즈를 스킵한다** (GameMode 가 경고 로그 1회) —
	// 에셋(8종 메시·애님)을 에디터에서 연결하기 전에도 기존 흐름이 그대로 돌아야 한다.
	UPROPERTY(EditAnywhere, Category="CharacterSelect")
	TArray<FCA3DCharacterDef> Characters;

	// 선택 페이즈 길이(초). 0 = 페이즈 스킵(기존 흐름 그대로 즉시 시작).
	// ⚠️ **C++ 기본값이 0 인 것은 무회귀가 목적이다** — 룰셋을 주입만 하는 기존 자동화
	// 테스트와 기존 실행 흐름(BeginPlay → 즉시 스폰·봇/서든데스 타이머 예약)이 이 Task 뒤에도
	// 한 줄도 달라지면 안 된다 (bFillWithBots 기본 false 와 같은 근거 — 조용히 켜져 있으면
	// 원인 찾기 어려운 차이를 만든다). 실제 10초는 에디터의 DA_Rules_Default 에서 지정한다.
	UPROPERTY(EditAnywhere, Category="CharacterSelect", meta=(ClampMin="0.0"))
	float CharacterSelectDuration = 0.f;

	// ── Lobby (Task 41, 2026-08-16 사용자 확정) ────────────────────
	// "사람이 모이고 게임 시작 버튼이 눌린 다음에 캐릭터 선택창이 진행된다."
	// 처음 들어온 사람이 방장, 나머지는 준비 버튼. **모두 준비해야** 방장이 시작을 누를 수 있다.
	// 로비에는 **시간 제한이 없다** — 방장이 누를 때까지 기다린다(타이머 없음).

	// 매치 시작 전 로비 페이즈를 쓰는가.
	// ⚠️ **C++ 기본값이 false 인 것은 무회귀가 목적이다** — 위 CharacterSelectDuration 기본 0 과
	// 완전히 같은 전략이다. 기존 자동화 스위트와 기존 실행 흐름(BeginPlay → 곧바로 선택 페이즈
	// 또는 즉시 시작)이 이 Task 뒤에도 한 줄도 달라지면 안 된다. 실제 true 는 에디터의
	// DA_Rules_Default 에서 지정한다.
	// (EditAnywhere 인 이유: 데이터 에셋 인스턴스는 템플릿이 아니라 EditDefaultsOnly 필드가
	//  에셋 에디터에 안 뜬다 — 이 클래스의 다른 튜닝 값 전부와 같은 사정.)
	UPROPERTY(EditAnywhere, Category="Lobby")
	bool bUseLobby = false;

	// ── Feedback (사운드·이펙트 슬롯) ─────────────────────────────
	//
	// 각 줄이 **사건 하나**다 (Gameplay/CA3DFeedback.h 의 ECA3DCue). 소리와 이펙트를 한 쌍으로
	// 묶어 둔 이유: 재생이 한 호출로 나가야 한 사건에서 소리와 이펙트가 따로 노는 일이 없다.
	//
	// **전부 비워 두어도 게임은 정상 동작한다** — 미지정 큐는 조용히 넘어가고 큐당 한 번만
	// Verbose 로그를 남긴다 (WaterSegmentClass·DropMarkerClass 와 같은 관례).
	//
	// TMap<ECA3DCue, ...> 이 아니라 이름 있는 개별 필드인 이유: 여기 값은 사람이 에디터에서
	// 채우는 것이라 **필드 이름이 곧 설명**이어야 하고, TMap 은 "키를 고른다" 는 단계가 하나
	// 더 붙는 데다 키를 빠뜨려도 컴파일러가 알려 주지 않는다.
	//
	// 튜닝 노브는 아래 볼륨 배수 하나뿐이다. 감쇠(3D 음향 — GDD 5장)·동종 사운드 동시 재생
	// 상한(GDD 7.4 의 4~5개)·랜덤 피치는 **전부 에셋 쪽 일**이다: 각각 Attenuation 에셋,
	// Sound Concurrency 에셋, Sound Cue 가 처리한다. C++ 에서 또 세면 규칙이 두 벌이 된다.
	//
	// ⚠️ 감쇠를 지정하지 않은 사운드는 2D 로 재생된다 — MatchEnd 처럼 위치가 없는 큐는
	// 감쇠를 비워 두면 그대로 화면 전체 소리가 된다 (코드에서 분기할 필요 없음).

	// 폭탄이 눈앞에 생긴 순간. 설치자는 로컬 예측 시점에, 나머지는 서버 확정 도착 시점에 —
	// **한 폭탄에 정확히 한 번**만 난다 (ABomb::BeginPlay 가 예측을 회수했으면 내지 않는다).
	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<USoundBase> BombPlaceSound;

	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<UNiagaraSystem> BombPlaceFX;

	// 물줄기가 퍼지는 순간. 연쇄 **1단계당 1회** — 셀마다가 아니다 (물줄기 세그먼트 FX 는
	// 별개로 AWaterSegment 가 셀마다 낸다). 서든데스 낙하 폭발도 같은 경로라 이 큐를 쓴다.
	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<USoundBase> ExplosionSound;

	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<UNiagaraSystem> ExplosionFX;

	// 블록이 부서진 순간. 파괴 **1묶음당 1회** — 한 폭발에 20칸이 함께 부서져도 한 번이고,
	// 위치는 그 칸들의 무게중심이다.
	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<USoundBase> BlockBreakSound;

	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<UNiagaraSystem> BlockBreakFX;

	// 아이템을 주운 순간 (아이템 위치). 물줄기에 타 없어진 것은 획득이 아니므로 나지 않는다.
	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<USoundBase> ItemPickupSound;

	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<UNiagaraSystem> ItemPickupFX;

	// 물방울에 갇힌 순간 (그 캐릭터 위치). 남이 갇혀도 난다 — 판을 읽는 정보다.
	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<USoundBase> TrappedSound;

	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<UNiagaraSystem> TrappedFX;

	// 니들로 갇힘에서 빠져나온 순간.
	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<USoundBase> EscapeSound;

	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<UNiagaraSystem> EscapeFX;

	// 사망한 순간 (익사·낙사·서든데스 공통 — 원인별로 나누지 않는다).
	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<USoundBase> DeathSound;

	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<UNiagaraSystem> DeathFX;

	// 폭탄을 차 보낸 순간 (폭탄 위치). 미끄러지는 내내가 아니라 **시작 1회**다.
	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<USoundBase> KickSound;

	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<UNiagaraSystem> KickFX;

	// 서든데스 낙하 예고가 뜬 순간. 웨이브당 1회 — DropsPerWave 를 올려도 소리는 한 번이다.
	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<USoundBase> SuddenDeathWarnSound;

	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<UNiagaraSystem> SuddenDeathWarnFX;

	// 매치가 끝난 순간 (우승·무승부 공통, 매치당 1회). 위치가 없는 큐 —
	// 감쇠를 비운 2D 사운드를 넣는 것이 전제다.
	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<USoundBase> MatchEndSound;

	UPROPERTY(EditAnywhere, Category="Feedback")
	TObjectPtr<UNiagaraSystem> MatchEndFX;

	// 모든 큐에 곱해지는 볼륨 배수. 개별 소리의 크기는 에셋(또는 Sound Class)에서 잡고,
	// 이 값은 "게임 효과음 전체가 조금 크다/작다" 를 한 번에 미는 용도다.
	UPROPERTY(EditAnywhere, Category="Feedback", meta=(ClampMin="0.0", ClampMax="4.0"))
	float FeedbackVolumeMultiplier = 1.f;
};
