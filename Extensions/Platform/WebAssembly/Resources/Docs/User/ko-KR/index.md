# WebAssembly/WASM SDK

`VeroScriptWebAssembly`은 브라우저와 기타 JavaScript 호스트를 위한 UI 비의존 WebAssembly SDK입니다. 메모리 내 소스 검사와 실행을 제공합니다. 화면, 편집기, 출력 패널과 프레임워크 연동은 호스트가 담당합니다.

<a id="runtime-factory-configuration"></a>

TypeScript 팩토리는 Emscripten 인스턴스를 만들기 전에 호출자의 설정을 복사합니다. 동결된 객체를 포함해 같은 설정 객체를 재사용해도 런타임의 내보내기, 힙 및 리소스 소유권은 독립적입니다. 상속된 `locateFile`과 `wasmBinary` 설정도 유지됩니다.

## 산출물과 배포

동일한 빌드에서 생성된 모듈, WASM, 선언 파일을 하나의 세트로 배포합니다.

| 산출물 | 용도 |
| --- | --- |
| `veroscript-engine.mjs` | 대화형 입력을 지원하는 Emscripten 모듈. |
| `veroscript-engine.wasm` | 일반 런타임 WebAssembly 바이너리. |
| `veroscript-engine-fast.mjs` | Asyncify 계측이 없는 저지연 런타임. |
| `veroscript-engine-fast.wasm` | 빠른 런타임 WebAssembly 바이너리. |
| `veroscript-webassembly.ts` | TypeScript 래퍼. |
| `veroscript-webassembly.d.ts` | 래퍼와 WebAssembly C ABI 선언. |
| `veroscript-engine.d.mts`, `veroscript-engine-fast.d.mts` | Emscripten 모듈 옵션 선언. |

표준 라이브러리와 지역화 카탈로그는 가상 파일 시스템에 포함됩니다. `.mjs`, `.wasm`, 선언 파일과 ABI는 서로 일치해야 하며 생성된 배포 파일을 직접 편집하지 마십시오. TypeScript 진입점은 선택한 Emscripten 엔진을 필요할 때만 로드합니다. 래퍼를 import하는 것만으로는 어느 엔진도 다운로드하지 않으며, 빠른 런타임은 빠른 엔진만, 일반 런타임은 대화형 엔진만 로드합니다.

## 런타임 선택

스크립트가 비동기 입력을 기다릴 수 있으면 `createVeroScriptRuntime`, 입력이 이미 준비된 검사와 실행에는 `createFastVeroScriptRuntime`을 사용합니다.

```TypeScript
const fast = await createFastVeroScriptRuntime();
console.log(fast.supportsInteractiveInput); // false
fast.dispose();

const normal = await createVeroScriptRuntime();
console.log(normal.supportsInteractiveInput); // true
normal.dispose();
```

빠른 런타임은 전체 Asyncify 계측을 포함하지 않으므로 `runSourceInteractive`를 사용할 수 없습니다. 일반 런타임은 `readLine` Promise가 완료될 때까지 스크립트를 일시 중지합니다.

`read_line`, 형식별 입력 함수 또는 `pause`로 비동기 입력을 기다리는 스크립트에는 일반 런타임과 `runSourceInteractive`를 사용합니다. 빠른 런타임은 입력 데이터가 이미 준비된 경우에 사용하며 비동기 입력을 기다릴 수 없습니다.

## 백엔드와 API

`checkSource`는 `main`을 호출하지 않고 소스를 검사하며 `runSource`는 검사 후
실행합니다. `supportsCompiledPrograms`와 `supportsThreads`로 artifact capability를
검색할 수 있고, 게시된 artifact는 각각 `true`와 `false`를 반환합니다. fast
runtime은 대화형 입력을 지원하지 않습니다.

공개 모듈은 JIT 백엔드를 선택하지 않습니다. `vm`은 바이트코드를 해석하며 기본값인 `mixed`도 VM으로 실행합니다. `jit`는 애플리케이션 초기화, `main` 또는 입력 요청 전에 구조화된 진단 `diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory`를 반환합니다. 기존에 `jit`를 지정한 호출자는 `vm` 또는 `mixed`를 선택해야 합니다. 정책 이름과 C ABI ID는 유지되며 `result.backend`는 요청한 정책을 나타내고 모든 JIT 카운터는 0입니다.

