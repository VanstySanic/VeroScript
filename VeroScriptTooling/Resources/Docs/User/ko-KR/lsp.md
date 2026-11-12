# VeroScript 언어 서버

`veroscript-lsp`는 VeroScript 언어 서버입니다. 표준 입출력에서
`Content-Length`로 감싼 JSON-RPC 메시지를 사용합니다. 프로젝트와 같은
VeroScript 배포판에 포함된 버전을 실행하십시오.

서버는 compiler와 Tooling engine을 직접 사용하므로 language rule, import,
preprocessor, host binding metadata와 diagnostic catalog가 CLI와 동일합니다.

호버는 구조화된 Markdown을 반환합니다. 심볼 제목, 오버로드별로 줄을 나눈 VeroScript
시그니처 코드 블록, 문서 및 정의 위치를 표시합니다. 네이티브 Tooling과 CLI의 호버 요약은
일반 텍스트로 유지됩니다.

## 전송과 수명 주기

일반적인 순서는 `initialize`, `initialized`, document/workspace notification과
request, `shutdown`, `exit`입니다. `shutdown` 응답을 받은 뒤 `exit`를 보내십시오.
shutdown 없이 exit하면 프로세스 결과가 0이 아닌 값이 됩니다.

`exit` 알림을 받으면 대기 중이거나 실행 중인 의미 분석을 취소하므로 분석 기한까지 기다리지 않고 프로세스를 종료합니다.

서버는 request 실행 중에도 입력을 계속 읽고 document update를 수신 순서로 적용합니다.
client는 대기 중인 request에 `$/cancelRequest`를 보낼 수 있으며 취소된 request는
JSON-RPC error code `-32800`을 반환합니다. 입력 queue는 제한되어 backpressure를
적용합니다. closed-file workspace symbol scan은 파일을 열거하고 parse하는 동안에도
취소를 협력적으로 확인합니다. request에는 bounded server deadline이 있으므로 큰
workspace scan이 오래된 editor query를 무기한 유지하지 않습니다. `Content-Length`는
UTF-8 byte 수이며 framing 또는 JSON 오류는 error stream에 기록됩니다.

file URI는 표준 LSP file-URI 규칙을 따릅니다. `file://server/share/module.vs`와 같은
UNC URI는 network path로 유지되며 host와 share 구성 요소를 잃지 않고 왕복됩니다.

client가 `workspace/didChangeWatchedFiles`의 dynamic registration을 advertise하면
서버는 `initialized` 후 `client/registerCapability`를 보내고 `**/*.vs` 및
`**/*.[vV][sS][lL][iI][bB]` watcher를 요청합니다. dynamic registration을 지원하지 않는 client는
이 pattern을 직접 구성한 뒤 `workspace/didChangeWatchedFiles` notification을
명시적으로 보낼 수 있습니다.

## 의미 분석과 요청 예산

문서 갱신은 수신 순서로 적용합니다. 의미 토큰 분석은 문서와 바인딩 스냅샷을 소유한 별도 워커에서 실행하여 색칠 중에도 호버와 완성이 응답할 수 있습니다. 변경 없는 문서는 유효한 파싱 스냅샷과 전체 토큰 스트림을 재사용합니다. 문서/의존성 변경은 영향받는 결과를 무효화하고 분석 설정, 감시 파일과 바인딩 변경은 재분석을 시작합니다. 요청 예산, 추적, 결과 한도만 바꾸면 의미 스냅샷을 유지하며 워크스페이스 탐색 한도는 파싱 프로그램을 지우지 않고 심벌 검색만 갱신합니다. 재사용 전 디스크 의존성의 수정 시각과 크기를 검사합니다. 대체된 분석은 `-32801`을 반환하며 오래된 토큰을 게시하지 않습니다.

같은 URI를 닫았다 다시 열면 새 문서 수명 주기가 시작됩니다. 버전 번호가 다시 시작해도 이전 시맨틱 토큰 결과 ID는 삭제합니다. 이전 ID를 보낸 delta 요청에는 전체 토큰 데이터를 반환하므로 이후에는 새 `resultId`를 사용하세요.

