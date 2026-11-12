# VeroScript WebAssembly 기술 문서

한 줄 표기도 정확히 유지합니다: `void main(string[] args)`, `int main(string[] args)`.
이 directory는 UI-free WebAssembly SDK를 문서화합니다. `VeroScriptWebAssembly.h`, `VeroScriptWebAssemblyC.h`, TypeScript binding, Emscripten build, browser test에 bound되며 editor, HTML application, CSS framework, frontend product는 다루지 않습니다.

## 공개 인터페이스

WebAssembly module은 implementation-facing C++ wrapper `VeroScript::WebAssembly::FRuntime`, opaque runtime handle/result/virtual-file/language/catalog/run WebAssembly C ABI, browser-facing generated TypeScript module/declaration 세 surface가 있습니다. TypeScript SDK는 C/C++ contract와 parity를 지켜야 합니다.

runtime은 language core, C++ host API, shared standard-library/localization asset에 의존하며 두 번째 language/bytecode version contract를 도입하면 안 됩니다.

## 실행 정책

[WebAssembly ABI, artifact, release 계약 — 프로그램과 입력 기능](release-contract.md#web-execution-policy)을 참조하세요.

published baseline은 script-level worker thread를 advertise하지 않습니다. unbounded script는 owning Web Worker를 terminate해 stop합니다. interactive input은 provider wait 중 runtime을 suspend하며 Wasm module당 active interactive run은 하나입니다.

## artifact와 binding rule

standard library/localization catalog는 generated virtual filesystem에 embed됩니다. release artifact set, generated declaration, binding parity check는 하나의 contract입니다. `VeroScriptWebAssemblyC.h`, TypeScript binding, result field, backend name, language tag, generated file name 변경은 parity/browser test도 갱신해야 합니다.

language selection은 embedded catalog에서 discovered canonical BCP 47 tag를 사용합니다. language count를 hard-code하지 않습니다. catalog key, API identifier, backend name, diagnostic, version value는 translate하지 않습니다.

## runtime API 세부 사항

`createVeroScriptRuntime()`는 interactive input용 Asyncify runtime이고 `createFastVeroScriptRuntime()`는 upfront input의 low-latency factory입니다. `runSource`는 creation 뒤 synchronous, `runSourceInteractive`는 promise를 반환하고 provider wait 중 VM/script state를 suspend합니다. active interactive run은 하나만 허용됩니다.

runSource는 file label, source overlay, standard input, script argument, backend를 받습니다. `void main(string[]
args)`/`int main(string[]
args)`를 support하며 non-zero result도 `exitCode` success입니다. embedded NUL은 reject합니다. Spawn은 browser baseline 밖이라 worker attempt는 structured diagnostic을 반환합니다.

`supportsCompiledPrograms` artifact에서 `compileSource`는 source graph를 한 번 compile하고 owned `VeroScriptProgram`을 반환합니다. repeated `run`은 program을 reuse하며 fresh state이고 `runInteractive`는 Asyncify artifact만 제공합니다. program은 runtime 전에 dispose하며 runtime disposal은 remaining handle을 release합니다. compiled-program은 artifact capability이지 language/bytecode version이 아닙니다.

## 상세 페이지

- [WebAssembly runtime과 binding](runtime-and-bindings.md)
- [WebAssembly ABI, artifact, release 계약](release-contract.md)
- [WebAssembly build와 artifact 계약](build-and-artifacts.md)

## C ABI 런타임과 프로그램 핸들

C runtime handle은 one caller at a time입니다. result-producing call pointer는 next replacement operation 또는 runtime destruction까지 runtime-owned입니다. Asyncify input wait에는 stateful getter/text operation이 unavailable이며 host는 interactive call completion 뒤 retry합니다.

vero_web_supports_threads는 pthread-backed script concurrency artifact 여부를 보고합니다. default browser artifact는 concurrent program에 independent Web Worker를 씁니다. 이 capability는 supportsInteractiveInput/supportsCompiledPrograms SDK flag와 별개입니다.

context entry point는 borrowed virtual-file path/source pair를 받고 compile은 runtime-owned program handle을 만듭니다. run_program/run_program_interactive는 compiled module을 fresh state로 reuse합니다. deterministic cleanup이 필요하면 runtime discard 전에 vero_web_destroy_program을 호출합니다.

## 명세의 기준 소스

public WebAssembly header, `Bindings/`, native WebAssembly implementation, Emscripten build script, artifact audit, declaration parity, smoke/browser test가 근거입니다. WebAssembly ABI, generated artifact, execution policy, input-provider, cancellation, asset-packaging 변경과 문서를 같이 갱신합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
