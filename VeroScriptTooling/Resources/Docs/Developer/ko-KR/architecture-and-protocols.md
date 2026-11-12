# Tooling 아키텍처와 protocol

`VeroScriptTooling`는 command-line, LSP, DAP, other host가 공유하는 language-service layer입니다. public language-core contract를 consume하며 semantic query와 protocol transport를 분리합니다.

LSP와 DAP는 `Private/Internal/VeroScriptToolingJson.h` 및 `.cpp`의 비공개 JSON 값, 파서, 직렬화기를 공유합니다. 문자열 이스케이프는 `Shared/Private/VeroScriptJsonString.h`를 사용합니다. 프레이밍, 요청 연결, 스케줄링, 취소 및 프로토콜별 필드는 각 어댑터가 담당합니다. 공유 구현 변경 시 결정적인 객체 순서, 숫자 변환, 기존 깊이 제한 및 오류 문구를 유지해야 합니다.

## 계층별 데이터 흐름

core는 parsing, semantic analysis, module loading, runtime metadata, diagnostic, debugger semantics를 소유합니다. `VeroScriptWorkspace`는 project context, document overlay, source/import resolution, preprocessor context, binding catalog, cross-file state를 제공합니다. public feature service는 explicit source/workspace를 query해 stable result를 반환하고 LSP/DAP adapter가 serialize/transport를 소유합니다. feature는 editor state를 read/source file을 write하지 않고 adapter는 public semantic result를 protocol text에서 reconstruct하지 않습니다.

## 공개 기능 분류

`VeroScriptAnalyzer`는 imported file을 포함한 workspace-aware diagnostic을 URI별 publish하고 stale result는 empty list로 clear합니다. `VeroScriptOutline`, `VeroScriptNavigation`, `VeroScriptHover`, `VeroScriptCallHelp`, `VeroScriptComplete`, `VeroScriptFindReferences`, `VeroScriptDocumentHighlight`, `VeroScriptRename`, `VeroScriptFormat` (`FFormatOptions`), `VeroScriptWorkspaceSymbols`는 structure, lookup, completion, call, reference, edit, format, workspace search를 제공합니다. `std::`를 포함한 namespace는 semantic이고 host-only location은 virtual binding URI를 사용합니다. LSP adapter는 client가 `textDocument.completion.completionItem.snippetSupport`를 true로 설정한 경우에만 snippet 삽입 문자열을 반환하며, 그 외에는 일반 문자열을 반환합니다. call context에서 completion target 뒤에 `(`가 이미 있으면(공백이나 주석이 사이에 있어도) 호출 구문 중복을 피하기 위해 callable snippet을 생략합니다. rename은 re-resolve하고 standard library는 rename 불가, format은 edit만 반환합니다.

`FCompletionItem::MatchQuality`는 primary textual ordering key이고 `SortPriority`는 같은
quality의 tie-breaker입니다. adapter는 이 order를 LSP `sortText`에 보존하고 다른 항목보다
명확히 나은 exact, case-insensitive-exact, prefix match에만 `preselect`를 설정합니다.
primitive word는 built-in `Keyword` entry이고 container word는 `TypeAlias`/`type` entry입니다.
둘 다 display detail은 localized되지만 type identifier는 stable하고 source definition이 없으며 rename은 거부됩니다.

같은 표시 이름의 callable overload는 하나의 completion item으로 묶고 각 source-facing
signature를 detail에 한 줄씩 표시합니다. call context에서는 특정 overload의 parameter를
추측하지 않고 빈 argument stop을 사용합니다. overload가 없는 callable 또는 generic entry는
client가 `textDocument.completion.completionItem.snippetSupport`를 선언한 경우 snippet을
포함할 수 있습니다. 그 외에는 일반 텍스트를 삽입합니다.

한정된 namespace의 completion에서는 `label`과 삽입 텍스트(`insertText`, 교체 범위가
있으면 `textEdit.newText`)에 마지막 이름을 유지합니다. 사용 가능한 callable 또는 generic type snippet이 있고 client가
`textDocument.completion.completionItem.snippetSupport`를 선언한 경우에만 snippet을 포함합니다.
그 외에는 마지막 이름만 삽입합니다. `labelDetails.description`에 namespace를 제공합니다.
선택적인 `$valencePresentation`은 Valence용 표시 텍스트와 UTF-16 semantic span을 담습니다.
다른 LSP client는 이를 무시할 수 있으며 표시 정보는 정렬이나 삽입 내용을 바꾸지 않습니다.