budget은 protocol worker가 dispatch할 때 시작합니다. analysis queue 대기는 포함하고 input queue 대기는 제외합니다. cancel은 `-32800`, timeout은 `-32001`을 반환합니다. 대기 중 만료된 request는 실행 중인 분석을 기다리지 않고 해제합니다. 이전 설정과 호환되도록 `lspRequestTimeoutMs`만 지정하면 analysis budget도 설정합니다. 분리하려면 `lspAnalysisTimeoutMs`를 명시하십시오. trace는 `lex`, `preprocess`, `parse`, import 경로 해석(`imports`), symbol, query, encode, queue를 기록합니다. `load`에는 내부 stage가 포함되므로 시간을 단순 합산하면 안 됩니다.

클라이언트 타임아웃에는 프로토콜 큐 대기와 문서 갱신/진단 처리도 고려해야 합니다. 이 협력적 분석 예산은 요청부터 응답까지의 최대 시간을 보장하지 않습니다.

## 초기화와 설정

`initialize.locale`은 document를 열기 전에 지정하는 BCP 47 언어 preference입니다.
설정은 `initializationOptions.veroscript` 아래에 두며,
`workspace/didChangeConfiguration`에서도 같은 object로 갱신할 수 있습니다.

```json
{
	"locale": "ko-KR",
	"initializationOptions": {
		"veroscript": {
			"importPaths": ["src", "shared"],
			"stdlibPath": "stdlib",
			"defines": ["FEATURE_SCORE=1"],
			"implicitImportPaths": [],
			"allowCircularImports": true,
			"allowProjectWideAccess": false,
			"lspRequestTimeoutMs": 2000,
			"lspAnalysisTimeoutMs": 5000,
			"lspTraceAnalysis": false,
			"maxCompletionItems": 512,
			"maxWorkspaceSymbolResults": 2000,
			"maxReferenceResults": 5000,
			"maxWorkspaceFiles": 10000,
			"maxWorkspaceFileBytes": 2097152,
			"bindingCatalogPaths": ["bindings/core.json", "bindings/game.json"],
			"predefinedMacros": [
				{
					"name": "FEATURE",
					"parameters": ["value"],
					"variadic": false,
					"body": "value",
					"sourceName": "host"
				}
			]
		}
	}
}
```

| 설정 | 유형 | 의미 |
| --- | --- | --- |
| `importPaths` | 문자열 배열 | 추가 module search root입니다. |
| `stdlibPath` | 문자열 | standard library search root입니다. 빈 값은 custom root를 지웁니다. |
| `defines` | 문자열 배열 | `FEATURE_SCORE=1` 같은 object-like preprocessor 정의입니다. |
| `implicitImportPaths` | 문자열 배열 | 일반 visibility rule에 따라 모든 source에서 사용할 file입니다. |
| `allowCircularImports` | boolean | 순환 module dependency를 허용할지 정합니다. |
| `allowProjectWideAccess` | boolean | non-exported declaration의 project-wide 공유를 허용합니다. |
| `lspRequestTimeoutMs` | integer | 대화형 request deadline(milliseconds, 50–30000), 기본 2000. |
| `lspAnalysisTimeoutMs` | integer | semantic full/delta budget(milliseconds, 50–30000), 기본 5000. |
| `lspTraceAnalysis` | boolean | `window/logMessage`로 분석 시간을 출력합니다. 기본 false. |
| `maxCompletionItems` | integer | 한 completion list에서 반환하는 최대 항목 수(32–5000)이며, 잘린 목록은 incomplete로 표시됩니다. |
| `maxWorkspaceSymbolResults` | integer | 한 `workspace/symbol` request에서 반환하는 최대 symbol 수(64–20000)입니다. |
| `maxReferenceResults` | integer | 한 references request에서 반환하는 최대 location 수(64–50000)입니다. |
| `maxWorkspaceFiles` | integer | LSP workspace root 아래 closed-file index에 포함하는 `.vs` 최대 파일 수(1–100000)입니다. |
| `maxWorkspaceFileBytes` | integer | closed-workspace indexing에서 파일 하나에 허용되는 최대 크기(1024–67108864 bytes)입니다. |
| `bindingCatalogPaths` | 문자열 배열 | 순서대로 merge하는 host catalog이며 중복 정의는 거부됩니다. |
| `predefinedMacros` | object 배열 | `name`, `parameters`, `variadic`, `body`, 선택적 `sourceName`을 가진 host macro입니다. |

