# Checklist 39 — Task 37·38 보정 3종 (사망 길이·인풋 차단·메시 캐시)

> 2026-08-14 사용자 지적 3건: ① "각자 죽는 애니메이션 시간이 다 다른데" 전역 딜레이 하나로 숨김 ·
> ② 폭탄을 상한까지 깐 상태에서 설치 키를 누르면 Attack 몽타주가 헛나감 — "인풋 자체에서 막아야지" ·
> ③ "클라마다 스폰된 애들의 메시 위치가 약간 다르고 방향도 다를 때가 존재"
> 신규 스위트 없음 — `PredictedBombVisual`·`CharacterAppearance`·`DeathHandling` 확장

## ① 사망 숨김 = 캐릭터별 DeathMontage 실측 길이

- [x] 지적이 맞았다: 8종 Die 애님 길이가 제각각이라 전역 `DeathHideDelay` 는 구조적으로 못 맞는다
- [x] **재생하는 것이 곧 타이머의 출처** — `FCA3DCharacterDef::DeathMontage` 를 `ApplyDeathState`
      시각 구간에서 C++ 가 직접 재생, `PlayAnimMontage` **반환 길이**(재생 속도 반영)로
      `HideAfterDeath` 타이머. 수치를 손으로 입력하지 않으므로 애님을 바꿔도 안 어긋난다
- [x] 몽타주는 슬롯으로 상태 머신(bDead→Die)을 덮고, 끝나는 순간 숨기므로 상태 머신 포즈로
      되돌아가는 프레임이 안 보인다
- [x] `DeathHideDelay` 는 **폴백으로 강등** — 몽타주 미지정·인덱스 미확정·재생 실패(Duration 0,
      헤드리스 테스트 포함)만 탄다. 기존 테스트가 곧 폴백 스펙 (코드 무변경, 주석만 갱신)

## ② 폭탄 상한 = 인풋(예측) 단계 차단

- [x] 원인: `ActiveBombCount` 가 서버 전용 비복제 → 원격 클라의 로컬 검증(`bHasSlot`)이 예측
      비주얼 수만 세어, 예측이 확정 회수된 뒤에는 상한인데도 통과 → 몽타주·설치음 헛발동 후 서버 거부
- [x] **`ActiveBombCount` 를 COND_OwnerOnly 복제** — 판정은 여전히 서버 단독(불변식 5), 소유
      클라는 읽기 전용 사본. 검증식(`ActiveBombCount + 예측 수 < MaxBombCount`)은 한 글자도 안
      바꾸고 정확해진다. 관전자·타 클라는 볼 이유가 없어 소유자 한정
- [x] 복제 지연으로 [ABomb 확정 ↔ 카운트 복제] 도착 순서가 어긋나는 한 프레임의 **과차단은
      안전측**(서버도 거부할 요청) — 주석 명시
- [x] 부수 이득: HUD(MatchWidget)의 폭탄 수 표시가 원격 클라에서도 정확해짐 (낡은 주석 갱신)

## ③ 클라별 메시 위치·방향 어긋남 = CMC 스무딩 캐시

- [x] 원인 (엔진 소스 확정): `SmoothClientPosition_UpdateVisuals`(CharacterMovementComponent.cpp:8563·
      8594)가 시뮬레이티드 프록시에서 **매 프레임** 메시 상대 트랜스폼을 "스무딩 오프셋 +
      `GetBaseTranslationOffset`/`GetBaseRotationOffset`" 으로 재작성한다. 그 Base 캐시는
      `PostInitializeComponents` 시점 값 — Task 37 의 외형 적용이 캐시를 안 갱신해 **원격
      프록시에서만** 낡은 오프셋으로 되돌아갔다 (내 화면의 내 폰은 멀쩡 — "클라마다 다르다"의 정체)
- [x] 수정: 적용 직후 같은 위치·회전으로 `ACharacter::CacheInitialMeshOffset()` — 엔진 주석이
      정확히 이 용도("call this at runtime if you intend to change the default mesh offset")
- [x] 테스트: 적용 후 `GetBaseTranslationOffset`/`GetBaseRotationOffset` == 적용값 + 인덱스 변경 시 추종

## 검증 (2026-08-14)

- [x] 두 타깃 빌드 `Result: Succeeded` — 서브에이전트 + 오케스트레이터 재확인
- [x] **전체 33스위트 실패 0, exit 0** — 오케스트레이터 직접 재실행
- [x] `PredictedBombVisual` ⑫ 신규(`ActiveBombCount` CPF_Net + 부모 대비 7개 등록) ·
      `CharacterAppearance` ③-c/e 신규(Base 캐시 추종)

## 남은 검증 (미실행 — 체크 금지)

- [ ] 리슨/데디 + 클라 2: 원격 화면에서 8종 메시가 전부 같은 자리·같은 방향인가 (③의 실전 확인)
- [ ] 상한 상태 설치 연타 시 몽타주·설치음이 완전히 침묵하는가 (②)
- [ ] 8종 각각 죽는 순간부터 사라질 때까지 Die 가 끝까지 재생되는가 (①)

## 에디터에서 할 일

- [ ] **DeathMontage 8종 제작** — 각 Die 애님(`Animation/Polyart/…`) 우클릭 → Create → AnimMontage
      (Mushroom 은 `DieSmile`). 슬롯은 AttackMontage 와 같은 DefaultSlot
- [ ] `DA_Rules_Default`: `Characters[i] → Death Montage` 8종 지정 — 지정 전까지는
      `Life → Death Hide Delay`(폴백) 로 동작