namespace segment completion은 namespace member completion과 다릅니다. expression/statement 경로에서
namespace item은 마지막 이름을 `label`과 `filterText`에 유지하고, replace range 뒤에 separator가 없을 때만
삽입 문자열에 `::`를 추가합니다. `using namespace` 대상에는 이름만 삽입합니다. snippet을 지원하는 client의
call context에서 `print` 같은 variadic host formatting function은 합쳐진 overload detail에서 placeholder를
추측하지 않고 `print(${0})`처럼 비어 있는 argument tab stop을 사용합니다.

semantic token에서 LSP adapter는 resolved span과 language/type word, directive, macro,
literal/comment용 lexer baseline을 combine합니다. 이 baseline은 incomplete input에서도
사용할 수 있고 LSP stream을 encode하기 전에 overlap을 제거합니다.

## LSP 프로토콜 계약

`VeroScriptLspStdio`는 `Content-Length` framing, JSON-RPC correlation, dispatch, cancellation, diagnostic publication, lifecycle을 소유합니다. request는 `textDocument/completion`, `completionItem/resolve`, `textDocument/hover`, `textDocument/definition`, `textDocument/declaration`, `textDocument/typeDefinition`, `textDocument/implementation`, `textDocument/prepareTypeHierarchy`, `typeHierarchy/supertypes`, `typeHierarchy/subtypes`, `textDocument/documentSymbol`, `textDocument/selectionRange`, `textDocument/documentLink`, `textDocument/foldingRange`, `textDocument/semanticTokens/full`, `textDocument/semanticTokens/full/delta`, `textDocument/signatureHelp`, `textDocument/references`, `textDocument/documentHighlight`, `textDocument/prepareRename`, `textDocument/rename`, `textDocument/formatting`, `textDocument/rangeFormatting`, `textDocument/onTypeFormatting`, `textDocument/codeAction`(`source.formatDocument`, `source.organizeImports`, `refactor.rewrite`), `workspace/symbol`, `workspaceSymbol/resolve`입니다.

notification은 `initialize`, `initialized`, `textDocument/didOpen`, `textDocument/didChange`, `textDocument/didSave`, `textDocument/didClose`, `shutdown`, `exit`, `workspace/didChangeConfiguration`, `$/cancelRequest`이며 cancel은 `-32800`을 반환합니다. completion trigger는 `.`/`:`/`@`, signature trigger는 `(`/`,`입니다. LSP는 zero-based UTF-16, C++는 one-based byte position이고 adapter가 한번 convert하며 old document version은 ignore합니다.

## configuration과 localization

`initializationOptions`, `workspace/didChangeConfiguration.settings`의 `veroscript` setting에는 import/implicit-import path, `stdlibPath`, define/macro, policy, `bindingCatalogPaths`가 있습니다. catalog는 ordered merge, duplicate reject, next non-lifecycle request에 reload/reanalyze하고 runtime callback을 register하지 않습니다. `initialize.locale`은 BCP 47 preference이며 unsupported/missing은 environment catalog/`en-US` fallback입니다. diagnostic/binding/type/protocol/URI/path/source identifier는 translate하지 않습니다.

## invariants

source position/range, document identity/version, cancellation/correlation/wire order, stable diagnostic ID/argument, empty result/protocol failure 구분, tooling이 edit를 apply하지 않는 rule을 보존합니다. [VeroScript 언어 서버 참조](../../User/ko-KR/lsp.md)를 참고합니다.

## Debug와 DAP boundary

`VeroScriptDebugSession`은 compilation, breakpoint, step, VM inspection, evaluation, variable write, output, JIT/backend를 소유하며 `VeroScriptDapAdapter`는 DAP mapping만 하고 second engine/transport를 만들지 않습니다. `FDapLaunchArgs`는 source/path/overlay/binding/policy/stop/call depth/backend/args/roots/environment/cwd/stdin을 가지며 debug attach는 `Mixed`/`Jit` 요청에도 `Vm`을 사용합니다. `Initialize` 뒤 `ConfigurationDone`은 synchronous run, stopped callback만 debug call이 valid, `Disconnect`는 resume/clear, `initialized`는 response 뒤 transport가 emit하고 `VeroScriptDapAdapter::Initialize()`는 emit하지 않습니다。

`VeroScriptDebugSession::BindBreakpoint`는 physical-line/preprocessor-map resolution을 소유하며 exact compiled source label만 등록합니다. invalid request 또는 eligible location이 없는 non-executable request는 localized display text를 포함한 unregistered result가 됩니다. adapter는 external DAP ID를 보존하고 actual bound line을 돌려주며, pre-launch breakpoint를 rebind한 뒤 `Breakpoint`/wire `breakpoint`의 `reason: "changed"` event를 publish합니다.

