# VeroScript — Rider

## 설치

설정 → Plugins → 디스크에서 설치로 ZIP을 선택합니다. Rider 2026.2.2(262.10315.191)는 검증 SDK 기준과 선언된 최소 빌드입니다. 이후 버전은 호환성 확인이 필요합니다. 편집, Run, Check, 로컬 디버깅에는 호환되는 Runtime을 풀고 Languages & Frameworks → VeroScript에서 루트를 선택합니다. `veroscript`, `veroscript-lsp`, `veroscript-dap`, `stdlib/`, `localization/`이 바로 아래에 있어야 합니다. 프로젝트 개인 설정이 앱 기본값보다 우선하며 둘 다 비면 `PATH`를 검색합니다. 기본 색상은 Runtime 없이 동작합니다. 바이너리 교체 후 언어 서버를 재시작합니다.

Runtime을 업데이트하거나 이동할 때는 `Vm/`과 해당 네이티브 라이브러리를 포함한 전체 압축 해제 디렉터리를 유지합니다.
기본 VM은 도구가 제공하므로 Rider에 추가 설정이 필요하지 않습니다.

## 설정

의미 설정은 검증된 JSON 객체입니다. 지원 필드: `importPaths`, `implicitImportPaths`, `stdlibPath` 또는 `stdlibSearchPaths`, `bindingCatalogPaths`, `defines`, `predefinedMacros`, `allowCircularImports`, `allowProjectWideAccess`, `lspTraceAnalysis`, `lspRequestTimeoutMs`, `lspAnalysisTimeoutMs`, `maxCompletionItems`, `maxWorkspaceSymbolResults`, `maxReferenceResults`, `maxWorkspaceFiles`, `maxWorkspaceFileBytes`. 카탈로그는 기호 설명이며 실행 콜백은 호스트가 등록합니다. 공유 설정은 `.idea/veroscript.xml`, Runtime 경로는 개인 설정입니다. 도구 언어는 BCP 47, 빈 값은 IDE 언어이며 UI는 여섯 언어를 제공합니다.

## 편집

`//` 주석 끝의 백슬래시가 LF 또는 CRLF 바로 앞에 있으면 다음 물리적 줄도 주석입니다. 로컬 색상 표시와 빈 블록 Enter는 같은 규칙을 사용하며 GUI 마크업의 인라인 VS에도 적용됩니다. 백슬래시 뒤에 공백이나 탭이 있으면 이어지지 않습니다.

호버는 기호의 분류와 이름을 나누어 표시하며 기본 제공 기본형과 컨테이너 형식에는 분류 제목 하나를 표시합니다. 소스에 정의된 형식 별칭의 분류와 선언은 유지됩니다. 시그니처, 오버로드, 설명, 예제 및 정의 위치는 Runtime의 내용을 유지합니다.

VeroScript 설정의 “빈 블록에서 Enter를 누르면 여는 중괄호를 새 줄로 이동”는 기본적으로 활성화됩니다. 같은 줄의 빈 선언 블록 또는 제어 블록 안에서 Enter를 누르면 여는 중괄호를 별도 줄로 옮기고 내부 들여쓰기 위치에 커서를 놓습니다. 주석, 문자열, 선택 영역, 여러 커서 및 모호하거나 여러 줄에 걸친 헤더에는 일반 Enter 동작을 사용합니다. 이 설정만 변경하면 LSP를 다시 시작하지 않습니다. 전체 문서 및 범위 서식은 계속 서버 스타일을 따릅니다.

진단은 Runtime의 결과를 유지합니다. `void t(){}`는 유효한 완성된 함수이며 `void t`는 미완성이므로 `()`가 필요합니다. 입력 중 진단은 서버가 완성된 코드를 받아들이면 사라집니다.

## 경로

프로젝트 경로는 루트 기준입니다. 작업 디렉터리도 루트에서 해석하고 프로그램, 입력, JIT 등 실행 재정의 경로는 해석된 작업 디렉터리 기준입니다. 상속한 경로는 원래 기준을 유지합니다. `$PROJECT_DIR$`는 항상 프로젝트 루트입니다. 생략한 설정은 상속하고 명시한 목록은 빈 목록도 실행 시 대체합니다. 실행 재정의는 LSP 설정을 바꾸지 않습니다.

## 실행과 검사

저장된 `.vs`를 우클릭하거나 실행 구성을 만듭니다. `main` 없는 최상위 문장도 진입점입니다. `run`은 실행, `check`는 프로그램 인수 없는 컴파일 검사, `attach`는 Debug로 연결합니다. 인수는 IDE 따옴표 규칙(`""`는 빈 인수), 환경은 문자열 JSON 객체입니다. 일반 입력은 바이트, 디버그 입력은 준비된 UTF-8 텍스트입니다. 로컬 작업은 먼저 문서를 저장합니다. Current File이 없으면 Registry의 `run.current.file.item.in.run.configurations.combobox`를 켭니다.

컨텍스트 작업은 저장된 로컬 파일에서 Run/Check를 선택하고 템플릿의 연결 전용 필드를 지웁니다.
Check는 상속된 스크립트 인수, 입력 및 JIT 경로도 지우며 적용 가능한 작업 디렉터리, 환경과
컴파일러 설정은 유지합니다.

## 로컬 디버깅과 JIT

