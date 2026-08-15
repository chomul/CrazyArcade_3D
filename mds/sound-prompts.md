# 사운드 제작 프롬프트 — 컨셉 + 큐별 생성 프롬프트

> 대상: `DA_Rules_Default` → `Feedback` 카테고리의 10개 사운드 슬롯 (체크리스트 31-⑤).
> AI 사운드 생성기(ElevenLabs SFX·Stable Audio 등)는 영어 프롬프트가 품질이 좋아
> **프롬프트는 영어**, 설명은 한글로 병기한다. 길이·루프 여부는 프롬프트에 함께 넣을 것.

## 게임 사운드 컨셉 (모든 프롬프트의 공통 전제)

- **불이 아니라 물이다.** 폭탄은 물풍선, 폭발은 물줄기 — "explosion"이라고만 쓰면
  화약 폭발음이 나온다. 반드시 water/splash/bubble 계열로 유도한다.
- **귀엽고 캐주얼.** 아트가 Polyart 로우폴리 몬스터 8종이다. 리얼·호러·밀리터리 톤 금지.
  키워드: `cute, cartoonish, playful, bouncy, family-friendly, arcade game`
- **짧고 또렷하게.** 내려보기 시점에서 여러 사건이 겹치므로 효과음은 0.3~1.5초의
  원샷이 기본. 꼬리(reverb tail)가 길면 Concurrency 로 잘라도 뭉개진다.
- 공통 스타일 접미사 (모든 프롬프트 끝에 붙이면 톤이 통일된다):
  `— cute casual arcade game SFX, cartoonish, clean studio quality, no music, no voice`

## 큐별 프롬프트

| # | 룰셋 필드 | 언제 나는가 | 길이 | 프롬프트 (영어) |
|---|---|---|---|---|
| 1 | `BombPlaceSound` | 폭탄 설치 (설치자는 예측 시점 즉시) | 0.3~0.5s | `A soft rubbery water balloon being plopped down on the ground, single short squishy "boing" with a wet undertone, light and bouncy` |
| 2 | `ExplosionSound` | 폭발 — 연쇄 1단계당 1회 | 0.8~1.2s | `A big water balloon bursting, powerful wet "splash-pop" followed by a short spray of water jets, punchy but cheerful, not a fire explosion` |
| 3 | `BlockBreakSound` | 블록 파괴 — 여러 칸이어도 1회 | 0.4~0.7s | `Light toy blocks crumbling and popping apart, a quick cluster of hollow wooden voxel pieces breaking, crisp and satisfying, low rumble kept minimal` |
| 4 | `ItemPickupSound` | 아이템 획득 | 0.3~0.5s | `A bright cheerful pickup chime, short ascending two-note sparkle "ding-ding", rewarding and cute` |
| 5 | `TrappedSound` | 물방울에 갇힘 | 0.6~1.0s | `A character being swallowed into a big wobbling water bubble, gulping "blub-wobble" sound with muffled underwater tone at the end, comical not scary` |
| 6 | `EscapeSound` | 니들로 탈출 | 0.4~0.6s | `A needle popping a big water bubble, sharp quick "pop!" with a small splash and a relieved springy release, triumphant and light` |
| 7 | `DeathSound` | 사망 | 0.8~1.2s | `A cute cartoon character defeat sound, a wet bubble burst followed by a short comical descending slide-whistle style "wah-wah" tone, funny not sad` |
| 8 | `KickSound` | 폭탄을 참 | 0.3~0.5s | `Kicking a big rubbery water balloon, short punchy "boing-thump" with a wet wobble as it rolls away, snappy and playful` |
| 9 | `SuddenDeathWarnSound` | 서든데스 낙하 예고 — 웨이브당 1회 | 0.5~0.8s | `An urgent but cute warning beep, two quick rising alarm blips with a slight cartoon siren feel, tense and attention-grabbing without being harsh` |
| 10 | `MatchEndSound` | 매치 종료 (2D — 감쇠 금지) | 1.5~2.5s | `A short victory fanfare jingle, bright brass-and-bells arcade "match complete" flourish, celebratory and cute, ends cleanly` |

## 톤을 가르는 규칙 3개

1. **물 소리 3형제를 구분한다** — 설치(1)는 "놓는" 소리, 폭발(2)은 "터지는" 소리,
   갇힘(5)은 "삼켜지는" 소리. 셋 다 물이라 대충 만들면 서로 구분이 안 된다.
   설치=짧고 탱탱, 폭발=크고 시원, 갇힘=먹먹(underwater muffle)이 구분 축이다.
2. **사망(7)은 웃겨야 한다.** 8인 파티 게임에서 죽음이 매판 8번 나온다 —
   슬프거나 무서우면 톤이 무너진다. 원작의 "뿅 터지는" 가벼움을 따른다.
3. **경고(9)만 유일하게 긴장 톤 허용.** 나머지 9개가 전부 밝기 때문에
   서든데스 경고는 대비로 즉시 읽힌다. 단 harsh/horror 로 가지 말 것.

## 생성 후 에디터 절차 (체크리스트 31-⑤ 요약)

1. WAV 임포트 → `Content/Audio/` (이름: `S_CA3D_BombPlace` 식으로 필드명과 맞춘다)
2. `SA_CA3D_Default`(Attenuation) 생성 → **MatchEnd 를 제외한 9종**에 지정
3. `SC_CA3D_Default`(Concurrency, Max 4~5 · Stop Oldest) → 최소 `Explosion`·`BlockBreak`·`BombPlace`·`Kick`
4. `DA_Rules_Default` → `Feedback` 카테고리 10슬롯에 지정
5. 확인: `-game` 실전에서 7종 발화는 이미 확인됨 — `Kick`·`SuddenDeathWarn` 은
   킥 아이템 획득 · 150초 경과 후 직접 확인 (체크리스트 31 "확인된 발화")

## 아직 훅이 없는 것 (만들어도 꽂을 자리가 없다)

- **BGM** · **UI 클릭음** · **캐릭터 선택 확정음** — 재생 경로가 코드에 없다.
  필요해지면 별도 Task 로 훅부터 만든다 (에셋 먼저 만들지 말 것).