공개 API에는 `VeroScriptDiagnosticSeverity`, `VeroScriptExecutionBackend`, `VeroScriptLanguage`, `VeroScriptDiagnostic`, `VeroScriptResult`, `VeroScriptJitStatistics`, `VeroScriptVirtualFile`, `VeroScriptInputRequest`, 실행 옵션, `VeroScriptProgram`, `VeroScriptCompileResult`, `VeroScriptProgramRunOptions`, `VeroScriptProgramInteractiveRunOptions`, `VeroScriptRuntime`, `VeroScriptEngineModuleOptions`, `createVeroScriptRuntime` 및 `createFastVeroScriptRuntime`이 포함됩니다. `VeroScriptInputRequest.index`는 0부터 시작하는 입력 요청 인덱스입니다. `VeroScriptProgram`은 `disposed`, `run`, `runInteractive`, `dispose`를 제공합니다. `runtime.supportsCompiledPrograms`는 재사용 가능한 컴파일을 지원하는지 나타냅니다. `checkSource`는 `main`을 호출하지 않고 검사하고, `runSource`는 검사 후 실행합니다.

`VeroScriptResult`의 기본 필드는 `abiVersion`, `operation`, `success`, `exitCode`, `exitRequested`, `backend`, `output`, `errorOutput`, `diagnostics`입니다. 컴파일과 실행 결과에는 `programHandle`, `timings`(`timings.compileMs`, `timings.executeMs`)와 실행별 JIT 활동을 나타내는 `jit`가 포함될 수 있습니다. `VeroScriptJitStatistics`는 `compileAttempts`, `compiledEntries`, `bridgeTrampolineEntries`, `linearTrampolineEntries`, `vmTrampolineEntries`, `unsupportedEntries`, `dispatchAttempts`, `dispatches`, `fallbacks`, `hotFunctions`, `directNativeCalls`를 제공합니다. `directNativeCalls`는 VM 값 브리지를 거치지 않고 다른 네이티브 진입점에 도달한 세대 범위 JIT 호출 수입니다. `VeroScriptResult.success`는 작업이 정상 완료되었는지를 나타내며 `exitCode === 0`과 같은 의미가 아닙니다. `exitRequested`는 스크립트가 `exit(n)`을 명시적으로 호출했을 때만 true입니다.

바인딩된 메서드 호출처럼 네이티브 프레임을 통해 VM에 재진입하는 경로에는 코어의 플랫폼별 스택 보호가 적용됩니다. 중첩이 너무 깊으면 `diag.call_stack_overflow`가 반환됩니다. 이 보호는 일반 인터프리터 재귀의 호출 깊이 제한과 별개입니다. 다시 시도하기 전에 네이티브 재진입의 중첩을 줄이세요.

## 한 번 컴파일하고 반복 실행하기

변경되지 않은 소스를 반복 실행할 때는 `compileSource`를 한 번 호출하고
반환된 프로그램을 재사용하십시오. 매번 구문 분석, import 확인, 의미 분석과
바이트코드 생성을 반복하지 않습니다.

```TypeScript
const compiled = runtime.compileSource(
	`int main(string[] args)
{
	return len(args);
}`,
	{ fileLabel: "main.vs" },
);

if (compiled.program) {
	const first = compiled.program.run({ args: ["one", "two"], backend: "mixed" });
	const second = compiled.program.run({ args: ["one"], backend: "vm" });
	console.log(first.exitCode, first.timings?.executeMs, first.jit?.dispatches);
	console.log(second.exitCode, second.timings?.executeMs);
	compiled.program.dispose();
}
```

컴파일에는 `fileLabel`과 `files`를 사용할 수 있지만 실행 시점의 `input`,
`args`, `backend`는 포함하지 않습니다. 각 `program.run`은 새로운 실행 상태를
사용하며 입력, 인수와 백엔드를 개별적으로 선택할 수 있습니다. 컴파일된
바이트코드는 공유되고 VM 상태와 JIT 통계는 실행마다 새로 만들어집니다.
일반 런타임에서는 `program.runInteractive({ args, backend, readLine })`도
사용할 수 있지만 빠른 런타임에서는 대화형 실행을 사용할 수 없습니다.
서로 다른 릴리스의 산출물을 사용할 수 있다면 `supportsCompiledPrograms`와
`supportsInteractiveInput`을 확인하십시오. 더 이상 필요하지 않은 핸들은
`program.dispose()`로 해제하며 런타임을 폐기하면 남은 프로그램도 해제됩니다.

