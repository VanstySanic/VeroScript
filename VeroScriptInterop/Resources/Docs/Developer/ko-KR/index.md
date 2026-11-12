# VeroScript Interop 기술 문서

`VeroScriptInterop`는 non-C++ host용 stable C11 process ABI입니다. 이 문서는 binding maintainer용입니다. ABI header, implementation, test, generated consumer binding이 authoritative하며 C contract에 없는 managed-language abstraction을 만들면 안 됩니다.

## ABI 공개 인터페이스

public header는 opaque session/value/callable/call-context/buffer/package/interception/generic-host-type/serialization context, string/byte view, status/enum, session option, source overlay, runtime configuration, JIT policy/statistic, native signature, host-type description, mutation event, visitor callback을 정의합니다. optional session JIT-cache capability는 ABI 0.1.1/original 32-byte policy layout을 유지하면서 append-only switch, retained-resource limit, cumulative cache-context statistic을 추가합니다.

exported function은 runtime/version/capability query, buffer, locale catalog, session create/destroy, configuration, source/file loading, execution, value, call, package, host binding, interceptor, mutation, reflection, serialization을 다룹니다. exported name, layout, calling convention, enum value, status meaning은 prose가 아니라 ABI입니다.

## ownership과 view

opaque handle에는 create, borrow, retain, destroy rule이 있습니다. view는 owner가 아닙니다. returned string/byte view는 owner보다 오래 살지 않고 buffer는 owner release 전에 copy합니다. callback context는 documented callback call 동안만 valid합니다. binding은 C ABI가 보장하지 않는 garbage collector/reference-counting assumption 대신 이런 제약을 보이게 해야 합니다.

## callback과 thread

callback result는 ABI callback status contract로 반환합니다. callback exception, failed conversion, invalid user data는 native resource 누출 없이 report합니다. host callback은 script worker에서 들어올 수 있어 host-owned state에 자체 synchronization이 필요합니다. creator-thread restriction/callback reentrancy는 core 및 각 language adapter와 맞춰야 합니다.

## 버전 관리

`vs_interop_get_abi_version()`은 ABI contract를 보고합니다. runtime semantic version, capability bit, package metadata, ABI version은 서로 다릅니다. user-facing version prose에 pre-release numeric marker를 넣거나 ABI number를 product version처럼 다루면 안 됩니다. additive field/function은 compatibility decision, layout/enum change는 ABI review/consumer update를 요구합니다.

## 현지화

locale function은 canonical BCP 47 catalog directory를 select/enumerate합니다. locale discovery는 fixed language count를 가정하지 않습니다. catalog key, exported function name, status code, ABI value, diagnostic ID, version string은 translate하지 않습니다.

## session, value, callback 세부 사항

session API는 owner-thread operation, idle-session operation, run 중 허용 operation을 구별합니다. loading, clearing, reflection, package management, configuration, binding change는 precondition이 맞지 않으면 reject합니다. `vs_interop_session_copy_last_error()`는 operation-level error fallback이고 structured diagnostic은 count, argument, formatted-diagnostic API로 읽습니다.

value surface에는 scalar, string, enum, host object, array, map, ordered map, set, queue, stack, deque, channel이 있습니다. enumeration callback은 borrowed view를 받습니다. channel capacity는 first use 전에 설정하며 close는 blocked operation을 wake하고 send/receive failure는 optional owned UTF-8 error buffer와 status를 반환합니다. nested capability transfer/invalid type conversion은 successful coercion이 아니라 engine error입니다.

callback context helper는 native host function, generic type factory, instance interceptor, external serialization/deserialization callback에서 error를 반환하는 supported path입니다. implementation이 engine error로 명시하지 않는 한 callback은 owning session을 reentrant destroy/clear하면 안 됩니다. callback user data, host-object payload, managed root는 가능한 모든 callback entry보다 오래 살아야 합니다.

## 상세 페이지

- [Interop ABI와 lifetime](abi-and-lifetime.md)
- [Interop value, callback, package](values-and-callbacks.md)

## 명세의 기준 소스

`Public/VeroScriptInterop.h`, private implementation/test, CMake package configuration, ABI를 소비하는 C#/Python adapter가 근거입니다. ownership, layout, callback, threading, status, locale, capability, versioning 변경과 문서를 함께 갱신합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
