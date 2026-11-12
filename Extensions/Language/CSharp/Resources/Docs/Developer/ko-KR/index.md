# VeroScript C# 기술 문서

이 directory는 C# source, Interop header, CMake target, managed test에 연결된 개발자 문서이며 user tutorial/XML API 중복이 아닙니다.

## 모듈 책임

`VeroScriptCSharp`는 `VeroScriptInterop` 위 NativeAOT-compatible .NET 8/9 runtime surface, `VeroScriptCSharpAot`는 emitter, `VeroScriptCSharpBindGen`은 catalog/generation, `VeroScriptCSharpFrontend`는 shared semantic model을 제공합니다. adapter는 stable C ABI를 사용하며 C++ private header/compiler-runtime 재구현을 포함하지 않습니다.

## ABI와 lifetime rule

Interop handle은 opaque ownership object입니다. lease, buffer, package, callable, callback registration, session은 `VeroScriptInterop.h`와 wrapper lifetime rule을 따릅니다. pointer/borrowed string/callback context를 수명 밖으로 노출하지 않고 wrapper는 encoding/buffer/status/diagnostic/exception 의미를 보존합니다.

## frontend와 generator boundary

frontend는 user code를 실행하지 않고 declaration을 parse합니다. BindGen은 metadata를 emit하며 native invocation thunk를 만들지 않습니다. unsupported signature/ownership/default/inheritance는 output replacement 전 source diagnostic으로 실패하고 AOT는 별 lowering path입니다.

## 호환성 규칙

checked API contract/package verification은 public type, marshaling layout, enum, callback convention, package metadata, target framework을 보호합니다. version, ABI ID, JSON key, CLI option, C# identifier는 번역하지 않는 token입니다.

## 관리 런타임 세부 사항

`VeroScriptRuntime`은 process compatibility gate이고 `EnsureCompatible()`가 session 전에 필요합니다. `VeroScriptSession`은 handle/root owner, `VeroScriptValue` borrowed view는 scope 한정, `VeroScriptCallable` thread/dispose rule은 lifetime-only, `VeroScriptPackage`는 metadata/signed byte/module state를 소유합니다.

## binding과 generated-code detail

binding은 documentation, annotation, type/parameter metadata, enum type, const/mutable flag, operator, clone/equality/hash, subclass factory, generic-host callback을 metadata/callback으로 보존하며 native function-pointer table이 아닙니다.

## 상세 페이지

- [C# runtime과 Interop adapter](runtime-and-interop.md)
- [C# binding, package, 호환성](bindings-and-packages.md)
- [C# toolchain과 package 계약](toolchain-and-packaging.md)

## 관리 실행 계약

`VeroScriptConfiguration`은 overlay/import/cache, depth, `MaxInstructions`, `TimeoutMilliseconds`, execution control, capability policy, `VeroScriptExecutionBackend`를 갖고 zero는 unlimited입니다. `RequestCancellation`은 non-owner thread에서 설계된 session operation입니다. `VeroScriptJitPolicy`, `GetLastJitStats`, `SetJitCacheLimits`, `GetJitCacheStats`는 `SessionJitCache`가 있을 때 bounded reuse/context를 노출합니다. 디버거가 연결되거나 유지할 상태를 초기화할 때는 VM을 사용합니다. 지원되는 호출은 같은 세대의 코드를 재사용할 수 있습니다. 실행 제어가 활성화된 `Mixed`는 기본적으로 VM을 사용하며 대상 부하를 측정한 뒤 `VeroScriptJitPolicy.EnableControlledMixed`로 제어형 JIT를 활성화합니다. 명시적인 `Jit` 선택은 이 스위치와 독립적이며 미지원 또는 VM이 선호되는 진입점은 제한을 유지하는 VM으로 fallback합니다.

## 명세의 기준 소스

`VeroScriptCSharp/`, frontend/BindGen project, Interop public header, CMake, managed test가 기준이며 ABI/ownership/threading/generated code/package 변경과 문서를 함께 갱신합니다.

`VeroScriptCSharp`: [Android 및 iOS / MAUI](../../User/ko-KR/mobile.md).

모바일 패키징도 같은 프로젝트와 패키지 이름을 사용합니다. `VeroScriptIncludeMobile=true`는 .NET 8/9 및 .NET 10 Android/iOS 대상과 검증된 네이티브 SDK를 포함합니다.


[Android/iOS의 CSharp](mobile.md)

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