팩토리의 `options`는 생성된 Emscripten 모듈로 전달됩니다. `.wasm` 파일을
사용자 지정 URL에서 제공하려면 `locateFile(path, prefix)`를 사용하고, 이미
로드한 `ArrayBuffer` 또는 `Uint8Array`가 있으면 `wasmBinary`를 사용하십시오.

## 언어, 가상 파일, 입력

초기 언어는 결정적인 `en-US`입니다. `availableLanguages`로 현재 빌드에 포함된 카탈로그를 확인하고 `setLanguage`로 선택합니다. Wasm 모듈 내부 언어 상태는 공유되므로 서로 다른 언어를 동시에 사용해야 하면 Web Worker를 분리하십시오.

`files`는 실제 파일 시스템에 쓰지 않고 한 번의 검사나 실행에 임시 import 모듈을 제공합니다.

```TypeScript
const result = runtime.runSource(
	`import "helper.vs";

int main()
{
	return answer();
}`,
	{
		fileLabel: "main.vs",
		files: [{ path: "helper.vs", source: `export int answer()
{
	return 42;
}` }],
	},
);
```

대화형 입력에는 일반 런타임과 `runSourceInteractive`를 사용하고 EOF에는 `null`을 반환합니다. 한 Wasm 모듈에서 입력을 기다리는 대화형 실행은 하나만 허용됩니다. TypeScript 래퍼는 대기 중인 다른 작업과 폐기를 거부합니다. 원시 C ABI에서는 모듈 전체를 보호하므로 create와 텍스트 getter는 null을 반환하고, 상태를 변경하는 텍스트 작업은 API 오류를, 숫자 getter와 프로그램 작업은 0을 반환합니다. 메모리 작업은 Busy(9), 메모리 결과 getter는 0을 반환하며 lease 해제는 계속 가능합니다. 또한, 폐기는 대화형 호출이 끝날 때까지 무시됩니다. 오래 실행되는 코드는 페이지를 막지 않도록 Worker에서 실행하십시오. [대화형 입력 튜토리얼](../../../../../../../Resources/Docs/Tutorials/ko-KR/toolchain/16-web-interactive-input.md)과 [Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md)를 참조하십시오.

입력 브리지의 메모리 할당 실패나 입력 줄의 Wasm32 크기 한도 초과는 API 실패 결과로 반환됩니다. 모듈은 다음 실행에도 사용할 수 있으며 `null`은 정상 EOF를 나타냅니다.

## 빌드 및 브라우저 경계

게시된 브라우저 산출물은 컴파일된 프로그램을 지원하며 pthread를 사용하지 않으므로 COOP/COEP 헤더가 필요하지
않습니다. TypeScript의 `runtime.supportsThreads`는 `false`입니다.
`void main(string[] args)`와 `int main(string[] args)`를 모두 사용할 수 있으며,
성공한 0이 아닌 `int main`도 `success: true`를 유지하고 `exitCode`로 반환됩니다.
브라우저 기본 환경에서는 `spawn`을 사용할 수 없고
`diag.failed_to_spawn_task_thread`를 반환합니다. 작업을 만들지 않는 channel,
mutex, semaphore, synchronized-value 작업은 한 번의 실행에서 사용할 수 있습니다.

## C++ facade와 C ABI

`FRuntime`을 통한 소스 실행이 성공하면 JSON 결과와 동일한 JIT 통계도 제공되며
`directNativeCalls`도 포함됩니다.

C++의 `VeroScript::WebAssembly::FRuntime`은 언어 선택, 소스 검사와 실행, 가상 파일, 입력 스트림, 백엔드 이름과 JSON 결과 변환을 제공합니다. 현재 C++ facade는 컴파일된 프로그램의 소유권 API를 직접 제공하지 않으므로 재사용 가능한 컴파일에는 TypeScript 래퍼 또는 C ABI를 사용하십시오. TypeScript 래퍼를 사용하지 않는 호스트는 `VeroScriptWebAssemblyC.h`를 사용할 수 있습니다. C ABI에는 `vero_web_compile_source_with_context`, `vero_web_run_program`, `vero_web_run_program_interactive`, `vero_web_destroy_program` 진입점도 있습니다. 프로그램 핸들은 런타임이 소유하며 해제해야 합니다. 백엔드 ID는 `0 = VM`, `1 = Mixed`, `2 = JIT`입니다. 언어 문자열은 성공적인 언어 변경 또는 런타임 파괴까지 유효하고, 사용 가능한 언어 JSON은 다음 사용 가능한 언어 조회 또는 런타임 파괴까지 유효합니다. 그 밖의 반환 JSON은 런타임 소유 텍스트를 반환하는 다음 API 호출 또는 런타임 파괴까지 유지되며 입력 문자열과 배열은 호출 중에만 차용됩니다.