설정은 같은 project의 CLI와 일치해야 합니다. catalog file이 변경되면 다시 읽고
열린 document를 재분석합니다. catalog는 metadata일 뿐 runtime callback을 등록하지
않습니다.

`initialize` 응답에는 `serverInfo`도 포함됩니다. 여기에는 server name, 생성된 product
version, host platform과 JIT capability가 들어갑니다. 이는 실행 중인 distribution의
metadata이므로 표시에는 사용할 수 있지만, 특정 version을 hard-code하거나 platform 또는
JIT 값으로부터 language semantics를 추론해서는 안 됩니다.

## 제공되는 기능

| 기능 | 계약 |
| --- | --- |
| Text synchronization | open/close, incremental change, text를 포함한 save입니다. |
| Completion | `.`, `:`, `@`가 trigger입니다. completion item은 사용 가능한 resolved documentation을 포함하며 resolve는 이를 보존합니다. documentation이 없을 때만 `detail`을 Markdown으로 승격합니다. |
| Hover | 확인된 심볼 detail 및 연속된 소스 또는 호스트 문서입니다. 소스의 `@param`, `@return(s)` 태그는 현지화된 레이블로 표시됩니다. |
| Navigation | definition, declaration, type definition, implementation입니다. |
| Symbols and ranges | 중첩 document symbol과 token·line·document selection range입니다. |
| Document links | 따옴표로 감싼 `import`와 `#include` target link입니다. |
| Folding ranges | block, 연속 comment, import group을 위한 lexer-safe folding range입니다. |
| Semantic tokens | full과 full delta이며 range request는 advertise하지 않습니다. |
| Signature and references | signature help, references, document highlight입니다. |
| Rename and formatting | prepare rename, rename, document formatting, 물리적 전체 줄로 확장되는 range formatting과 on-type formatting입니다. |
| Code action | `source.formatDocument`, 완전히 중복된 import를 안전하게 제거하는 `source.organizeImports`, 메서드 구현을 생성하는 `refactor.rewrite`, `codeAction/resolve`를 지원합니다. |
| Workspace symbols | source, host, soft symbol을 대상으로 case-insensitive search를 수행합니다. |

## 프로토콜 메서드

서버는 다음 lifecycle 및 synchronization 메시지를 받습니다.

- `initialize`, `initialized`, `shutdown`, `exit`;
- `textDocument/didOpen`, `textDocument/didChange`, `textDocument/didSave`,
  `textDocument/didClose`;
- `workspace/didChangeConfiguration`, `workspace/didChangeWorkspaceFolders`,
  `workspace/didChangeWatchedFiles`; 그리고
- 대기 중인 request를 취소하는 `$/cancelRequest`.

기능 요청 메서드는 다음과 같습니다.

- `textDocument/completion` 및 `completionItem/resolve`;
- `textDocument/hover`, `textDocument/definition`,
  `textDocument/declaration`, `textDocument/typeDefinition`,
  `textDocument/implementation`;
- `textDocument/prepareTypeHierarchy`, `typeHierarchy/supertypes`,
  `typeHierarchy/subtypes`;
- `textDocument/documentSymbol`, `textDocument/selectionRange`,
  `textDocument/documentLink`, `textDocument/foldingRange`;
- `textDocument/semanticTokens/full` 및
  `textDocument/semanticTokens/full/delta`;
- `textDocument/signatureHelp`, `textDocument/references`,
  `textDocument/documentHighlight`;
- `textDocument/prepareRename` 및 `textDocument/rename`;
- `textDocument/formatting`, `textDocument/rangeFormatting`,
  `textDocument/onTypeFormatting`, `textDocument/codeAction`,
  `codeAction/resolve`; 그리고
- `workspace/symbol`, `workspaceSymbol/resolve`.

## 문서 동기화

