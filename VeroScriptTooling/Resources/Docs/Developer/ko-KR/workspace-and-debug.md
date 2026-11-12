# 워크스페이스, LSP와 DAP

## 워크스페이스 상태

`VeroScriptWorkspace`는 project/module context의 stateful boundary이며 open overlay, source/import resolution, cross-file analysis, workspace symbol, preprocessor context, binding catalog을 소유합니다. open overlay는 disk file보다 우선하고 close로 lifetime이 끝납니다. option은 path, stdlib, implicit import, define/macro, policy, ordered catalog이며 catalog는 merge/duplicate reject하고 host symbol을 기술할 뿐 runtime behavior를 register하지 않습니다.

`TryGetDocument`는 열린 buffer 하나의 독립된 복사본을 반환합니다. 경로가 없으면
`false`를 반환하고 출력을 변경하지 않습니다. 모든 buffer가 필요할 때는
`SnapshotDocuments`를 사용하며, LSP의 단일 document 읽기는 다른 소스를 복사하지 않습니다.

## document lifecycle과 invalidation

`textDocument/didOpen`에서 overlay create, `textDocument/didChange`에서 incremental change, `textDocument/didSave`의 optional `text`로 refresh, `textDocument/didClose`에서 remove합니다. open/accepted change/save/configuration 뒤 diagnostic을 publish합니다. URI version이 old/equal이면 ignore하고 reopen은 new lifetime입니다. `$/cancelRequest`는 pending request를 cancel하고 `-32800`을 반환합니다.

## 언어 기능 처리 흐름

`VeroScriptAnalyzer`, `VeroScriptComplete`, `VeroScriptCallHelp`, `VeroScriptHover`, `VeroScriptNavigation`, `VeroScriptFindReferences`, `VeroScriptDocumentHighlight`, `VeroScriptOutline`, `VeroScriptWorkspaceSymbols`, `VeroScriptRename`, `VeroScriptFormat`이 direct service입니다. LSP는 completion/resolve, navigation과 type hierarchy, document symbol, selection/folding range, document link, semantic token, signature/reference/highlight, rename, document/range/on-type format, `source.formatDocument`, `source.organizeImports`, `refactor.rewrite` code action, workspace symbol/resolve를 expose합니다. `source.organizeImports`는 안전하게 제거할 수 있는 완전히 같은 import가 있을 때만 반환합니다. 빈 커서 위치의 `refactor.rewrite`는 class/struct에 필요한 상속 메서드 구현 skeleton을 제공합니다. range format은 selection을 physical line으로 expand하고 actual replacement range를 반환합니다. namespace는 semantic이며 `std::`, host-only `veroscript:host-bindings`를 포함합니다.

completion에서 `FCompletionItem::MatchQuality`는 primary textual rank이고
`SortPriority`는 동률만 가릅니다. adapter는 result order를 보존해야 합니다. LSP의
`sortText`도 같은 순서를 나타내며, `preselect`는 다른 항목보다 명확히 나은 exact,
case-insensitive-exact, prefix match에만 지정합니다. primitive word는 completion, hover, semantic
token에서 `Keyword`로 유지되고 container word는 `TypeAlias`/`type`입니다. 둘 다 localized display
detail을 갖지만 source definition이 없고 rename target이 될 수 없습니다. 표시에는 `tooling.builtin_type.detail` 및
`tooling.builtin_type.documentation` catalog key를 사용하고 protocol field와 type
identifier는 번역하지 마십시오.

한정 namespace member에서 `FCompletionItem::Label`과 `InsertText`는 마지막 이름과 tooling이
생성한 snippet을 유지하고 `LabelDescription`은 한정 namespace를 담습니다. LSP로 serialize할 때
snippet은 client가 `textDocument.completion.completionItem.snippetSupport`를 선언한 경우에만
포함하고, 그 외에는 마지막 이름을 일반 텍스트로 보냅니다. namespace는 표준 LSP
`labelDetails.description`으로 serialize하십시오. 선택적 `$valencePresentation`은 Valence의
최종 label/declaration 및 UTF-16 semantic span을 더하며 다른 LSP client는 무시할 수 있습니다.
filtering, sorting, 삽입 내용을 바꾸면 안 됩니다.