## 관리 메모리

호스트 API: `runtime.memory` / `runtime.memory.setLimits` (bigint). 소유권, 할당량, 뷰 만료와 임대는 [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/memory.md) 문서를 참고하세요.

<a id="managed-memory-contract"></a>

호스트 API는 버퍼, 슬라이스, 복사, 스칼라 읽기/쓰기, 스냅샷 및 명시적 lease를 제공합니다. 크기, 오프셋, 한도, ID, 64비트 정수는 `bigint`를 사용하며 스칼라는 little/big endian을 지원합니다. `fromArray`는 Node.js `Buffer`를 포함한 독립 복사본을 만들고 `toArray`도 복사본을 반환합니다. 사용 후 영역을 해제하세요. lease는 저장소를 고정하고 크기 변경을 막으며 런타임 해제 후에도 반환할 수 있습니다. `withBytes`는 동기 콜백만 허용합니다. 콜백 안에서 해당 lease를 해제하거나 쓰기 가능한 뷰를 보관하거나 Wasm 힙을 늘리지 마세요. 읽기 전용 lease는 복사본을 제공하며 쓰기 가능한 뷰는 힙 증가 후 다음 콜백에서 다시 생성됩니다. 대화형 입력을 기다리는 동안 상태를 가진 메모리 작업은 거부됩니다.

호스트 영역과 스크립트 세션은 별도 한도를 사용하며 각 컨텍스트의 초기 한도는 버퍼당 16 MiB, 총 보유량 32 MiB입니다. `runtime.memory.setLimits`는 호스트 영역 한도만 변경하며 0은 무제한입니다. 이 한도는 관리되는 바이트 저장소만 다루며 전체 Wasm 힙이나 완전한 샌드박스를 다루지 않습니다.

## 코어 호환성과 진단

<a id="compiler-diagnostics"></a>

내장 컴파일러는 파일 간 선언을 준비하고 구조화된 어노테이션을 처리하며, 알 수 없는 어노테이션은 메타데이터로 유지합니다. 브라우저 SDK는 네이티브 어노테이션 프로세서를 등록하지 않습니다. `delegate.vs`를 가져오면 소문자 멤버 이름을 사용하는 `std::delegate`와 `std::multicast_delegate`를 사용할 수 있습니다. 진단은 `error`, `warning`, `info`, `hint` 수준을 보존하며 성공 결과에도 오류가 아닌 진단이 포함될 수 있습니다. 소스 실행은 컴파일 진단을 유지하고 재사용 프로그램은 컴파일 결과에 이를 제공합니다. 바이트코드는 고정 `VSBC` 식별자와 제품 버전 `0.1.1`을 사용합니다. 이전 개발 빌드의 패키지와 캐시는 소스에서 다시 빌드하십시오.

## 대화형 입력 소유권

대화형 호출은 옵션을 읽은 뒤 모듈 상태를 다시 확인합니다. 재진입 요청이나 폐기된 런타임의 요청은 입력 공급자를 인수하기 전에 거부되므로 실행 중인 프로그램의 콜백과 후속 입력이 유지됩니다.

<a id="input-provider-failures"></a>

SDK 입력 제공자의 예외와 거부된 Promise는 현재 실행을 중단하고 원래 오류로 거부합니다. 잘못된 결과 형식이나 NUL이 포함된 행은 `TypeError`로 거부합니다. 실패 시 네이티브 스택을 재개하여 정리하며, 오류를 EOF로 처리한 뒤 스크립트의 정상 실행을 계속하지 않습니다. 런타임과 컴파일된 프로그램은 다시 사용할 수 있습니다. `null`은 계속 정상 EOF를 의미합니다. 표시되지 않은 원시 모듈 제공자는 거부되거나 잘못된 입력에 대한 기존 EOF 처리를 유지합니다.

## 콘솔 출력과 기능

<a id="console-capabilities"></a>

`std::console::write`는 줄바꿈 없이 `output`에 텍스트를 추가하며, `flush`는 표준 출력을 플러시합니다. `std::console::error`는 호스트의 진단 행 채널을 사용하므로, 줄바꿈으로 끝나지 않는 마지막 행에는 `errorOutput`에서 줄바꿈이 추가됩니다. SDK는 터미널 호스트를 제공하지 않습니다. 기능 조회는 `false`, 크기는 0을 반환하고, 유효한 색상·커서·지우기 작업도 이스케이프 시퀀스를 추가하거나 캡처한 텍스트를 삭제하지 않고 `false`를 반환합니다. 비동기 입력 지원은 터미널 기능과 독립적입니다.

