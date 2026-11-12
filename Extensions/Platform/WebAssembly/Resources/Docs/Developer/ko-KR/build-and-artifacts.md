# WebAssembly build와 artifact 계약

## Emscripten 모듈

WebAssembly CMake target은 `VeroScriptWebAssembly`를 core에 link하고 shared module entry point에서 interactive input 지원 `veroscript-engine`, 미지원 `veroscript-engine-fast` 두 executable을 만듭니다. 둘 다 `stdlib/`, `localization/`을 virtual filesystem에 embed합니다.

generated module은 allocation, WebAssembly ABI version/thread capability query, create/destroy, language selection, catalog discovery, source run/check, source option, interactive-context entry point를 export합니다. export list는 generated SDK contract 일부입니다.

## SDK 소스 생성

build는 TypeScript source/declaration을 WebAssembly output directory로 copy합니다. native public header, generated TypeScript declaration, parity test는 일치해야 합니다. mismatch를 숨기려고 generated file을 hand-edit하면 안 됩니다.

저장소 루트에서 다음 명령으로 SPDX 선언과 Tab 들여쓰기를 포함한 형식 선언을 다시 생성합니다. 그런 다음 `npm --prefix Extensions/Platform/WebAssembly/VeroScriptWebAssembly run typecheck`로 일치 여부를 확인합니다.

```sh
npm --prefix Extensions/Platform/WebAssembly/VeroScriptWebAssembly run generate:declarations
```

## 릴리스 검사

artifact audit는 complete interactive/fast module set, generated declaration, SDK source를 요구합니다. browser smoke test는 headless execution, interactive input, event-loop behavior, Web Worker execution, worker termination을 검사합니다. size budget은 release review limit이지 ABI limit이 아닙니다.

## export와 declaration parity

generated Emscripten export list에는 thread-capability query, language/catalog query, source option/context call, compiled-program create/execute/destroy, interactive program execution, context checking이 있어야 합니다. TypeScript binding은 additive compiled-program export를 feature-detect하여 해당 capability 이전 artifact도 unsupported operation을 만들지 않고 사용하게 합니다.

browser test는 borrowed virtual-file array, program-handle reuse/cleanup, program run별 fresh state, program suspended 중 interactive input, language discovery/selection, result-pointer lifetime, thread-capability branch를 다룹니다. 이 check는 language/bytecode version contract를 바꾸지 않습니다.

## 대화형 스택 작업 영역

대화형 엔진은 실행을 일시 중단할 때 Wasm 힙에서 1 MiB Asyncify 저장 스택을 할당하고 실행 상태를 복원한 뒤 해제합니다. 이를 통해 중첩된 바인딩 메서드 호출 안에서 입력을 기다릴 수 있습니다. 깊은 네이티브 재진입은 호스트 호출 스택 용량의 제한도 받습니다. 빠른 엔진은 Asyncify 작업 영역을 할당하지 않습니다.

이 저장 스택은 16 MiB C/C++ 스택과 별개의 영역이며 바인딩 메서드가 VM에 재진입할 때 네이티브 프레임을 보관합니다. 릴리스 검증은 Node와 Chromium에서 소스 실행과 컴파일된 프로그램, `vm`과 `mixed` 각각에 대해 32단계 호출 체인의 일시 중단과 복원을 두 번 수행합니다. 또한 128단계 네이티브 재진입이 입력 요청 전에 `diag.call_stack_overflow`로 거부되고, 같은 프로그램을 더 얕은 중첩으로 다시 실행할 수 있는지 확인합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/build-and-artifacts.md) · [fr-FR](../fr-FR/build-and-artifacts.md) · [ja-JP](../ja-JP/build-and-artifacts.md) · [ko-KR](build-and-artifacts.md) · [zh-CN](../zh-CN/build-and-artifacts.md) · [zh-TW](../zh-TW/build-and-artifacts.md)
