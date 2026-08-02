# Checklist 19 — CA3DGameInstance

> 대응 Task: `mds/Tasks/19-CA3DGameInstance.md`
> **실제로 접속 테스트하지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**
> 로컬 검증과 VPS 실환경 검증을 구분한다 — 로컬만으로 완료 선언 금지.

## ⏸️ 이 Task 는 보류 상태다 (2026-08-02)

구현은 끝났고 **꺼져 있다**(`bEnableEOS=false`). 배포 여부가 미정이라 로비를 배포 시점으로 이월했다.
아래 "동작 검증" 절이 전부 비어 있는 이유는 못 해서가 아니라 **엔진 블로커에 막혀 있기 때문**이다 —
상세와 재개 절차는 Task 문서의 "보류 상태" 절.

## 빌드 (필수 게이트)
- [x] `CrazyArcade3DEditor` 빌드 통과 (2026-08-02 · 유니티 병합 강제)
- [x] `CrazyArcade3DServer` 빌드 통과 (2026-08-02 · 동일)
- [x] `Build.cs` OSS 의존 추가 후 프로젝트 파일 재생성

## 선행 (사용자 액션)
- [x] EOS 포털 제품/샌드박스/디플로이먼트/클라이언트 발급 완료 (2026-08-02)
      — 값은 `Config/EOSCredentials.ini`(**gitignore 대상**), 템플릿은 `.example.ini`
      - [x] 자격 증명이 커밋 대상 어디에도 없음을 확인 (`git ls-files` + 전체 문자열 검색)

## 코드 검증 (정적)
- [x] Device ID 익명 인증 사용 (계정 로그인 없음 — `bUseEAS=false`/`bUseEOSConnect=true`)
- [x] `bAllowJoinInProgress=false` (난입 없음 — GDD)
- [x] 재접속/중간 복원 로직 없음 (GDD 6.2)
- [x] 게임플레이 로직 없음 — 세션·이동만
- [x] 실패를 반드시 통지한다 — 조용히 실패하면 로비가 영원히 "검색 중"으로 멈춘다
      (런타임 확인: 로그인 전 `ca3d.FindSessions` → `방 검색 실패 — EOS 로그인 전` 델리게이트 통지)
- [x] 넷드라이버 무변경 — 전송은 `IpNetDriver` 그대로 (VPS 데디가 배포 목표)

## 동작 검증 — 부분 (2026-08-02)
- [x] 자격 증명 로더가 `EOSCredentials.ini` → `GEngineIni` 병합 (`Artifacts=2개` 로그)
- [x] EOS 플랫폼 생성 + 백엔드 왕복 — `EOS_Connect_CreateDeviceId` 가 `EOS_Success`
- [x] Device ID 로그인 자체는 가능 — **SDK 직접 호출로 ProductUserId 획득**
      (`ca3d.EOSDeviceIdProbe`. 자격 증명이 정상임을 증명하는 대조군)
- [ ] ❌ **OSS 경유 로그인** — `EOS_InvalidParameters`. 엔진 블로커 (Task 문서 참조)
- [x] **끈 상태에서 게임이 이전과 동일하게 동작** — 데디 서버 봇 매치 완주,
      파괴 27건·사망·순위 정상·에러 0건, EOS 플랫폼 생성/로그인 시도 0건

## 동작 검증 — 로컬 (블로커로 미실행)
- [ ] 방 생성 → 공개 목록 검색 → 참가 → 같은 매치 진입 (2클라)
- [ ] 전원 준비 → 방장 시작 흐름
- [ ] 매치 종료 → 로비 복귀 → 재생성 가능 (세션 정리 확인)

## 동작 검증 — VPS 실환경 (주 1회 게이트, 미실행 시 미검증)
- [ ] 리눅스 데디 서버 세션에 외부 클라 참가
- [ ] 도쿄 리전 핑 확인 (30~50ms 기대)
