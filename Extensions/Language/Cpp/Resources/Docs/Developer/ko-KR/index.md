# VeroScript C++ 기술 문서

이 directory는 public header, CMake target, test, generated artifact에 source-bound된 C++ 개발자 문서입니다. user guide가 아니며 API name, command-line option, JSON key, diagnostic, code example을 번역한 표기로 다시 쓰면 안 됩니다.

## 모듈 소유권

C++ group은 다섯 module로 구성됩니다.

- `VeroScriptCpp`는 language core 위 high-level C++ embedding 및 session surface입니다.
- `VeroScriptCppAot`는 compile-checked VeroScript program을 C++ source bundle로 emit합니다. bundle은 template instantiation, internal linkage, initialization order를 명시적으로 유지하기 위해 하나의 translation unit을 사용합니다.
- `VeroScriptCppFrontend`는 source tool이 소비하는 stable AST로 supported C++17 subset을 parse합니다.
- `VeroScriptCppBindGen`은 표현 가능한 C++ declaration을 Binding Catalog로 변환합니다. catalog는 metadata이며 native invocation thunk가 아닙니다.
- `VeroScriptCppTranspiler`는 semantics-preserving C++17 subset을 VeroScript source로 lower하고 real compiler로 generated output을 validate합니다.

language-neutral core는 `VeroScript/`입니다. C++ module은 core에 의존할 수 있지만 core는 C++ tooling에 의존하면 안 됩니다. frontend는 BindGen과 Transpiler가 공유하며 두 consumer는 parse 뒤 서로 다른 semantic policy를 적용합니다.

## semantic 경계

C++ tool은 general C++ compiler인 척하지 않고 bounded subset을 의도적으로 수용합니다. 지원하지 않는 ownership, linkage, pointer, template, preprocessor, control-flow construct는 diagnostic을 만들어야 합니다. 이러한 construct를 silently approximate하는 변경은 ABI 또는 source correctness regression입니다.

frontend는 AST가 표현할 수 있을 때 qualified name, alias, attribute, declaration range, constructor, destructor, friend declaration, type-parameter template, 같은 file의 out-of-class definition을 보존합니다. BindGen은 VeroScript에 equivalent source construct가 없어 Transpiler가 거부해야 하는 structured metadata를 유지할 수 있습니다. 이는 별도 policy이므로 편의상 합치면 안 됩니다.

## output과 transaction 규칙

multi-input tool은 output을 replace하기 전에 complete input set을 parse/merge합니다. catalog, API reference, source map, manifest는 하나의 logical result로 atomic replace되어야 합니다. duplicate basename은 deterministic path-derived disambiguation을 받습니다. parse, merge, validation, final VeroScript compilation 실패는 partial output tree를 남기면 안 됩니다.

generated API reference는 documentation artifact입니다. implementation source로 compile해서는 안 되며 JSON Binding Catalog는 compiler/IDE tooling의 machine-readable input으로 남습니다.

## concurrency와 host 계약

embedding/AOT host는 callback state를 complete run 동안 살아 있게 두고 worker thread가 도달할 수 있는 state를 synchronize해야 합니다. script global과 class static field는 native worker별로 분리됩니다. `channel<T>`는 explicit synchronized cross-worker capability입니다. native callback, interceptor, mutation observer, reachable AOT host symbol은 concurrently run할 수 있습니다.

## engine과 session 계약

`VeroScriptEngine`은 stateless `CheckSource`, `CheckFile`, `RunSource`, `RunFile` entry point를 제공합니다. file check/run은 bytecode cache를 사용할 수 있으며 source label, input/output/error stream, circular-import policy, project-wide-access policy는 explicit input입니다.

`VeroScriptSession`은 stateful surface입니다. configuration, input stream, native/host registration, loading, compiling, committing, reloading, clearing, execution-changing operation은 run 중에 거부됩니다. `LoadFile`은 file-backed reload path를 설정하고 source-only module은 path로 reload할 수 없습니다. `Reload`, `ReloadCompiledModule`, `ReloadPackage`는 replacement를 stage/validate한 뒤 commit합니다. package kind와 entry-point shape는 일치해야 하며 reload package는 loaded module kind와도 일치해야 합니다.