text sync는 incremental(`textDocumentSync.change = 2`)입니다. client가 보낸 순서대로
memory overlay에 적용하며 full-text change와 ranged change를 모두 받습니다. LSP line은
zero-based이고 character는 UTF-16 code unit입니다.

URI의 각 열린 수명 주기 안에서 document version은 monotonic합니다. 마지막으로 수락한 version 이하의 `didChange`는
무시됩니다. `didSave`는 complete text를 포함할 수 있고 diagnostic을 보내기 전에
overlay를 갱신합니다. `didClose`는 overlay를 제거하고 해당 URI의 diagnostic을 지웁니다.
열린 buffer는 import 해결에서 disk file보다 우선합니다.

`workspace/didChangeConfiguration`은 새 설정을 적용하고 열린 document를 재분석합니다.
`workspace/didChangeWorkspaceFolders`는 추가 및 제거된 workspace root를 적용합니다.
`workspace/didChangeWatchedFiles`는 workspace symbol index를 무효화하고 on-disk 변경을
반영하기 위해 열린 document를 재분석합니다.

`initialize.workspaceFolders` 또는 `rootUri`가 closed-file indexing에 사용할 명시적인
workspace root를 정합니다. 열린 buffer는 항상 우선하며,
`maxWorkspaceFiles`와 `maxWorkspaceFileBytes`는 해당 root 아래의 closed file에
적용됩니다.

## Tooling 의미론

- completion은 표시 이름이 같은 callable overload를 하나의 항목으로 묶고, detail에
  각 signature를 별도 줄로 표시합니다. call context에서 묶인 overload 항목은 특정 overload의
  parameter를 추측하는 대신 `Select(${0})`처럼 비어 있는 argument stop을 사용합니다.
  overload가 없는 callable은 `Helper(${1:count}, ${2:name})` 같은 LSP snippet을 포함할 수 있고, generic type은
  `HostPair<${1}, ${2}>` 같은 snippet을 포함할 수 있습니다. generic completion의 replace range 뒤에 `<`가
  있으면(space나 tab을 사이에 둘 수 있음) generic snippet을 생략하고 type name만 삽입해 `<` 중복을 피합니다.
  completion target 뒤에 `(`가 있으면 callable completion snippet을 생략해 호출 구문이 중복되지 않게 합니다.
  client가 `textDocument.completion.completionItem.snippetSupport`를 true로 설정한 경우에만
  snippet 삽입 문자열을 반환합니다. 지원하지 않으면 일반 문자열을 반환합니다. client가 snippet을 지원할 때
  call context의 variadic host formatting function은 합쳐진 overload detail에서 parameter placeholder를 만들지 않고
  `print(${0})`처럼 비어 있는 argument tab stop을 사용합니다. 지원하지 않으면 함수 이름만 삽입합니다.
- 한정 namespace의 completion은 `label`에 마지막 이름을 유지합니다. client가 snippet 지원을 선언한
  경우에 사용 가능한 callable 또는 generic type snippet을 `insertText`와 (필드가 있는 경우) `textEdit.newText`에 사용하고,
  그 외에는 이름만 일반 텍스트로 삽입합니다. container는 표준 `labelDetails.description`에 들어갑니다.
  선택적 `$valencePresentation` metadata는 Valence에 최종 label/declaration과 UTF-16
  semantic span을 제공하며 다른 LSP client는 무시할 수 있습니다. filtering, sort order,
  삽입 text는 바뀌지 않습니다.
- namespace segment completion은 마지막 이름을 `label`과 `filterText`에 유지합니다. expression/statement 경로에서
  namespace를 선택하면 replace range 뒤에 `::`가 없을 때 삽입 문자열에 추가합니다. 이미 `::`가 있거나
  `using namespace` 대상을 완성하는 경우에는 이름만 삽입합니다.
- completion은 textual-match quality를 contextual priority보다 먼저 정렬합니다. 순서는
  exact, case-insensitive exact, prefix, word-boundary/camel-case, contained,
  subsequence입니다. `sortText`가 이 완전한 순서를 담습니다. 첫 item에는
  exact/case-insensitive-exact/prefix tier가 다른 모든 반환 item보다 엄격히 우수할 때만
  `preselect`가 설정됩니다.