일반 Run은 `vm`, `mixed`, `jit`를 지원합니다. JIT 파일/디렉터리 필드로 호환 모듈을 선택적으로 고정합니다. 비워 두면 CLI 경로 덮어쓰기를 생략하고 모듈 환경의 선택을 상속하며 백엔드 미설정 시 기본값은 auto입니다. JIT가 비활성화되었거나 사용 가능한 자동 후보가 없으면 mixed는 VM을 사용하고 강제 jit는 실행 전에 실패합니다. Check/로컬 Debug는 JIT를 로드하지 않으며 attach는 경로를 비워야 합니다. Debug는 VM입니다. 진입 정지는 `inherit`/`true`/`false`, 깊이 0은 기본값입니다. 시작 준비는 1–3600초, 기본 30초입니다. 행/조건 중단점, 일시정지, 단계, 스택, 범위, 페이지 변수, 평가와 대입을 제공합니다. 재개하면 참조가 만료됩니다. 자동 호버는 이름 경로만 평가하며 다른 식은 명시적 평가를 사용합니다. program 실행 중 중단점 변경은 다음 정지에 반영합니다.

변수 자식 항목은 한 번에 최대 100개씩 표시합니다. 호스트가 개수 없이 전체 목록만 제공해도 펼칠 수 있습니다. 실행을 재개하면 대기 중인 이전 페이지가 만료됩니다.

디버거가 읽기 전용으로 표시한 변수에는 수정 기능을 표시하지 않습니다. 다른 변수의 편집 가능 여부는 계속 호스트 기능과 디버깅 컨텍스트에 따라 결정됩니다.

## 프로그램 연결

attach/program은 TCP DAP 호스트에 연결하며 임의 PID나 실행 중 VM을 인수하지 않습니다. `veroscript --debug --dap-port=4711 script.vs`를 시작하고 주소/포트를 지정합니다. 프로그램 빈 값은 호스트 진입점을 상속하고 작업 디렉터리/입력/환경은 비우며 backend/깊이는 호스트가 정합니다. 명시한 의미 설정만 보냅니다. 비어 있지 않은 인수는 대체하고 빈 인수 재정의 옵션은 지웁니다. 일부 빈 의미 목록은 호스트 기본값을 상속하므로 지울 수 없습니다.

프로그램 및 도메인 연결은 호스트 주소의 앞뒤 공백을 제거하고 내부 공백이나 제어 문자를 거부합니다.

## domain 연결

attach/domain은 선택적 예상 ID를 받으며 로컬 Runtime/파일이 필요 없습니다. 실행 재정의는 비우고 환경 `{}`, 진입 `inherit`, 깊이 0, backend mixed, 부모 환경 상속을 유지합니다. 호스트의 `veroscriptSupportsDomainAttach` 선언이 없으면 프로그램 로드 없이 실패합니다. 같은 연결로 연속 호출을 처리하며 호스트 기능에 따라 정지/단계/평가/조건/수정을 제공합니다. 식별자와 불투명 버전을 표시하지만 소스 비교/LSP 자동 동기화는 없습니다. 물리 소스 경로는 로컬에서 접근 가능해야 하며 경로 매핑은 없습니다. 어댑터 소스 참조는 읽기 전용 스냅샷으로 표시합니다. 연결 5초, 초기화 10초, domain 연결은 시작 예산을 사용합니다. 해제는 `terminateDebuggee=false`; 취소/프로세스는 호스트 소유입니다. 연결 손실은 세션을 종료하며 자동 재연결하지 않습니다.

## 문제 해결

언어 서비스 상태, 디버그 콘솔, IDE 로그를 확인하고 같은 Runtime의 도구를 설치합니다. 대상은 로컬 Rider 프로젝트와 Windows/Linux x64 Runtime입니다. 다른 IDE, Remote Development, Windows에서 WSL 실행, macOS/ARM64, 실제 앱/UE domain 통합은 별도 검증이 필요합니다。

```json
{"importPaths":["src"],"defines":["FEATURE=1"],"bindingCatalogPaths":["bindings/catalog.json"]}
```

[LSP](../../../../../../../../VeroScriptTooling/Resources/Docs/User/ko-KR/lsp.md)

[DAP](../../../../../../../../VeroScriptTooling/Resources/Docs/User/ko-KR/debugging.md)


## 관리 메모리

서로 일치하는 최신 Runtime 도구를 사용하면 LSP가 `std::memory` 완성, 호버 및 의미 색상을 제공합니다. 디버깅 일시 중지 중 변수 또는 감시에서 buffer나 view를 펼치면 `[byte_length]`, buffer의 `[object_id]` 및 사용 가능한 주소를 확인할 수 있습니다. 검사 항목은 읽기 전용이며 주소는 일시적인 관찰값입니다. 플러그인은 임의의 메모리 읽기나 쓰기를 제공하지 않습니다. 실행을 재개하면 변수 참조가 만료되므로 다음 중지 위치에서 다시 펼쳐야 합니다.

[std::memory](../../../../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/memory.md)

## 컴파일 확장과 생성 소스

현재 도구는 공개 매크로(`#define_export`, `export import`)와 소스 컴파일 확장(`import compiler "extension.vs";`)을 지원합니다. 호환되는 CLI/LSP/DAP 도구와 동일한 가져오기 경로를 사용하세요. DAP `sourceReference`가 있는 생성 메서드와 가상 모듈은 읽기 전용 디버그 스냅샷으로 열립니다. 스냅샷에서는 Run, Check, 중단점 및 LSP 동기화가 비활성화됩니다. 소스 읽기 실패는 표시되지만 일시 중지된 세션을 종료하지 않습니다. 네이티브 호스트의 처리 레지스트리와 모듈 환경은 호스트에서 설정하며, 플러그인은 이를 추가 의미 설정 JSON 필드로 노출하지 않습니다.

<!-- docs-navigation -->

---

[사용자 문서](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
