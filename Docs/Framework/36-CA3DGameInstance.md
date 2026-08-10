# UCA3DGameInstance

> `Source/CrazyArcade3D/Framework/CA3DGameInstance.h/.cpp` · UGameInstance
> ⏸️ **보류 중** — 구현 완료·`bEnableEOS=false`로 꺼져 있음 (2026-08-02 사용자 결정)

EOS 온라인 세션 수명: Device ID 로그인 → 방 생성/검색/참가 → 로비 복귀. 실패 통지 델리게이트 5종.

## 역할

- **온라인 세션 수명 관리** (현재 꺼짐): Device ID 로그인 → 방 생성/검색/참가 →
  로비 복귀, 실패를 델리게이트로 통지.
- 맵 전환을 넘어 살아남는 유일한 게임 계층 객체 — 세션·접속 배관 전용, 게임플레이 로직 금지.

## 왜 이렇게 했는가

- **왜 GameInstance인가** — 세션은 맵 전환을 넘어 살아야 하는 상태다. GameMode/GameState는
  맵마다 새로 만들어지므로 부적합. 엔진 표준 자리.
- **게임플레이 로직 금지** — 이 클래스는 접속 배관만. 게임 규칙이 스며들면 세션 계층과
  게임 계층이 얽혀 어느 쪽도 교체 못 하게 된다.
- **왜 보류됐나** — 목표가 "게임 자체를 빨리 완성"이고 배포가 미정이라 로비/세션을 배포
  시점으로 이월. 접속은 IP 직접 — 데디 서버에서 이미 검증된 경로다.
  ⛔ 엔진 블로커도 확인됨: UE 5.8 OSS v1 EOS는 데스크톱에서 Device ID 로그인이 구조적으로
  불가(`ADD_USER_LOGIN_INFO` 기본 꺼짐 → `EOS_InvalidParameters`). 자격 증명 문제가 아님을
  SDK 직접 호출 성공으로 분리 증명했다. 재개 절차는 `mds/Tasks/19-*.md`.
- **자격 증명은 gitignore 대상 별도 ini** — `Config/EOSCredentials.ini`를 모듈 시작 시
  GEngineIni에 병합. 저장소에 비밀이 남지 않게 하는 표준 처리 + 유출 회귀 테스트까지 있다.
- **꺼진 상태가 안전함을 검증** — `bEnableEOS=false`로 데디 봇 매치가 이전과 동일 동작
  (에러 0)을 확인하고 껐다. "죽은 코드"가 아니라 "대기 중 코드".

## 연결
- 재개 시 연결: 로비 UI(미제작) · 배포 문서 `mds/build.md`

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
