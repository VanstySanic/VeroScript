# WebAssembly runtime과 binding

한 줄 표기도 정확히 유지합니다: `void main(string[] args)`, `int main(string[] args)`.
## 런타임 팩터리

`createVeroScriptRuntime()`는 interactive input용 Asyncify-capable factory입니다. `createFastVeroScriptRuntime()`는 whole-program Asyncify stack instrumentation을 생략하고 모든 input이 run 전에 제공될 때 preferred path입니다. capability flag가 허용하면 두 factory는 같은 language/result contract를 노출합니다.

`runSource`는 module creation 뒤 synchronous입니다. `runSourceInteractive`는 input provider가 logical line을 기다리는 동안 VM/script state를 suspend합니다. 한 Wasm module에는 하나의 interactive run만 active일 수 있습니다. provider exception은 runtime이 안전하게 input wait를 떠난 뒤 promise를 reject합니다.

[WebAssembly/WASM SDK](../../User/ko-KR/index.md#runtime-factory-configuration)을 참조하세요.

## input과 result

run은 file label, optional virtual file, standard input, script argument, 선택 `vm`/`mixed`/`jit` backend를 받습니다. `void main(string[]
args)`, `int main(string[]
args)`를 지원합니다. non-zero integer result도 successful run이며 `exitCode`로 반환합니다. embedded NUL input은 reject합니다.

browser baseline은 script-level worker thread를 advertise하지 않습니다. unbounded program은 owning Web Worker를 terminate해 stop합니다. independent program은 independent runtime module/worker를 사용합니다.

[WebAssembly ABI, artifact, release 계약 — 프로그램과 입력 기능](release-contract.md#web-execution-policy)을 참조하세요.

필드와 메서드의 실행 가능한 리플렉션은 `vm`, `mixed`에서 동작합니다. 공개 브라우저 모듈에는 pthread가 없으므로 새 callable 작업과 기존 작업 모두 시작할 수 없습니다. `is_file`, `is_directory`, `remove_dir`는 내장 표준 라이브러리를 포함한 모듈의 가상 파일 시스템에만 접근하며 방문자의 로컬 파일에는 접근하지 않습니다.

## 컴파일된 프로그램

`compileSource`는 source file label/in-memory virtual file을 받고 result와 optional owned `VeroScriptProgram`을 반환합니다. program handle은 source graph를 reparse하지 않고 새 input, argument, backend selection으로 반복 run할 수 있습니다. 각 run은 fresh execution state이고 compilation diagnostic은 compile result에 남습니다.

`VeroScriptProgram.dispose()`는 native handle을 release하며 SDK boundary에서 idempotent입니다. runtime은 live program을 track하고 runtime disposal 때 release합니다. interactive compiled execution은 Asyncify artifact, `readLine` provider, 같은 WebAssembly module에 다른 active interactive run이 없음을 요구합니다.

## complete source와 compiled-program surface

TypeScript SDK는 runSource를 `vero_web_run_source_with_context`에 map하고 script argument, selected backend, paired in-memory virtual file을 포함합니다. compileSource는 `vero_web_compile_source_with_context`에 map되고 success result는 VeroScriptProgram을 own합니다. VeroScriptProgram.run은 `vero_web_run_program`, runInteractive는 Asyncify-only `vero_web_run_program_interactive`를 사용합니다. program run은 fresh VM state에서 input, argument, backend를 선택합니다.

native C++ surface에는 `FRuntime::RunSource`, `RunSourceWithInputStream`, `CheckSource`, `SetLanguage`, `GetAvailableLanguages`가 있습니다. C ABI는 argument, option, context, compile, program, interactive, context-check entry point도 제공합니다. C ABI array/string은 call 동안 borrowed이고 returned JSON/result pointer는 runtime-owned lifetime rule을 따릅니다. VeroScriptResult는 backend, output, error output, diagnostic, timing, program handle, per-run JIT statistic을 separate field로 가집니다.

## result와 JIT field mapping

`VeroScriptResult.jit`는 검사, 컴파일, API 오류 또는 소스 로드 실패 시 null입니다. 로드에 성공한 실행은 백엔드 없는 명시적 `jit` 요청처럼 스크립트 초기화 전에 거부되더라도 실행별 카운터를 반환합니다. 공개 모듈의 모든 카운터는 0입니다. `directNativeCalls`와 `compileAttempts`는 통계이며 언어나 ABI 버전이 아닙니다.

## 관리 메모리

[WebAssembly/WASM SDK — 관리 메모리](../../User/ko-KR/index.md#managed-memory-contract)을 참조하세요.

## 실패 정리와 스냅샷

입력 브리지의 메모리 할당 실패나 Wasm32 크기 한도 초과 시 네이티브 스택을 재개하고 API 실패 결과를 반환하므로 모듈을 다음 실행에도 사용할 수 있습니다. 오류 주입은 `vm`과 `mixed`의 소스 실행 및 컴파일된 실행과 각 실패 후 복구를 검증합니다. 정상 EOF 동작은 유지됩니다.

실패 시에도 소유권에 따라 정리합니다. 대화형 입력 줄을 복사하다 예외가 발생해도 입력 메모리를 해제하며, 결과 JSON을 만들 수 없는 컴파일된 프로그램은 핸들을 호출자에게 반환하기 전에 제거합니다. 읽기 전용 lease는 독립 스냅샷을 한 번 생성하고 `toArray`는 전체를 다시 복사하지 않고 해당 스냅샷을 반환합니다. 회귀 테스트는 스냅샷의 독립 수명과 컴파일된 실행이 실패한 뒤 관리 메모리 전역 변수가 다시 초기화되는지 확인합니다.

## 코어 호환성과 진단

[WebAssembly/WASM SDK — 코어 호환성과 진단](../../User/ko-KR/index.md#compiler-diagnostics)을 참조하세요.

## 대화형 입력 소유권

대화형 호출은 옵션을 읽은 뒤 모듈 상태를 다시 확인합니다. 재진입 요청이나 폐기된 런타임의 요청은 입력 공급자를 인수하기 전에 거부되므로 실행 중인 프로그램의 콜백과 후속 입력이 유지됩니다.

문자열 포인터 테이블은 실제 전달 항목 수로 할당하여 사용자 지정 또는 변경되는 배열 반복이 초기 길이 기반의 할당 범위를 넘어서 쓰지 않도록 합니다. 동기 및 비동기 헬퍼 모두 Wasm32 한도를 검증합니다. 런타임 폐기는 추적 중인 프로그램을 직접 순회하며 별도 배열을 만들지 않습니다.

[WebAssembly/WASM SDK — 대화형 입력 소유권](../../User/ko-KR/index.md#input-provider-failures)을 참조하세요.

## 콘솔 출력과 기능

[WebAssembly/WASM SDK — 콘솔 출력과 기능](../../User/ko-KR/index.md#console-capabilities)을 참조하세요.

잘못된 색상 이름, 지우기 모드, 커서 좌표, 인수 형식 또는 개수는 터미널 기능이 지원되지 않더라도 진단을 생성합니다. 거부 후에도 런타임을 다시 사용할 수 있습니다. 캡처한 텍스트는 실행이 완료될 때 `VeroScriptResult`로 반환됩니다.

## 현재 언어 기능

[WebAssembly/WASM SDK — 현재 언어 기능](../../User/ko-KR/index.md#compiler-and-application-services)을 참조하세요.

공개 C ABI와 TypeScript 옵션은 `FModuleEnvironment`, 네이티브 확장 등록, 리소스 공급자 또는 `FEngineConfig::I18nServices`를 노출하지 않습니다. Core는 사용자 정의 C++ WebAssembly 빌드에서 정적 네이티브 등록을 지원하며, 공개 엔진은 데스크톱 동적 라이브러리를 불러올 수 없습니다. 이 호스트 통합에는 별도로 구성한 산출물이 필요합니다. 기존 소스 오버레이와 프로그램 소유권 계약은 계속 적용됩니다.

## 주석 이름 일치 규칙

[WebAssembly/WASM SDK — 주석 이름 일치 규칙](../../User/ko-KR/index.md#annotation-name-matching)을 참조하세요.

허용 이름이 겹치면서 정의가 충돌하는 주석 등록은 컴파일 진단을 생성합니다. 두 엔진은 검사, 소스 실행, 컴파일된 프로그램 생성에 같은 규칙을 적용합니다. 컴파일된 프로그램은 정의의 규칙을 유지하므로 변경하려면 다시 컴파일해야 합니다. Web C ABI와 TypeScript 옵션은 바뀌지 않습니다. 소스 메타데이터 계약이 바뀌면 이전 개발 버전 패키지와 캐시를 소스에서 다시 생성하십시오. SDK 프로그램 핸들은 계속 메모리에만 유지됩니다.

## Core 실행과 한도

[WebAssembly/WASM SDK — Core 실행과 한도](../../User/ko-KR/index.md#numeric-and-library-limits)을 참조하세요.

컴파일 프로그램을 반복 실행할 때 내부 작업 공간은 재사용할 수 있지만, 실패 후를 포함해 매번 스크립트 전역 변수, 지역 변수, 인수를 새로 초기화합니다. Core 변경 후에는 두 Wasm 변형을 다시 빌드하고 기존 SDK 릴리스 검증을 실행하십시오. 이전 SDK 검증 기록은 기록에 명시된 산출물에만 적용됩니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/runtime-and-bindings.md) · [fr-FR](../fr-FR/runtime-and-bindings.md) · [ja-JP](../ja-JP/runtime-and-bindings.md) · [ko-KR](runtime-and-bindings.md) · [zh-CN](../zh-CN/runtime-and-bindings.md) · [zh-TW](../zh-TW/runtime-and-bindings.md)