LSP semantic-token adapter는 resolved semantic span과 lexer baseline을 merge합니다.
fallback은 모든 valid physical line에서 language/type word, directive, macro,
literal/comment를 다루고 parse recovery 중에도 이용할 수 있으며 LSP stream으로 encode하기
전에 overlap을 제거합니다.

## 진단과 현지화

diagnostic은 source file별로 group되어 imported file에 `textDocument/publishDiagnostics`를 보내며 clean/stale state는 empty list로 clear합니다. `initialize.locale`은 BCP 47이고 unsupported는 environment catalog, missing은 `en-US` fallback입니다. identifier/argument/protocol/URI/path/type/source는 translate하지 않습니다.

## 디버깅

`VeroScriptDebugSession`은 Core의 로드, 실행, 중지, 스택, 범위, 값 및 평가를 도구용 데이터로 변환하고 `VeroScriptDapAdapter`는 DAP 메시지에 대응시킵니다. `VeroScriptDapStdio`와 `VeroScriptDapTcp`는 전송 계층이며 VM, 중단점 또는 수명 관리를 다시 구현하지 않습니다. `VeroScriptDebugSession::SetOutputHandler`에는 `VeroScriptDebugSession::FOutputHandler`를 설정합니다. 이 콜백은 `Run()` 중 텍스트와 오류 플래그를 받습니다. `FDebugRunResult`는 `Output`/`ErrorOutput`에 전체 출력을 보관하며 DAP는 종료 전에 `stdout`/`stderr`를 보냅니다. [언어 서버 참조](../../User/ko-KR/lsp.md)도 참고하세요.

## 디버그 세션 계약

`LoadSource`/`LoadFile`은 `FDebugLoadResult`, `Clear`는 state invalidate, `SetBreakpoint`는 session ID입니다. `SetRunMode`, `SetExecutionBackend`, `SetMaxCallDepth`, `SetJitPolicy`, `GetLastJitStats`가 적용되고 attach는 `Vm`/JIT invalidate입니다. stopped 때 `EDebugStopReason`, `GetStackTrace`, `GetLocals`, `GetChildren`, `Evaluate`, `SetLocal`, `SetChild`가 valid하며 `FDebugRunResult` success/exit code는 별개입니다.

`BindBreakpoint`는 adapter의 strict path입니다. one-based physical source line을 정확히 하나의 compiled source에 대한 preprocessor map으로 resolve하고, resolve된 preprocessed location만 `SetExactBreakpoint`에 등록합니다. inactive, missing, ambiguous, out-of-range request 또는 eligible location이 없는 non-executable request는 등록하지 않고 localized display text를 돌려줍니다. function 내부 request는 다음 executable physical line으로 이동할 수 있지만 module initialization은 exact line이어야 합니다.

## DAP 어댑터 수명 주기

`VeroScriptDapAdapter`는 in-process mapper입니다. `Initialize`, `Launch`/`Attach`, breakpoint, `ConfigurationDone`는 `Run`을 block하고 `Stopped`/output/terminal을 emit합니다. `Next`, `StepIn`, `StepOut`, `StackTrace`, `Scopes`, `Variables`, `Evaluate`, `SetVariable`는 stopped callback 한정입니다. `Attach`는 load이지 mid-run attach가 아니며 `Disconnect`는 resume/cleanup합니다. live `IHostBindingCatalog`는 out-of-process로 retain되지 않습니다.

`setBreakpoints`는 source가 entries를 교체해도 stable DAP ID를 유지합니다. load 전 entries는 pending 및 unverified입니다. load 시 `BindBreakpoint`로 rebind하고 각 entry에 `Breakpoint` event를 emit합니다. wire adapter는 이를 표준 DAP `breakpoint` event(`reason: "changed"`)로 serialize합니다. 반환하는 DAP line은 request line이 아니라 실제로 bind된 physical line입니다.

## debug output과 completion