잘못된 색상 이름, 지우기 모드, 커서 좌표, 인수 형식 또는 개수는 터미널 기능이 지원되지 않더라도 진단을 생성합니다. 거부 후에도 런타임을 다시 사용할 수 있습니다. 캡처한 텍스트는 실행이 완료될 때 `VeroScriptResult`로 반환됩니다.

## 현재 언어 기능

<a id="compiler-and-application-services"></a>

두 엔진은 소문자 `pair<T, U>` / `std::pair`, `std::attribute<T>`, `std::bind_method`와 내장된 `attribute.vs`, `callable.vs`, `i18n.vs`, `compiler.vs` 모듈을 지원합니다. 애플리케이션의 `std::i18n::context`는 리터럴 텍스트, 매개변수 형식을 보존하는 이름 기반 서식 지정과 독립 스냅샷을 제공합니다. 이 언어 설정은 SDK 진단 언어를 변경하지 않습니다. SDK는 애플리케이션 리소스나 서식 지정 공급자를 제공하지 않습니다.

`import compiler "builder.vs";`는 `checkSource`, `compileSource`, `runSource` 처리 중 `files`에서 스크립트 컴파일 확장을 불러올 수 있습니다. 따라서 검사는 애플리케이션의 `main`을 호출하지 않아도 확장 코드를 실행할 수 있습니다. Core는 기본 확장 한도와 기능 정책을 적용합니다. 프런트엔드가 컴파일을 중단할 수 있어야 한다면 Web Worker를 사용하십시오. 확장 실패는 진단을 반환하며 런타임은 재사용할 수 있습니다.

## 대화형 스택 작업 영역

대화형 입력은 중첩된 바인딩 메서드 호출 안에서도 실행을 일시 중단할 수 있습니다. 깊은 네이티브 재진입은 호스트 호출 스택의 제약을 받습니다. 빠른 엔진에는 실행 전에 입력을 제공해야 합니다.

## 주석 이름 일치 규칙

<a id="annotation-name-matching"></a>

주석 이름은 기본적으로 대소문자를 구분합니다. 스크립트 컴파일 확장은 등록 전에 `annotation_definition.case_sensitive = false`를 설정하여 해당 정의에 로캘과 무관한 ASCII 대소문자 일치 규칙을 적용할 수 있습니다. 프로세서 구독과 `has_annotation`, `find_annotations`, `annotation_info.matches_name` 리플렉션은 이 규칙을 따릅니다. `name()`과 `raw_text()`는 소스 표기를 유지하며 `definition().case_sensitive()`는 해당 규칙을 반환합니다. `@Name` 형식의 주석은 `find_annotations`와 `matches_name` 조회에 `@` 접두사가 필요하고 `has_annotation`에서는 생략할 수 있습니다. 인수 이름, 메타데이터 키, 등록되지 않은 주석은 계속 대소문자를 구분합니다.

## Core 실행과 한도

<a id="numeric-and-library-limits"></a>

Core는 `bigint.vs`, `rational.vs`, `core.vs`를 통해 `std::bigint`와 `std::rational`을 제공합니다. 이 형식은 VM 안에서 정수와 유리수의 정확한 값을 유지하며, 정확한 텍스트로 내보내려면 `to_string()`을 사용합니다. 고정 폭 Wasm ABI 인수에 사용하는 JavaScript `bigint`와는 별개입니다. 중첩 제네릭 형식, 상속된 중첩 형식, 지역 형식은 Core의 범위 및 접근 규칙을 따릅니다.

`runtime.setStandardLibraryLimits(maxTextBytes, maxReadBytes, maxSplitParts)`는 부호 없는 64비트 범위의 `bigint`로 작업별 텍스트 바이트 수, 일괄 읽기 바이트 수, 분할 항목 수를 제한합니다. 0은 호스트 한도를 추가하지 않으며, 생략한 인수는 0으로 재설정됩니다. 변경은 이후 소스 및 대화형 실행과 기존 컴파일 프로그램의 다음 실행에 적용됩니다. 대화형 입력 대기 중에는 변경할 수 없습니다. 이 한도는 관리 메모리 할당량 및 Core 수치 한도와 독립적이며 Wasm 힙 전체를 제한하지 않습니다. Web API는 Core의 유한한 기본 수치 한도를 사용합니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
