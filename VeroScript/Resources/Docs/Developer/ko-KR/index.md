# VeroScript 코어 기술 문서

[교체 가능한 VM 백엔드](vm-backends.md)

이 directory는 core maintainer와 module author를 위한 language core/native implementation 기술 문서입니다. language tutorial이나 runtime user manual이 아니며 public header/test가 supported contract의 기준입니다.

## pipeline

core는 frontend, AST, diagnostic, module loader, preprocessor, semantic compiler, bytecode, VM, code generation, JIT, package, reflection, host binding, external serialization, concurrency, debug, hot reload, locale selection을 소유합니다. source/overlay/import/package resolve, tokenize/preprocess, public AST parse, semantic/type/overload/generic/ownership/visibility check, bytecode/debug metadata emit, module/package link, VM/JIT execution 순서입니다. failure는 structured diagnostic/source location을 보존하며 다른 program이나 partial artifact를 만들지 않습니다. `VeroScriptModuleLoader`의 import path, implicit import, stdlib root, overlay, `-D`, predefined macro, host type name, circular/project-wide policy는 explicit input이고 normalization이 source fingerprint에 들어갑니다. stdlib root는 인접 `localization/` tree를 merge할 수 있습니다.

## 런타임 불변 조건

`VeroScriptVm`, `FValue`, object/container handle, function/host value, reference는 하나의 ownership model입니다. destruction, reference, mutation event, external serialization, retained callable은 identity/lifetime을 보존합니다. container mutation callback은 완료된 mutation을 보고하며 exception은 rollback하지 않습니다. script global/class static field와 task registry/task ID는 worker-local이고 channel은 explicit synchronized capability이며 session/callable creator-thread restriction은 embedding contract입니다.

## 실행 백엔드

[코어 아키텍처와 컴파일 파이프라인 — 경계와 의미론](architecture-and-pipeline.md#core-vm-ownership)을 참조하세요.

## 공개 및 내부 구현 경계

`Public/`는 supported native include surface이고 `Private/`에는 compiler, parser, VM, JIT, implementation detail이 있습니다. 외부 consumer와 language adapter는 public header를 사용합니다. source tree 내부의 소수 embedding/tooling/AOT implementation target만 CMake에서 private include path를 명시적으로 받습니다. 이는 review 대상 friend dependency이며 installed/supported API가 아니고 core owner review 없이 늘리지 않습니다.

native C++ SDK는 build-matched static-library contract입니다. consumer는 같은 header, library, architecture, runtime, build identity로 다시 빌드해야 하며 build 간 C++ binary-layout compatibility는 보장하지 않습니다. stable process ABI는 C11 `VeroScriptInterop` surface뿐입니다. public C++ layout, enum value, bytecode format, host-binding record, diagnostic ID, package metadata 변경에는 계속 compatibility review가 필요합니다.

## localization과 asset

core는 shared localization asset root에서 runtime/tooling catalog를 canonical BCP 47 directory name으로 load합니다. locale discovery는 directory/catalog 기반이며 locale 수는 compile-time constant가 아닙니다. source identifier, diagnostic ID, JSON key, API name, path, version, code sample은 번역하지 않는 technical token입니다.

## 세션, 패키지와 호스트 바인딩

session surface는 source/file/package load, compile without commit, compiled module commit, run, clear, reload를 구분합니다. file-backed reload는 source path를 유지하고 source overlay는 reload path를 만들지 않습니다. package는 application/library shape를 validate하며 hot reload는 opt-in이고 explicit reloadable state만 보존합니다. host binding catalog는 native function, host type, generic template/instantiation, member, operator, clone callback, inheritable type, scoped enum, subclass factory를 validate합니다. reflection은 private compiler structure를 노출하지 않고 loaded metadata를 enumerate합니다.

## 상세 페이지

- [코어 아키텍처와 컴파일 파이프라인](architecture-and-pipeline.md)
- [코어 실행, 상태, package](execution-and-state.md)
- [코어 JIT와 동시성](jit-and-concurrency.md)
- [코어 빌드와 런타임 asset](build-and-assets.md)

## execution governance와 JIT observability

`FExecutionLimits`, `FExecutionControl`, `FScriptCapabilities`가 cooperative governance를 제공합니다. instruction/wall-clock zero는 unlimited이며 `RequestCancel`은 shared control을 mark하지만 active native/managed callback을 강제 종료하지 않습니다. capability policy의 file/environment/console/task restriction은 VM이 강제하고 JIT는 우회하지 않습니다. requested backend는 `Vm`, `Mixed`, `Jit`이고 debugger/retained initialization/cleanup/interpreter enforcement가 effective VM path를 선택합니다. `FJitStats`는 compilation, cache, dispatch, fallback/failure, budget, entry-kind, fast path를 구분하며 unsupported compilation은 execution failure가 아니면 정상 fallback입니다.

## 컴파일러 모델 갱신

parser/compiler는 static type modifier와 static data member를 module-owned backing global로 보존합니다. static access는 declaring type hierarchy/interface로 해석되고 generic instantiation은 substituted static storage를 갖습니다. AST는 lambda capture, generic parameter/bound, alias, annotation, source range를 유지하며 lowering은 identity/location을 보존해야 합니다. unsupported capture/generic form은 명시적으로 failure합니다.

## 명세의 기준 소스

`Public/`, 책임 `Private/` tree, core CMake target, focused runtime/compiler/VM/JIT test, deployed stdlib/localization asset이 기준입니다. semantic behavior, execution policy, ownership, public ABI, bytecode/package format, diagnostic, runtime asset loading의 변경과 문서를 함께 갱신합니다.

- [지역화 리소스와 배포](localization-and-resources.md)

- [관리 메모리 구현 계약](memory-runtime.md)

- [컴파일 처리와 소스 메타데이터](compile-processing.md)
- [스크립트 컴파일 확장](script-compile-extensions.md)

- [응용 프로그램 국제화 실행 계약](i18n-runtime.md)

- [모듈 환경과 네이티브 확장](module-environments.md)

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
