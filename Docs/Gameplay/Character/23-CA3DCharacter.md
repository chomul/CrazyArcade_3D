# ACA3DCharacter

> `Gameplay/Character/CA3DCharacter.h/.cpp` · ACharacter — 사람·봇 공용

## 역할
- 행동 진입점: `Move`/`DoJump` · `TryPlaceBombPredicted` · `TryUseNeedle` ·
  킥 시도(매 틱) · 갇힌 상대 터뜨리기 검사
- 판정 기준점: `GetFootCell()`(발밑 셀) · `GetContactReach()`(접촉 거리)
- 사망 적용(`ApplyDeathState`) · 시점 각 결정(`GetViewRotation`)
- 스탯 보관 안 함 — `UStatusComponent` 소관

## 왜
- **왜 사람·봇 공용?** → 봇 전용 경로 = 검증 두 벌. 사람이 못 하는 건 봇도 못 함
- **왜 발밑 셀이 판정 중심?** → "발판만이 안전하다": 층 이동 점프 = 셀 바뀜 = 회피,
  제자리 점프 = 셀 그대로 = 피격
- **왜 이동 수치가 계수×CellSize?** → 셀 크기 변경에도 감각 유지 + 매직 넘버 금지
- **왜 ApplyDeathState가 공통 단일 지점?** → 컬리전·이동 모드는 비복제 값 —
  서버·클라 각자 같은 함수 실행. 숨김은 **액터 단위**(메시 하나만 끄면 BP 추가 메시가
  남음 — 실제 사고). 폰 파괴 안 함 — 부활 여지
- **왜 Move는 Dead만 차단, DoJump는 Alive만 통과?** → 갇힘 중 미세 이동 허용·점프 탈출
  금지 (사용자 확정). 가드가 캐릭터에 있는 이유: 봇도 같은 경로
- **왜 GetViewRotation 오버라이드?** → 카메라 yaw 비복제 → 원격 폰은 0도(카메라 눕는 버그).
  보는 로컬 컨트롤러의 각 우선, 없으면 복제 `CamYawIndex`
- **왜 GetContactReach 공용?** → 킥·터뜨리기가 한 벌. 두 벌이면 "킥은 되는데
  터뜨리기는 안 되는 거리"
- **공중 설치 = -Z 스캔** (잠정) → 아래 첫 솔리드 위 셀. 그리드 밖이면 RPC 자체 생략

## 네트워크
RPC 3: `ServerPlaceBomb` · `ClientRejectBomb` · `ServerUseNeedle`. 자체 복제 없음(CMC 기본)

## 연결
[24-StatusComponent.md](24-StatusComponent.md) · [18-PredictedBombVisual.md](../Bomb/18-PredictedBombVisual.md) · [26-CA3DPlayerController.md](26-CA3DPlayerController.md) · [37-BotController.md](../../AI/37-BotController.md)

## Q&A
아직 없음