### DAP 전송 계약

`VeroScriptDapStdio::Run`은 caller stream의 Content-Length DAP이고 `FDapStdioRunOptions`는 live binding/catalog/args/stdlib/environment/cwd/input을 제공합니다. live binding은 `Run` 중 borrowed/priority입니다. `program`/`source`, `fileLabel`, `workspace`, `cwd`/`workingDirectory`, `stopOnEntry`, `args`, `input`, `env`/`environment`, `stdlibSearchPaths`/`stdlibPath`, `backend`, `maxCallDepth`, policy를 받습니다. args는 `main(string[]
args)`, input은 script stdin입니다. 제한은 4096 args, 2048 path, 1024 environment이며 invalid input은 localized diagnostic입니다.

Core 디버거의 `bReadOnly` 상태는 `FDebugVariable`, `FDapVariable`를 거쳐 DAP의 `presentationHint.attributes = ["readOnly"]`로 전달됩니다. 중첩 variant의 불변 페이로드는 감시 결과에서도 이 상태를 유지합니다. 불변 값의 수정 거부는 Core가 담당합니다.

`VeroScriptDapTcp::Run`은 IPv4 sequential이고 `FDapTcpRunOptions::BindAddress` default는 `127.0.0.1`, empty/`0.0.0.0`는 all interface, `Port == 0`은 `OnListening`, `MaxSessions == 1`/`0`/negative는 one/unlimited/one rule입니다.

## LSP workspace와 request contract

`VeroScriptLspStdio::Run`은 worker analysis/cancellation을 사용합니다. root priority는 `workspaceFolders`, `rootUri`, FLspStdioRunOptions::DefaultWorkspaceRoots입니다. root는 index용이고 `.vs`만 scan하며 open overlay가 우선합니다. safe default는 2000 ms interactive / 5000 ms semantic, 512 completion, 2000 symbol, 5000 reference, 10000 file, 2 MiB file이며 lspRequestTimeoutMs, maxCompletionItems, maxWorkspaceSymbolResults, maxReferenceResults, maxWorkspaceFiles, maxWorkspaceFileBytes는 safe range로 clamp합니다.

`VeroScriptFormat`은 토큰을 기준으로 동작하는 formatter입니다. 기본값은 들여쓰기 단계마다 공백 4개이며, `bInsertSpaces=false`이면 단계마다 tab 하나를 출력합니다. LSP 서식 지정은 `options.insertSpaces`와 `options.tabSize`를 따릅니다. `insertSpaces=false`이면 tab을, 그렇지 않으면 단계마다 `tabSize`개의 공백을 출력합니다(최대 16, 값이 없거나 잘못되면 4). 범위 서식은 전체 물리 줄로 확장하고 `FRangeFormatResult`에 실제 교체 범위를 반환합니다. 실패하면 LSP 편집 결과는 비어 있습니다. `VeroScriptWorkspace::WorkspaceSymbols`는 열린 문서, overlay, 닫힌 파일을 병합합니다. `FWorkspaceSymbolsOptions::ShouldCancel`은 한 번의 query 동안만 빌려 쓰고 보관하지 않습니다. 취소된 결과는 캐시하거나 게시하지 않으며 기본 캐시는 문서나 설정이 바뀌면 무효화됩니다.

## 소스 문서 주석

소스 정의 전처리기 매크로 completion item에는 바로 앞의 연속된 `//`/`///` 또는 `/* ... */` 주석 문서가 첨부될 수 있습니다. 호스트 completion 문서는 binding metadata에서 가져옵니다. `completionItem/resolve`는 전달된 문서를 보존하고 문서가 없을 때만 `detail`을 Markdown으로 사용합니다. Hover는 소스 선언 앞의 연속 주석을 추출해 `@param 이름 설명`, `@return 설명`, `@returns 설명`을 활성 언어 카탈로그의 레이블로 표시하고 알 수 없는 태그는 본문에 남깁니다. 이는 Tooling 표시 규약이며 컴파일러의 주석 처리는 바꾸지 않습니다.

## 의미 색칠 스케줄링