stdout은 line-buffered이고 complete line, `std::console::flush`, bounded buffer에서 flush하며 stderr는 immediate입니다. handler exception은 isolate됩니다. successful int main은 non-zero ExitCode가 가능하고 bSuccess와 다릅니다. preprocessor line map은 physical source를 remap하며 conditional evaluation은 active define/macro를 사용합니다.

## DAP 전송과 시작 계약

`VeroScriptDapStdio::Run`은 Content-Length이고 `FDapStdioRunOptions`는 binding/catalog/args/stdlib/environment/cwd/input을 제공합니다. live binding은 `Run` 동안 borrowed입니다. `program`/`source`, `fileLabel`, `workspace`, `cwd`/`workingDirectory`, `stopOnEntry`, `args`, `input`, `env`/`environment`, `stdlibSearchPaths`/`stdlibPath`, `backend`, `maxCallDepth`를 받습니다. args는 `main(string[] args)`, input은 script stdin, environment는 restore됩니다. 4096/2048/1024 limit을 enforce합니다.

중지할 수 있는 엔드포인트는 `FDapTcpRunOptions::Control`에 공유 `FDapTcpRunControl`을 지정합니다. 호스트의 `RequestStop()`이 소켓 대기를 깨우고 도메인의 연결 해제 정책에 따라 일시 정지된 호출을 재개합니다. 도메인을 해제하거나 모듈을 언로드하기 전에 `Run` 스레드의 종료를 기다립니다. 중지 후 다시 시작할 때는 새 제어 객체를 사용합니다.

`VeroScriptDapTcp::Run`은 IPv4, `127.0.0.1` default, empty/`0.0.0.0`, `Port == 0`/`OnListening`, `MaxSessions == 1`/`0`/negative session rule입니다.

## DAP locale과 Attach termination semantics

DAP locale은 initialize.arguments.locale BCP 47이며 unsupported는 environment catalog를 보존합니다. Launch/Attach load option은 같고 Attach는 mid-run VM attach가 아닙니다. Attach 뒤 Disconnect의 terminateDebuggee default는 false, Launch는 true입니다. false는 session release, true는 host termination request이며 TCP는 MaxSessions policy를 따릅니다.

## 기존 Session 디버깅

호스트가 `IVeroScriptLiveDebugDomain`을 제공하고 수명을 유지합니다. `Info()`는 동기화된 스냅샷입니다. 프로토콜 스레드 요청은 실행 안전 지점에 VM 작업을 예약합니다. 연결 하나는 활성 컨텍스트 하나(thread 1)를 노출하므로 디버깅 호출을 직렬화합니다. `Connect()`는 콜백을 발생시키거나 코드를 시작/리로드하지 않으며 실패 시 콜백을 보관하지 않습니다. `Activate()`는 구성을 완료합니다. `SetBreakpoints`는 한 소스를 원자적으로 교체하고 지정 ID를 유지하여 요청 순서대로 결과를 반환하며 같은 업데이트의 중복 이벤트를 보내지 않습니다. `Disconnect()`는 중단점과 콜백을 제거/정리하고 호스트 정책에 따라 정지를 해제합니다. 취소는 별도 호스트 작업입니다.

콜백과 정지 VM의 검사, 평가, 변경 및 재개는 Session 제어 스레드에서 수행합니다. 정지 콜백은 해당 스레드에서 요청을 처리해야 합니다. 스레드 간 직접 제어는 `RequestPause()`와 공유 `FExecutionControl::RequestCancel()`뿐입니다. 빌린 프로그램, 바인딩, 소스 overlay와 진입 이름 뷰는 콜백 반환 시 만료됩니다. 필요한 메타데이터를 복사하세요. overlay는 현재 구성이며 고정 소스 스냅샷이 아닙니다. 소스/바인딩 리비전은 별도로 게시합니다.

