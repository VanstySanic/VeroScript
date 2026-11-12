# Tooling 모듈

`VeroScriptTooling`은 VeroScript language core를 둘러싼 공개 analysis layer입니다.
editor, language server, static checker와 debugger integration에 사용합니다.

## 공개 surface

| Surface | 목적 |
| --- | --- |
| Analyzer | source/file을 분석하고 structured diagnostic을 반환합니다. |
| Formatter | 전체 source replacement를 생성하며 macro를 확장하지 않습니다. |
| Completion / call help | context completion, grouped overload, signature, 상속/interface 메서드 구현 생성, host metadata와 preprocessor entry를 제공합니다. |
| Hover / navigation | 해석된 declaration, documentation comment, definition과 namespace-aware range를 제공합니다. |
| Outline / semantic tokens | declaration 기반 document symbol과 semantic highlighting을 제공합니다. |
| References / rename | 파일 간 reference, read/write, prepare와 workspace text edit를 제공합니다. |
| Workspace | 열린 buffer overlay, 다중 파일 분석, import 해결과 workspace symbol을 제공합니다. |
| LSP / DAP | `VeroScriptLspStdio::Run` 및 debugger adapter입니다. LSP는 resolve, type navigation, range, link, token delta, formatting과 format code action을 제공합니다. |

생성된 메서드 completion은 `FCompletionItem::bOverrideMember`를 설정하며,
`bImplementsInterfaceMember`는 생성 메서드가 순수 인터페이스 요구 사항을 충족할 때 true입니다.
virtual default 메서드의 override는 상속 멤버 재정의로 분류됩니다.
`Documentation`은 active Tooling locale을 따릅니다. `InsertText`에는 메서드 skeleton이 들어 있고,
`bInsertTextSnippet`은 snippet placeholder 사용 여부를 나타냅니다. 같은 이름의 overload는 하나의 항목으로
묶고 detail에 signature를 한 줄씩 표시합니다. LSP는 생성 후보를 명시적 text edit로 보내며,
snippet을 지원하지 않는 client에는 일반 메서드 텍스트를 보냅니다.

C++ 쿼리 API의 위치는 1부터 시작합니다. 열린 문서가 디스크 파일보다 우선합니다. `FWorkspaceOptions`는 import 경로, 표준 라이브러리 검색 루트, 정의, 사전 정의 매크로, 암시적 import, 프로젝트 전체 접근과 호스트 바인딩 메타데이터를 제공합니다.

네이티브 호스트는 쿼리·워크스페이스 옵션, `FLspStdioRunOptions`, `FDapStdioRunOptions`/`FDapLaunchArgs`에 `AnnotationProcessing`을 제공할 수 있습니다. 컴파일과 같은 고정 프로세서 레지스트리를 사용하세요. LSP 팩터리와 의존성 판독기는 프로토콜 및 분석 작업자의 동시 호출에 안전해야 합니다. 재사용 전에 의존성 내용과 존재 여부를 확인하며 `Cacheable=false` 프로세서 결과는 정상 반환하되 캐시하지 않습니다.

`FWorkspaceSymbolsOptions.WorkspaceRoots`는 closed-file symbol indexing에 사용할 root를
제공하며 `ImportPaths`와 별개입니다. 따라서 project를 index한다고 해서 모든 index
directory가 implicit import path가 되지는 않습니다. `MaxWorkspaceFiles`와
`MaxWorkspaceFileBytes`는 큰 workspace 또는 신뢰할 수 없는 workspace를 순회할 때
각각 파일 수와 파일 크기를 제한합니다. `ShouldCancel`은 긴 scan 중 협력적으로
확인되며, `FWorkspaceSymbolsResult::bCancelled`가 설정된 결과는 새 index로
publish하거나 cache해서는 안 됩니다.
workspace symbol 수집은 declaration identity의 중복을 제거하므로 여러 import에서
같은 declaration이 발견되어도 한 번만 반환합니다.

`VeroScriptWorkspace::ImplementationDocument`는 derived type과 interface 또는
virtual-member implementation을 찾습니다. `FormatRangeDocument`는 선택 범위를
완전한 physical line으로 확장한 뒤 replacement range를 반환하므로 token이나
line fragment만 다시 쓰지 않습니다.
`FWorkspaceSymbolsOptions`를 받는 `ImplementationDocument` overload는 같은 workspace
limit와 cancellation callback을 적용합니다. 취소되면
`FNavigationResult::bCancelled`가 설정되며 partial definition을 publish해서는 안 됩니다.
`FormatDocument`와 `FormatRangeDocument`에는 library caller를 위한 선택적
indentation width와 공백/tab 스타일을 지정하는 overload도 있습니다. width가 0이면 단계마다 공백
4개를 사용하는 workspace 기본값을 유지하며, `bInsertSpaces=false`이면 단계마다 tab 하나를 출력합니다.
workspace를 사용하지 않는 formatter도 같은 스타일 옵션을 받습니다.
상태가 없는 `VeroScriptFormat::FormatRangeSource` 진입점은 open workspace 없이도 동일하게
전체 physical line을 대상으로 하는 range 계약을 제공합니다.

