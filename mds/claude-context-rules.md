# Claude 컨텍스트 절약 규칙 (언리얼 프로젝트 전용)

> CLAUDE.md 200줄 제한으로 상세를 이곳에 둔다. 명시적 예외 요청이 없는 한 항상 적용.

## 엔진 소스 참조

- 엔진 소스 코드를 통째로 읽지 말고, 필요한 클래스/함수만 grep으로 찾아서 해당 부분만 확인할 것
- 예: `grep -n "virtual void BeginPlay" Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h`
- 전체 헤더 파일을 열람하기 전에 함수 시그니처만 필요한지 먼저 확인

## 빌드 에러 처리

- 빌드 로그 전체를 붙여넣지 말고, 에러가 발생한 파일명 + 라인 번호 + 에러 코드(C2xxx, LNK2xxx 등)만 추려서 전달
- UHT 관련 에러는 보통 마지막 5~10줄에 핵심 원인이 있으므로 그 부분만 확인

## 코드 작성 규칙

- 리플렉션 매크로(UCLASS, UPROPERTY, UFUNCTION)의 meta 옵션은 실제로 필요한 것만 작성, 불필요한 옵션 나열 금지
- .h/.cpp 동시 수정 시 변경되는 부분만 diff로 보여주고 전체 파일 재출력 지양

## 블루프린트 관련

- 블루프린트 구조를 논의할 때 JSON 직렬화 전체를 붙여넣지 말고 핵심 노드 흐름만 텍스트로 요약해서 전달
- 스크린샷보다는 "OnComponentHit → CastToPlayer → ApplyDamage" 같은 화살표 요약 선호

## 컨텍스트 제외 목록

아래 폴더/파일은 코드 분석·검색·컨텍스트 로딩에서 자동으로 건너뛸 것 — 빌드 산출물이자 소스가 아니므로 읽을 필요 없음.
명시적으로 요청받은 경우에만 예외. (검색·분석 제외일 뿐, git 커밋 대상 여부와는 무관 — `Content/*.uasset` 은 계속 커밋한다.)

| 분류 | 대상 |
|---|---|
| UE 자동 생성 폴더 | `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, `Build/` |
| 캐시/임시 파일 | `*.pdb`, `*.obj`, `*.tlog`, `*.ilk`, `*.exp`, `*.lib` |
| 대용량 에셋 (텍스트 분석 불필요) | `Content/**/*.uasset`, `Content/**/*.umap` |
| IDE/에디터 생성 파일 | `.vs/`, `.vscode/`, `*.sln`, `*.vcxproj` |