semantic token은 별도 analysis worker를 사용합니다. 순서가 유지되는 protocol worker가 document update, request state, output을 소유하며 job은 buffer/options/binding 복사본을 소유합니다. version/dependency 검증 후 immutable parsed/token snapshot을 게시합니다. 같은 version의 full/delta와 hover, definition, completion은 유효한 snapshot을 재사용합니다. 색칠용 recovered AST는 point query의 원본으로 사용하지 않습니다. cancel/deadline probe는 lex, preprocess, parse, import, symbol traversal, encode에 전달됩니다. queue cancel은 protocol worker를 깨우고 만료 job은 active analysis를 기다리지 않습니다. 기본 budget은 interactive 2000 ms, semantic 5000 ms입니다. `lspAnalysisTimeoutMs`로 분리하고 `lspTraceAnalysis`로 측정합니다. [예산](../../User/ko-KR/lsp.md)을 참조하십시오.

### 콜백, 캐시 및 수명 주기 계약

`FSemanticTokensOptions::ShouldCancel`과 `RecordStage`는 동기 쿼리 호출 동안만 빌리며 해당 분석 스레드에서 실행합니다. 캡처는 호출이 반환될 때까지 유효해야 합니다. 취소 시 토큰 없이 `bCancelled`를 반환하며 불완전한 결과는 게시하거나 캐시하면 안 됩니다. 측정 콜백의 예외는 무시합니다. `FSemanticTokensResult::Snapshot`은 프로그램과 소스 저장소를 소유하지만 불투명한 Tooling 내부 상태이며 외부 캐시나 재사용 API가 아닙니다.

재사용하려면 문서 내용/버전, 설정, 의존성이 일치해야 합니다. 디스크 기록은 가져온 소스/패키지를 읽기 전에 수집하고 게시 또는 재사용 전에 다시 검사하며, 유효하지 않은 기록은 재사용을 막습니다. 오버레이 변경도 관련 가져오기를 무효화합니다. 같은 URI를 닫았다 다시 열면 버전이 더 작은 값부터 시작해도 분석/토큰 상태와 이전 결과 ID를 지웁니다. 설정 변경은 진행 중인 작업을 무효화합니다. 같은 소스 레이블은 중복 파일 시스템 정규화를 생략하며 다른 레이블은 여전히 정규 경로를 비교합니다.

`VeroScriptLspResponsivenessTest.py`는 실제 stdio 프로세스의 full/delta 재사용, 분석 중 hover, 취소, 기한, 의존성 편집, 설정 변경 및 닫기/다시 열기를 검증합니다. 렉서와 작업 공간 테스트는 취소 후 재시도와 로드에서 스냅샷 생성 사이의 디스크 변경을 다룹니다. 이 회귀 테스트는 담당 Tooling/코어 테스트 영역에 유지합니다.

## 요청 내부의 질의 재사용

참조 수집과 이름 변경은 `Internal/VeroScriptSourceQuerySession`으로 현재 파일의 정확한 파싱 프로그램을 점 질의에 재사용합니다. 상태는 요청 내부에만 존재하고 디스크 의존성을 검사하며 복구 파싱이나 프로세서가 처리한 프로그램은 재사용하지 않습니다. 선언이 현재 소스에 있는 블록/함수 지역 변수는 같은 이름의 다른 열린 모듈을 검색하지 않습니다. 요청 내부 집합으로 토큰 위치를 중복 제거하고 기존 경로의 각 표기는 스캔마다 한 번만 해석합니다.

프로토콜 전용 설정(예산, 추적, 결과 한도)은 의미 분석 스냅샷을 유지합니다. 분석 설정 변경은 스냅샷과 진행 중 작업을 무효화합니다. 워크스페이스 탐색 한도 변경은 심벌 색인만 무효화하며 파싱 프로그램은 유지합니다.

Darwin에서는 `Private/Internal/VeroScriptLspThread.h`가 8 MiB pthread 스택으로 프로토콜 및 의미 분석 작업자를 만들며 다른 플랫폼은 `std::thread`를 사용합니다. join과 소유권을 보존하고 스택 확대를 파서/분석 제한 증가로 해석하지 마세요. JSON 부동소수점 출력은 [Core 십진 변환](../../../../../VeroScript/Resources/Docs/Developer/ko-KR/architecture-and-pipeline.md#floating-formatting-support)의 내부 도우미를 사용합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/architecture-and-protocols.md) · [fr-FR](../fr-FR/architecture-and-protocols.md) · [ja-JP](../ja-JP/architecture-and-protocols.md) · [ko-KR](architecture-and-protocols.md) · [zh-CN](../zh-CN/architecture-and-protocols.md) · [zh-TW](../zh-TW/architecture-and-protocols.md)