`VeroScriptWorkspace::ImplementationDocument`는 별도로 정의되거나 주석으로 생성된 함수·메서드 본문도 찾습니다. 선언 탐색은 계속 선언을 대상으로 합니다. `SourceImplementation`은 본문 파일명과 물리 위치를 소유하며 빈 파일명은 별도 본문이 연결되지 않았음을 뜻합니다. 자유 함수나 비가상 메서드의 직접 구현 조회는 워크스페이스 전체를 스캔하지 않습니다.

포매터는 블록 본문에 Allman 중괄호를 사용합니다. `else`, `else if`, `catch`, `finally`,
`do…while` 끝의 `while`은 새 줄에서 앞 블록의 닫는 중괄호에 맞춥니다.
초기화 식의 구문, 리터럴 내용과 전처리 지시문은 유지하며 매크로를 확장하지 않습니다.

범위 경계가 블록 주석, 원시 문자열 또는 여러 줄의 전처리 지시문 안에 있으면 서식 편집을 반환하지 않습니다.
전체 지시문이나 뒤의 일반 코드를 선택하면 서식을 지정할 수 있으며, 매크로 원문과 줄 연속은 유지합니다.

semantic kind은 identifier casing이 아니라 분석 결과와 host metadata에서 결정됩니다.
user namespace와 예약된 `std` tree는 completion, hover, navigation, reference,
rename, outline, semantic token에서 일관된 namespace symbol로 처리됩니다.
completion item은 argument 또는 generic type용 LSP snippet placeholder를 포함할 수 있습니다.
LSP adapter는 client가 `textDocument.completion.completionItem.snippetSupport`를 선언한 경우에만
snippet 형식의 삽입 문자열을 보내며, 그 외에는 일반 텍스트를 보냅니다.

namespace segment item은 member item과 다릅니다. expression/statement 경로에서는 replace range 뒤에
separator가 없을 때 `InsertText`에 `::`를 추가합니다. 이미 `::`가 있거나 `using namespace` 대상을 완성할 때는
마지막 이름만 삽입합니다.

한정된 namespace의 member에서는 `FCompletionItem::Label`과 `InsertText`가 마지막 이름과
기존 snippet을 유지하고, `LabelDescription`이 한정 namespace를 별도로 담습니다. LSP에서는
표준 `labelDetails.description`입니다. snippet 형식 삽입 문자열은 client가
`textDocument.completion.completionItem.snippetSupport`를 선언한 경우에만 보내고 그 외에는 일반 텍스트를
사용합니다. server는 Valence의 최종 label/declaration 및 UTF-16
semantic span용 `$valencePresentation`도 보낼 수 있으며, 다른 LSP client는 이 extension을
무시할 수 있습니다. 이 presentation field는 filtering, sorting, 삽입 text를 바꾸지 않습니다.

completion은 context priority보다 textual match quality를 먼저 정렬합니다. 순서는 exact,
case-insensitive exact, prefix, word-boundary/CamelCase, contains, subsequence입니다.
`FCompletionItem::MatchQuality`가 primary key이고 `SortPriority`는 동점만 가르므로 integration은
반환된 순서를 보존해야 합니다. LSP `sortText`도 같은 전체 순서를 담습니다. LSP는
exact/case-insensitive-exact/prefix tier가 다른 모든 item보다 엄격히 좋을 때에만 첫 item에
`preselect`를 붙입니다. primitive와 container type word는 builtin type입니다. primitive spelling은
completion, hover, semantic token에서 `keyword`로 유지되고 container spelling은 `type`/type
alias입니다. 둘 다 definition의 source location이 없으며 rename은 거부합니다. 설명 hover text는
active Tooling locale을 따르지만 type identifier와 protocol field는 바뀌지 않습니다.

LSP adapter는 `completionItem/resolve`, type definition, implementation, selection range,
document link, folding range, full/minimal-delta semantic token, 물리 줄 단위 range와
on-type formatting, `source.formatDocument`, 안전한 duplicate-import
`source.organizeImports`, 메서드 구현 생성 `refactor.rewrite`도 제공합니다. cancel과 watched-file notification으로
workspace를 갱신하고 그 사이 closed-file workspace symbol index를 cache합니다.
`VeroScriptLspStdio::Run`은 `FLspStdioRunOptions`를 받아 client가 `workspaceFolders`나
`rootUri`를 제공하지 않을 때 closed-file indexing에 사용할 `DefaultWorkspaceRoots`를
설정할 수 있습니다. client가 제공한 root는 이 default를 대체합니다.

