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

	// 고정 내려보기 각도(도). 45도 스냅 회전은 yaw 만 돈다 (GDD 5장).
	UPROPERTY(EditAnywhere, Category="Camera")
	float CameraPitchDeg = -55.f;

	// 45도 스냅 보간 속도 (RInterpTo 지수 계수).
	UPROPERTY(EditAnywhere, Category="Camera")
	float CameraYawInterpSpeed = 8.f;

	// ── Map ───────────────────────────────────────────────

	// 바닥 블록 파괴 허용 여부. (미확정)
	UPROPERTY(EditAnywhere, Category="Map")
	bool bFloorDestructible = false;

	// 그리드 크기 (X, Y, 층).
	UPROPERTY(EditAnywhere, Category="Map")
	FIntVector MapSize = FIntVector(21, 21, 4);

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

	// BFS 탐색 노드 상한. 21×21×4 맵의 한 층이 441칸이므로 512면 같은 층 대부분을 덮는다.
	// 상한이 있어야 지형이 뚫려 탐색 공간이 커져도 서버 비용이 예측 가능하다. (⚠️ 잠정)
	UPROPERTY(EditAnywhere, Category="Bot", meta=(ClampMin="16"))
	int32 BotMaxPathNodes = 512;

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
};