[기존 Session 디버깅](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/session-debugging.md#debug-disconnect-lifetime)을 참조하세요.

## 구현 책임

[C++ embedding과 session — 구현 책임](../../../../../Extensions/Language/Cpp/Resources/Docs/Developer/ko-KR/session.md#session-debug-implementation)을 참조하세요.

## 메모리 검사

`FMemoryInspection`을 디버거 변수로 전달합니다. 허용된 저장 주소는 십진 문자열 `memoryReference`가 되고 `veroMemory`는 제공 가능한 `objectId`, `objectAddress`, `dataAddress`, `byteLength`를 십진 문자열로 반환합니다. uint64 정밀도 및 사용 불가 값의 생략을 유지합니다. 메타데이터 자식에 쓰기 대상이나 가짜 저장 주소를 만들지 않습니다. `variablesReference`는 객체 ID와 별개인 불투명 DAP 핸들입니다. 임의 메모리 읽기/쓰기 기능을 광고하지 마세요.

## 컴파일 처리와 소스 메타데이터

네이티브 호스트는 쿼리, `FWorkspaceOptions`, `FLspStdioRunOptions`, `FDapStdioRunOptions`, `FDapLaunchArgs`의 `CompileProcessing`으로 `FCompileProcessingOptions`를 전달합니다. 일반 프로세서는 모듈 로딩 전에 설치되며 `AnnotationProcessing`과 코어 준비 경로를 공유합니다. 분석, 완성, 탐색, 인덱싱, 디버그에 같은 구성을 사용하세요. 레지스트리는 동결해야 하며 각 컴파일이 별도 인스턴스를 만듭니다. 네이티브 콜백은 서비스 시작 시 제공하며 LSP/DAP JSON으로 보낼 수 없습니다.

일반 의존성 리더가 기존 주석 리더보다 우선합니다. 캐시는 실제 읽은 의존성을 다시 검사하며 캐시 불가 프로세서 결과를 재사용하지 않습니다. 구성 변경 후 `SetOptions`를 호출하세요. 코어의 원본 구간이 `ActiveSource` 편집을 원래 좌표에 연결하여 탐색, 토큰, 진단, 디버그가 공유합니다. 생성 선언은 별도의 읽기 전용 소스를 가집니다. 호버와 완성은 주석 보존 정책에 따른 코어 설명을 사용하며 메타데이터 리플렉션 시그니처도 코어와 일치합니다.

## 스크립트 컴파일 확장

같은 옵션의 `ScriptCompileExtensions`가 확장 루트, 구성, 실행 정책을 지정합니다. `import compiler "extension.vs";`는 코어의 선행 선언 규칙을 따르며 완성, 경로 탐색, 문서 링크는 프로세서를 실행하지 않고 인식합니다.

확장 소스를 편집하려면 `GetScriptCompileServiceCatalog()`를 `Bindings`로 제공하거나 내보낸 카탈로그를 `bindingCatalogPaths`에 지정합니다. 이는 컴파일 전용 `std::compiler` 시그니처이며 일반 코드에 실행 서비스를 제공하지 않습니다.

의미 분석 스냅샷은 처음에 없었던 리소스까지 스크립트 콜백이 읽은 리소스를 검증합니다. 오버레이 버전과 확장 코드 변경도 결과를 무효화합니다. 사용자 지정 리더에서는 종료된 작업의 통합 코드/리소스 리더를 재사용 콜백으로 보존할 수 없어 다시 분석합니다. full/delta는 공개된 토큰을 계속 비교하며 구성 변경 후 `SetOptions`가 필요합니다.

## 프로토콜 편집과 소스 좌표

범위/입력 시 서식은 여러 줄 주석과 원시 문자열의 조각을 보존합니다. 주변 코드를 조정하려면 전체 리터럴이나 문서를 서식 지정하세요.

동적 감시가 있으면 LSP는 바인딩 카탈로그도 감시합니다. 크기와 시간이 같아도 이벤트는 다시 로드하며, 이벤트가 없으면 쿼리 전에 두 속성을 확인합니다. 잘못된 카탈로그는 마지막 정상 스냅샷을 유지하고 상태를 알립니다. 구성 변경 시 감시를 교체합니다. LSP/DAP는 모듈 규칙으로 경로를 정규화하고 중복 제거하며 Windows 대소문자 별칭도 같은 파일입니다. DAP는 카탈로그와 시작 구성을 검증한 후 교체하므로 검증 거부 시 기존 프로그램과 네이티브 인덱스를 보존합니다.

디버그 재로드가 취소되거나 예외를 내면 이전 바이트코드, 바인딩, 소스, 중단점을 지운 뒤 예외를 전달합니다. 다음 로드는 가능합니다. DAP는 실패 응답을 보내며 연결을 유지합니다.

DAP는 스레드 1만 노출합니다. `stackTrace`는 `startFrame` / `levels`를 따르며 0은 나머지 전체, `totalFrames`는 전체 수이고 페이지를 나누어도 중지 프레임 정체성이 유지됩니다. `FDebugStackFrame.Column`은 물리 UTF-8 바이트 열, `FDapStackFrame.Column`은 1부터 시작하는 UTF-16 열이며 전송 계층이 클라이언트의 0/1 기준을 적용합니다. 가져온/생성된 소스도 메타데이터 정리 전에 보정을 저장하며 이후 디스크 편집으로 변하지 않습니다. live domain은 DAP UTF-16 열을 제공해야 합니다.

## 해석된 모듈 환경

분석, 쿼리, workspace, 디버그 및 `FLspStdioRunOptions` / `FDapStdioRunOptions`의 `ModuleEnvironment`는 코어와 같은 불변 `FModuleEnvironment`입니다. 완전한 환경을 제공하며 명시적 live 카탈로그 충돌은 진단을 냅니다. 환경 변경은 인덱스를 무효화하고 편집 없이도 프로세서 의존성을 검증합니다.

공통 리졸버는 import, overlay, 소스 소유권, 가시성, 처리 구성, 리소스를 담당합니다. 호스트 후보는 바인딩 출처와 공개 시그니처의 가시 형식을 따릅니다. 파싱이 범위를 복구하지 못하면 마지막 완성 대안에서 전체 호스트 카탈로그를 열거하지 않습니다. 표준 카탈로그에는 `std::i18n`이 있으며 getter만 있는 속성은 `property` / `readonly`를 유지합니다.

LSP는 `capabilities.experimental.veroscriptModuleEnvironment`에 컴파일/실행 지문과 `moduleSourceMethod: "veroscript/moduleSource"`를 알립니다. `{ "uri": "veroscript-module:/..." }` 요청은 `uri`, `languageId`, `text`, `readOnly`, 컴파일 지문 또는 접근 불가/없음일 때 `null`을 반환합니다. 프레임 한도를 지키며 클라이언트는 해당 스킴의 콘텐츠 공급자를 제공합니다. 일반 파일 URI는 유지합니다. 유효한 재내보내기는 대상만 읽게 하며 모든 간접 모듈을 노출하지 않습니다. rename도 이러한 읽기 전용 문서를 수정하지 않습니다.

디버그는 환경을 보존하고 외부 의존성을 검증합니다. `TryGetSource`와 DAP `sourceReference`는 컴파일한 주 버퍼, 메모리 모듈, 생성 소스의 고정 스냅샷입니다. 물리 파일 라벨이 있어도 이후 디스크 내용으로 대체하지 않습니다. reload/clear는 이전 참조를 무효화하며 live domain은 자체 소스 콜백을 사용합니다. 같은 경로는 마지막 overlay가 우선하며 편집기 버퍼가 모든 쿼리에서 환경 소스를 대체합니다.

접근 검사는 정규화된 형식 정체성을 보존합니다. 다른 네임스페이스의 같은 끝 이름만으로 private/protected/friend 권한을 얻지 못합니다. 짧은 이름은 같은 유일한 선언을 가리켜야 하며 전역 선언과 `::Same`이 우선합니다.

import 정리는 현재 전처리와 버퍼로 확인한 연속 블록의 완전히 중복된 전역 import만 삭제합니다. 매크로가 바꾼 줄, 비활성 분기, 불확실한 의존성, 리터럴, 추가 문장은 보존합니다. 지시문, 주석, 다른 선언, 범위 경계는 블록을 끝냅니다. 이어진 물리 줄이나 어휘 오류에서는 편집하지 않으며 원래 줄바꿈을 유지합니다.

## 호스트가 제공하는 도메인 연결

앞의 로딩 방식 `Attach`는 `program` 또는 `attachMode` 생략 시에 적용됩니다. 지속형 모드에서는 `IVeroScriptLiveDebugDomain`을 `VeroScriptDapStdio::Run` / `VeroScriptDapTcp::Run`의 대응 오버로드에 제공하고 `attachMode: "domain"`, `domainId`를 보냅니다. 전송 전에 `initialize.body.veroscriptSupportsDomainAttach == true`를 확인해야 하며 없거나 false이면 launch/program으로 대체하지 않습니다. 잘못된 필드는 로딩 없이 실패합니다. attach 응답은 해당 도메인의 정체성, 버전, 기능을 제공하며 `configurationDone`은 프로그램 시작 없이 중단점을 활성화합니다. 연결은 이후 호스트 호출에도 유지됩니다. 도메인을 유지하고 디버그 컨텍스트를 스레드 1로 직렬화하세요. 종료 요청은 `veroscriptCanTerminate: true`일 때만 가능하며 그렇지 않으면 연결을 유지한 채 실패합니다.

연결을 끊기 전에 전달된 pause/cancel 호출의 반환을 기다립니다. 닫는 동안 새 제어는 실패하고 오래된 제어는 다음 연결을 대상으로 하지 않습니다. 제어 전달은 연결 종료나 이를 비우는 스레드에 예약된 작업을 기다리면 안 됩니다. 호스트 연결 종료와 콜백 비우기는 제어 mutex 밖에서 실행하며 호스트는 attach, 재개, 연결 종료, 중지 상태 접근을 계속 직렬화합니다.

생성 메서드 완성에는 `FCompletionItem::bOverrideMember`, `bImplementsInterfaceMember`, `Documentation`, `InsertText`, `bInsertTextSnippet`이 있습니다. 순수 인터페이스 구현과 기본 본문이 있는 가상 메서드 등의 상속 override를 구별합니다. 설명은 현재 로캘을 따르고 같은 이름 오버로드는 그룹화합니다. LSP는 명시적 텍스트 편집을 보내며 snippet 미지원 시 일반 텍스트를 사용합니다.

## 프로토콜의 JIT 메타데이터

`Initialize`는 프로토콜 메타데이터와 기능 플래그를 반환합니다. `jitCapability`는 Core JIT 서비스 계약(`backend-services`)을 나타내며 선택한 백엔드나 네이티브 코드 기능을 나타내지 않습니다. 호스트는 `IJitBackendFactory::GetDescription()`으로 해당 기능을 조회합니다. 호스트는 팩터리를 직접 제공하거나 라이브러리 또는 디렉터리의 명시적 UTF-8 경로로 `SetJitBackendPath`를 호출할 수 있습니다. 로더는 ABI를 검사하고 팩터리나 산출물이 유지되는 동안 라이브러리를 유지합니다. `PATH`나 암묵적인 환경 설정에서는 검색하지 않습니다. Tooling 자체는 JIT 모듈을 로드하지 않으며 DAP의 `backend`는 실행 정책만 저장합니다. 디버거가 연결된 실행은 VM을 사용합니다.

## 어댑터의 VM 구성

`FDebugLoadOptions::VmBackendFactory` / `VmBackendPath`가 로드 경로로 전달되도록 유지하세요. `FDapLaunchArgs`와 stdio/TCP 옵션은 호스트의 선택을 전달합니다. DAP 요청으로 네이티브 팩터리를 보낼 수는 없습니다. 팩터리와 경로는 배타적입니다. 디버깅은 VM에서 실행하며 `backend` 필드 자체가 VM을 로드하지 않습니다. 실행이 확정되면 선택은 변경할 수 없으며 유지된 도메인과 호환되어야 합니다. 컴파일 확장은 `ScriptCompileExtensions`에 별도 선택을 지정할 수 있으며, 지정하지 않으면 디버그 로드의 선택을 상속합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/workspace-and-debug.md) · [fr-FR](../fr-FR/workspace-and-debug.md) · [ja-JP](../ja-JP/workspace-and-debug.md) · [ko-KR](workspace-and-debug.md) · [zh-CN](../zh-CN/workspace-and-debug.md) · [zh-TW](../zh-TW/workspace-and-debug.md)