wire protocol, request, cancellation과 설정은 [Language Server](lsp.md)를, debugger는
[디버깅](debugging.md)을 참조하십시오.

## 디버그 어댑터 프로토콜

CLI에서 stdio 또는 TCP 디버그 어댑터를 시작할 수 있습니다.

```powershell
veroscript --debug hello.vs
veroscript --debug --dap-port=4711 hello.vs
```

`--dap-port`에는 `--debug`와 1~65535 범위의 포트가 필요합니다. TCP 기본 수신 주소는 `127.0.0.1`이며, `--dap-bind=<주소>`는 TCP 모드에서만 지정할 수 있습니다. 프로그램과 인수는 DAP `launch` 또는 `attach`로 전달합니다. 디버깅은 VM을 사용하므로 mixed/JIT 실행 성능을 나타내지 않습니다.

source line breakpoint는 `setBreakpoints`로 설정합니다. exception breakpoint와 function
breakpoint는 지원하지 않습니다. 비어 있는 `setExceptionBreakpoints` request만 standard client
launch handshake의 clear operation으로 성공하며, non-empty filter와 모든
`setFunctionBreakpoints` request는 실패합니다. client는 이 breakpoint category를 enabled로
표시해서는 안 됩니다.

`launch` 또는 `attach` 전에 설정한 source breakpoint는 pending/unverified 상태입니다. code가
load된 뒤 adapter는 같은 DAP ID와 `reason: "changed"`를 가진 standard `breakpoint` event를
보냅니다. exactly one compiled source에 속하고 preprocessing 후에도 active이며 executable
location이 있는 line만 verified가 되며 matching basename으로 fallback하지 않습니다. client는
반환된 physical line을 사용해야 하고 function 안에서는 next executable line으로 이동할 수 있습니다.
전체 binding rule은 [디버깅](debugging.md)을 참고하십시오.

프로세스 내 통합에서는 `VeroScriptDapAdapter`를
`Initialize` → `Launch` 또는 `Attach` → `SetBreakpoints` → `ConfigurationDone` 순서로
사용합니다. 프로세스 내 `ConfigurationDone`은 동기 호출이므로 wire host는 execution
worker에서 호출해야 protocol thread가 `pause`, `terminate` 및 정지 상태 request를 계속
처리할 수 있습니다. `Attach`는 `Launch`와 같은 loader로 program을 로드하며 이미 실행 중인
VM에 연결하는 기능이 아닙니다. `Pause`와 `Terminate`는 thread-safe cooperative control이고,
`IsStopped()`와 `IsRunning()`은 현재 실행 상태를 제공합니다.

`VeroScriptDapStdio::Run`은 `FDapStdioRunOptions`를 받아 `DefaultProgram`,
`DefaultScriptArgs`, `DefaultStdlibSearchPaths`, `DefaultEnvironment`,
`DefaultWorkingDirectory`, `DefaultInput` 등을 기본값으로 사용합니다. embed host가
`LiveBindings`를 지정하면 등록된 host function을 실행할 수 있으며, 소유하지 않는 catalog는
DAP 실행 전체 동안 유효해야 합니다. live binding이 없으면 `bindingCatalogPaths`가 분석과
launch에 사용할 metadata/stub binding을 제공합니다.

`VeroScriptDapTcp::Run`은 `FDapTcpRunOptions`를 받습니다. `MaxSessions=0`은 host가 중지될
때까지 client를 계속 받고, `Port=0`은 ephemeral port를 선택해 `OnListening`으로 알리며,
`BindAddress`는 일반적으로 `127.0.0.1`로 유지해야 합니다. `bAnnounceListen=false`는 error
stream의 기본 readiness 알림을 끕니다. stdio host는 clean session에서 `0`, protocol 또는
I/O 오류에서 `1`, client가 host terminate를 요청하면 `2`를 반환합니다.

### 프로토콜 비종속 디버그 세션

`VeroScriptDebugSession`은 DAP 어댑터 아래에서 재사용할 수 있는 C++ 계층입니다. 호스트나
사용자 지정 어댑터가 DAP wire protocol을 구현하지 않고 로드, 중단점, 동기식 중단 콜백,
상태 검사 및 실행 결과를 관리해야 할 때 사용하십시오. `LoadSource`와 `LoadFile`은 workspace
계층과 동일한 import path, define, 사전 정의 매크로, source overlay, 선택적 binding catalog,
implicit import path, import 정책 및 표준 라이브러리 root를 받습니다.

