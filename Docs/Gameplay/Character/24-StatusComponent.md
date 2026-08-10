# UStatusComponent

> `Gameplay/Character/StatusComponent.h/.cpp` · UActorComponent

## 역할
- 스탯 보관·복제: 폭탄 수·범위·속도 배수·니들·킥
- 생존 상태 전이: Alive → Trapped(4초 익사 타이머) → 탈출 or 사망(사인 기록)
- 아이템 효과 적용(`ServerApplyItem`) + 판정 짝(`HasRoomForItem`)
- 사망을 GameMode에 통지. 순위 판정은 안 함

## 왜
- **왜 컴포넌트?** → 설계 결정 8: 봇·사람 코드 경로 동일. 캐릭터=행동, 컴포넌트=상태
- **왜 RPC 0개?** → 전부 서버 로컬 진입점 + `HasAuthority()` 가드.
  클라 요청은 캐릭터 RPC가 받음. 상태 변경 표면 최소화 (불변식 5)
- **왜 ActiveBombCount 비복제?** → 서버 판정 전용. 클라 예측은 예측 비주얼 개수로 —
  복제 지연 값으로 예측하면 오히려 틀림
- **왜 ServerTrap이 Alive만?** → 중복 갇힘·시체 갇힘 방지 + "갇힌 상대에게 폭탄 무효"
  원작 규칙이 공짜 (봇 PopTrapped 우선순위의 근거)
- **왜 EDeathCause 구분?** → 사인 집계·밸런스 신호(예: "익사 0건"). 새 값은 끝에 append
- **왜 속도 재계산이 단일 경로?** → 롤러·갇힘·사망이 제각각 만지면 복원이 꼬임
- **왜 HasRoomForItem이 여기?** → `ServerApplyItem` 바로 옆. 봇 파일에 있으면
  "봇은 상한인 줄 아는데 실제로는 오르는" 어긋남

## 네트워크
복제 6: 스탯 5 + `LifeState`(OnRep_Life → ApplyDeathState 공통 경로)

## 연결
[23-CA3DCharacter.md](23-CA3DCharacter.md) · 갇힘 호출: [16-ExplosionSubsystem.md](../Bomb/16-ExplosionSubsystem.md) · 통지: [35-CA3DGameMode.md](../../Framework/35-CA3DGameMode.md)

## Q&A
아직 없음