`Run(ERunMode::Eval)`은 `main`을 호출하지 않고 모듈을 초기화합니다. 호출 사이의 전역 상태는 `SetHotReloadEnabled(true)`를 설정했을 때만 유지됩니다. `Clear()`는 loaded state, last-invocation JIT statistic, reusable session JIT code를 제거합니다. `SetJitPolicy`는 subsequent execution에 적용됩니다. 일반 `CallFunction` invocation은 fresh VM 사이에서도 compiled code를 reuse할 수 있고 `SetJitCacheLimits`, `GetJitCacheStats`는 current generation/configuration의 bounded retained resource와 cumulative compilation을 보여줍니다. host call, instance interceptor, mutation callback, field/global access, object construction, callable retention/invocation, external serialization, reflection은 각각 documented idle/lifetime rule을 지키는 별도 session operation입니다.

## public C++ AOT와 tool output

AOT API는 generated `.cpp` string 하나 또는 relative source file과 manifest를 포함하는 `FCppAotSourceBundle`을 반환합니다. BindGen은 merged catalog, optional API reference, project manifest를 반환합니다. Transpiler는 generated source와 source map을 반환하고 project mode는 input별 file record와 manifest도 반환합니다. shared output writer는 atomic입니다. parse, merge, lowering, validation, path collision 실패 뒤 output file을 replace하지 않습니다.

frontend AST는 source range, declaration attribute, alias, template, C linkage, storage/semantic qualifier, body, out-of-class definition을 기록합니다. `FTypeMapOptions`는 raw host pointer, rvalue reference, known host type, known enum type, known type parameter, known alias를 explicit하게 제어합니다.

## 상세 페이지

- [C++ 그룹 아키텍처](architecture.md)
- [C++ embedding과 session](session.md)
- [C++ frontend, BindGen, Transpiler](frontend-and-tools.md)
- [C++ AOT emitter](aot.md)
- [C++ build/test 계약](build-and-tests.md)

## execution governance와 session observability

`VeroScriptSession`은 requested `EExecutionBackend` 외에 `SetExecutionLimits`, `SetExecutionControl`, `RequestCancel`, `SetCapabilities`를 노출합니다. limit field의 zero는 unlimited이고 cancellation은 cooperative이며 capability restriction은 VM path에서 enforce되어 JIT code가 bypass할 수 없습니다. `SetJitPolicy`와 `GetLastJitStats`는 statistic을 version value로 만들지 않으면서 adaptive compilation contract를 노출합니다.

session call surface에는 free-function/instance call, constructor invocation, field/global access, callable retain/invocation, reflection enumeration, external serialization, `CallMethodBypassingInterceptor`가 포함됩니다. 이 bypass는 선택된 entry에만 적용됩니다. body가 만드는 nested call은 여전히 interceptor dispatch를 따릅니다. `RequestCancel`은 run 중에도 발행할 수 있지만 host callback state synchronization은 host의 책임입니다.

## 명세의 기준 소스

`Public/` 아래 public header, 각 module의 `Private*` implementation, module CMake file, focused test가 source of truth입니다. public contract, output format, supported-subset rule, ownership rule, concurrency guarantee 변경은 같은 change에서 이 문서를 갱신해야 합니다. identifier, path, target name, diagnostic, version value는 source 표기를 그대로 복사하며 localize하지 않습니다.

## Session JIT 재사용

Session 코드 재사용 조건, 제어된 실행, 보존 제한과 집계는 [C++ Session](session.md#session-jit-cache)에서 관리합니다.

## 보관 바이트 할당량

`MaxNativeBytes` 또는 `MaxRetainedPayloadBytes`의 0은 해당 바이트 보관을 허용하지 않습니다. 무제한을 뜻하는 정책 예산 0과 다릅니다.

## 취소 및 호스트 전달

[C++ embedding과 session — 취소 및 호스트 전달](session.md#session-control-and-streams)을 참조하세요.

같은 `FExecutionControl` 토큰을 공유하는 Session은 취소 상태도 공유합니다. `Reset()`과 재사용 전에 이전 취소 호출이 끝날 때까지 기다려, 오래된 요청이 토큰을 재사용하는 다음 실행을 취소하지 않도록 합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