`BindBreakpoint`는 adapter용 strict source-breakpoint path입니다. one-based physical line을
exactly one compiled source에 resolve하고, bind되면 nonzero `Id`와 actual physical `Line`을,
bind할 수 없으면 localized `Message`를 포함한 `FBreakpointBinding`을 반환합니다. function 안의
line은 next executable line으로 이동할 수 있으므로 caller는 requested line을 가정하지 말고
반환된 line을 사용해야 합니다.

`Run` 전에 스크립트 인수, 환경, working directory, 입력, 최대 call depth, 요청 backend 또는
JIT 정책을 구성할 수 있습니다. `FDebugRunResult`에는 `bSuccess`, 구조화된 `Diagnostics`,
분리된 `Output` 및 `ErrorOutput`, `ExitCode`가 포함됩니다. 디버거는 유효 backend를 여전히
`Vm`으로 강제하며, 요청한 backend와 JIT 정책은 설정으로 유지되고 JIT 통계에는 주로 그
무효화가 기록됩니다.

`SetStopHandler`는 실행이 일시 중지된 동안 동기적으로 호출됩니다. 핸들러는
`GetStackTrace`, `GetLocals`, `GetChildren`, `Evaluate`, `SetLocal`, `SetChild`를 사용해
상태를 검사하거나 수정한 뒤 `SetRunMode`로 `Continue`, `StepIn`, `StepOver`, `StepOut` 중
하나를 선택할 수 있습니다. 비동기 호스트용 `RequestPause()`와 `RequestCancel()`은 스레드
안전한 협력적 제어입니다. 일반 세션 설정과 `Run` 호출은 제어 스레드에서 수행하고 요청한
작업이 반환될 때까지 세션을 유지하십시오.

## 공개 경계와 현지화

SDK와 함께 배포되는 public header만 사용하십시오. 내부 구현 파일은 지원되는
integration surface가 아닙니다. Tooling은 CLI와 runtime의 localization catalog를
공유합니다. diagnostic code와 argument는 언어 간에 안정적이며 표시만 달라집니다.
[진단 현지화](../../../../../VeroScript/Resources/Docs/User/ko-KR/localization.md)도 참조하십시오.

## 현재 구문 인식 편집기 적용 범위

completion, outline, hover, signature help 및 semantic token은 같은 type의 declaration group,
`do`/`while`, expression list가 있는 classic `for` clause 및 signed 8-bit `char` literal을 인식합니다.
initializer는 자신의 declaration이 도입되기 전 scope에서 해석됩니다. shadowing name이나 nested lambda
local은 이후 declaration initializer 또는 `for` step expression으로 새어 나가지 않습니다. 여러 physical
line에 걸친 raw string에는 각 physical line마다 하나의 string semantic token이 부여되며, literal 안에서
comment marker처럼 보이는 text도 comment로 분류되지 않습니다.

## 워크스페이스 분석 관찰자

VeroScriptWorkspace::AnalyzeAll(OnDocument)는 문서마다 한 번씩 빌린 경로와 분석 결과로 관찰자를 동기 호출합니다. 콜백 이후 필요한 데이터는 복사하세요. 순회하는 동안 관찰자는 같은 워크스페이스 문서를 열거나 닫거나 변경하면 안 됩니다.

## 임베디드 분석 및 디버깅의 VM 선택

Tooling을 직접 사용하는 호스트는 인터프리터를 명시적으로 선택합니다. 실행 전에 `VeroScriptDebugSession::SetVmBackendFactory`/`SetVmBackendPath`를 호출하거나 `FDebugLoadOptions` 또는 `FDapStdioRunOptions`의 `VmBackendFactory`/`VmBackendPath`를 지정하세요. 팩터리와 경로는 배타적입니다. 실행 확정 시 선택이 고정되며 지우기/다시 로드도 도메인을 유지하여 교체를 허용하지 않습니다. 설정 실패는 기존 선택을 유지합니다. 호환 Runtime 실행기는 제품 백엔드를 제공하지만 Tooling 링크만으로는 제공되지 않습니다.

컴파일 확장 스크립트를 실행하는 쿼리는 분석/워크스페이스 옵션의 `ScriptCompileExtensions`에서 `VmBackendFactory` 또는 `VmBackendPath`를 사용합니다. 디버그 로드는 확장 전용 선택이 없으면 선택한 VM을 상속합니다. 확장을 실행하지 않는 파싱과 일반 분석은 VM이 필요 없습니다. 작업 중 백엔드와 소유 리소스를 유지하세요.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
