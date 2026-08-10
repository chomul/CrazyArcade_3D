# UCA3DGameInstance

> `Framework/CA3DGameInstance.h/.cpp` · UGameInstance — ⏸️ 보류 (`bEnableEOS=false`)

## 역할
- EOS 세션 수명: Device ID 로그인 → 방 생성/검색/참가 → 로비 복귀. 실패는 델리게이트 통지
- 세션·접속 배관 전용. 게임플레이 로직 금지

## 왜
- **왜 GameInstance?** → 세션은 맵 전환을 넘어 살아야 함. GameMode/State는 맵마다 재생성
- **왜 보류?** → "게임 빨리 완성" 우선 + 배포 미정. 접속은 IP 직접(데디 검증 완료)
- **⛔ 엔진 블로커** → UE 5.8 OSS v1 EOS는 데스크톱 Device ID 로그인 구조적 불가
  (`ADD_USER_LOGIN_INFO` 기본 꺼짐). SDK 직접 호출 성공으로 자격 증명 문제가 아님을 분리 증명
- **왜 자격 증명이 별도 ini?** → gitignore 대상 — 저장소에 비밀 금지 + 유출 회귀 테스트
- **꺼짐 검증** → off 상태로 데디 봇 매치 동일 동작(에러 0) 확인 — 죽은 코드가 아니라 대기 코드

## 연결
재개 절차: `mds/Tasks/19-*.md`

## Q&A
아직 없음