- primitive/container word는 built-in type입니다. primitive spelling은 completion, hover, semantic
  token에서 `keyword`로 유지되고 container spelling은 `type`/type alias입니다. 둘 다 localized
  hover detail을 제공하며 source definition이 없고 rename할 수 없습니다.
- `completionItem/resolve`는 원래 item과 이미 제공된 documentation을 유지합니다. documentation은 없고 `detail`이 있을 때만 detail을 Markdown으로 추가합니다.
- Hover는 소스 선언 바로 앞의 연속 주석을 읽습니다. 소스에서 정의한 전처리기 매크로의 completion item에는 앞선 주석이 포함될 수 있으며, 호스트 completion 문서는 binding metadata에서 가져옵니다. Hover는 `@param`, `@return`, `@returns`를 현지화된 레이블로 표시합니다.
- user namespace와 예약된 `std::` tree는 hover, navigation, reference, rename,
  outline, semantic token에서 namespace symbol로 처리됩니다. standard library
  namespace는 rename할 수 없습니다.
- `textDocument/declaration`은 definition과 같은 위치를 반환합니다. type definition은
  class, struct, interface, enum, type alias 선언을 찾고 불완전한 buffer에서는 outline과
  declaration-head recovery를 사용합니다. implementation은 파생 type과 override member의
  implementation 위치를 반환합니다. parameter와 `const` qualifier가 일치하는 `final`이
  아닌 virtual, `override`, abstract member만 대상이 되며, 모호한 type name은 제외됩니다.
  definition 조회를 그대로 반복하는 기능은 아닙니다.
- selection range는 token, line, whole document 순서로 parent를 가집니다. document link는
  선택적인 `export` prefix를 포함해 따옴표로 감싼 `import`와 `#include`를 현재 document에서
  해석합니다.
  코어 module resolver를 사용하여 현재 디렉터리, 설정한 검색 경로, 표준 라이브러리와
  저장하지 않은 열린 document를 조회합니다. 해석하지 못한 target은 로컬 link를 유지합니다.
- semantic-token delta는 이전 result가 없거나 오래되면 전체 data를 반환하고, 일치하면
  최소 연속 차이 edit와 새 result id를 반환합니다.
- semantic token legend의 전체 종류는 `namespace`, `type`, `class`, `enum`,
  `interface`, `struct`, `variable`, `property`, `enumMember`, `function`,
  `method`, `macro`, `keyword`, `decorator`, `decoratorArgument`, `constant`,
  `label`, `string`, `number`, `comment`, `preprocessor`입니다. lexical baseline은
  language/type word, directive head, macro name도 분류합니다. 겹치는 LSP span 없이 모든
  유효 physical line을 다루며, 불완전한 입력에서 declaration analysis가 복구되는 동안에도
  계속 사용할 수 있습니다.
- range formatting은 안전하게 전체 물리 줄로 확장하고 외부 block indent를 유지합니다.
  문서 전체, 범위, 입력 중 서식 지정은 `insertSpaces` 설정을 따릅니다. `false`이면 들여쓰기 단계마다
  tab 하나를 출력하고, `true`이면 단계마다 `tabSize`개의 공백을 출력합니다(최대 16, 없거나 잘못된 값이면 4).
- code action은 `context.only`가 제외하지 않는 한 `source.formatDocument`를 반환합니다.
  완전히 같은 import 줄을 안전하게 찾아 제거할 수 있을 때만 해당 중복 줄만 제거하는
  `source.organizeImports`도 반환합니다.
- class 또는 struct 본문에서 유효한 빈 커서 위치에 completion은 접근 가능하고 아직 final이 아니며
  구현되지 않은 상속 virtual 멤버와, 구현해야 하는 순수 interface 멤버 또는 override할 수 있는
  virtual default 멤버를 제안합니다. generic signature는
  파생 형식에 맞게 구체화됩니다. 항목을 선택하면 편집 가능한 `throw 0;` 본문을 가진 메서드가
  삽입되며, pure interface 구현에는 `override`가 붙지 않습니다. 식별자 prefix를 입력하는 중에도
  completion을 사용할 수 있습니다. 빈 커서 위치에서는 같은 후보를 `refactor.rewrite` code action으로도
  제공합니다. snippet을 지원하지 않는 client에는 일반 메서드 텍스트를 삽입합니다.
- 빈 커서 위치의 code action은 completion과 같은 메서드 구현을 `refactor.rewrite`로 제공합니다.
  `context.only`는 `refactor` 또는 `refactor.rewrite`로 이 action을 필터링할 수 있습니다.
- workspace symbol query는 대소문자를 구분하지 않고 공백으로 나눕니다. symbol name,
  qualified name, container, detail, file label을 검색하며 `::`와 기존 `.` 구분자를 같은
  방식으로 처리합니다. 정확 일치와 prefix 일치가 fuzzy subsequence 일치보다 먼저 정렬되고,
  각 item에는 안정적인 `data.veroscriptSymbolId`가 포함됩니다. `workspaceSymbol/resolve`는
  detail과 container를 갱신하며 ID를 찾지 못하면 원래 item을 그대로 반환합니다.
- document symbol과 workspace symbol은 namespace container를 공개합니다. 여러 import 경로를
  통해 같은 declaration identity가 관찰되어도 결과에는 한 번만 나타납니다. source file이 없는
  host symbol은 `veroscript:host-bindings` URI와 zero range를 사용합니다.

open, 승인된 change, save 또는 configuration change 뒤에는
`textDocument/publishDiagnostics`를 보냅니다. imported file을 포함해 파일별로 묶고 빈
배열로 오래된 marker를 제거합니다. 각 diagnostic에는 LSP severity, message, 가능한 경우
stable code와 `source: "veroscript"`가 포함됩니다. source file이 없는 host symbol은
`veroscript:host-bindings` URI와 zero range를 사용합니다.

적용 가능한 결과가 없으면 서버는 해당 LSP method에 맞는 null 또는 empty result를 반환합니다.
지원하지 않는 method는 JSON-RPC `-32601`을 반환하고, document 또는 position이 필요한
method의 잘못된 parameter는 `-32602`를 반환합니다.

## 현지화

서버는 CLI와 runtime과 같은 localization policy를 사용합니다. `initialize.locale`은
표시만 바꾸며 script semantics나 symbol resolution은 바꾸지 않습니다. client는 번역된
text가 아니라 message ID/code를 logic 경계로 사용해야 합니다. 누락된 file이나 key는
`en-US`로 fallback합니다. [진단 현지화](../../../../../VeroScript/Resources/Docs/User/ko-KR/localization.md)를 참조하십시오.

## 관련 참조

[Tooling 모듈](index.md), [CLI 참조](../../../../../VeroScriptCli/Resources/Docs/User/ko-KR/index.md),
[Host binding](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/bindings.md), [디버깅](debugging.md)도 함께 참조하십시오.

## 호스트가 제공하는 모듈 소스

호스트는 해석된 모듈 환경과 컴파일 프로세서를 제공할 수 있습니다. 기본 Runtime 서버는 CLI의 패키지 선택이나 애플리케이션의 네이티브 콜백을 자동으로 가져오지 않습니다. 편집하는 프로그램에 맞는 가져오기 경로, 바인딩 카탈로그 및 호스트 서비스를 사용하세요. 카탈로그는 설명을 제공하며 실행 가능한 프로세서를 설치하지 않습니다.

`capabilities.experimental.veroscriptModuleEnvironment`가 `moduleSourceMethod`를 알리면 지원 클라이언트는 해당 메서드(`veroscript/moduleSource`)로 반환된 `veroscript-module:` 위치를 읽기 전용으로 열 수 있습니다. 이는 편집 가능한 작업 영역 파일이 아닌 스냅샷입니다. 서버가 다시 시작되거나 컴파일 지문이 바뀌면 원본 소스에서 다시 이동하세요. 없거나 접근할 수 없는 모듈은 `null`을 반환합니다. 일반 디스크 소스는 계속 파일 URI를 사용합니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/lsp.md) · [fr-FR](../fr-FR/lsp.md) · [ja-JP](../ja-JP/lsp.md) · [ko-KR](lsp.md) · [zh-CN](../zh-CN/lsp.md) · [zh-TW](../zh-TW/lsp.md)
