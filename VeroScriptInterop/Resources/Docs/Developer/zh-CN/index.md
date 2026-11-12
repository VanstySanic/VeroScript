# VeroScript Interop 技术文档

`VeroScriptInterop` 是供 non-C++ host 使用的 stable C11 process ABI。本文档面向 binding maintainer。ABI header、implementation、test 与 generated consumer binding 是 authoritative；不得发明 C contract 中不存在的 managed-language abstraction。

## ABI 表面

public header 定义 opaque session/value/callable/call-context/buffer/package/interception/generic-host-type/serialization context、string/byte view、status/enum、session option、source overlay、runtime configuration、JIT policy/statistic、native signature、host-type description、mutation event 与 visitor callback。optional session JIT-cache capability 在保持 ABI 0.1.1 与 original 32-byte policy layout 的同时，增加 append-only switch、retained-resource limit、cumulative cache-context statistic。

exported function 覆盖 runtime/version/capability query、buffer、locale catalog、session create/destroy、configuration、source/file loading、execution、value、call、package、host binding、interceptor、mutation、reflection、serialization。exported name、layout、calling convention、enum value、status meaning 属于 ABI，而非 prose。

## ownership 与 view

opaque handle 有明确 create、borrow、retain、destroy rule。view 不是 owner。returned string/byte view 不得超过 owner 生命周期；buffer 应在 owner release 前 copy。callback context 仅在 documented callback call 内 valid。binding 必须显式呈现这些约束，而不是依赖 C ABI 未保证的 garbage collector/reference-counting assumption。

## callback 与 thread

callback result 经 ABI callback status contract 返回。callback exception、failed conversion、invalid user data 必须在不泄露 native resource 的情况下 report。host callback 可由 script worker 进入，host-owned state 因此需要自身 synchronization。creator-thread restriction/callback reentrancy 必须与 core 和每个 language adapter 对齐。

## 版本管理

`vs_interop_get_abi_version()` 报告 ABI contract。runtime semantic version、capability bit、package metadata、ABI version 是不同值。不得把 pre-release numeric marker 放进 user-facing version prose，也不得将 ABI number 作为 product version。additive field/function 需要 compatibility decision；layout/enum change 需要 ABI review 与 consumer update。

## 本地化

locale function 选择并 enumerate canonical BCP 47 catalog directory。locale discovery 不得假定 fixed language count。catalog key、exported function name、status code、ABI value、diagnostic ID、version string 永不 translate。

## session、value 与 callback 细节

session API 区分 owner-thread operation、idle-session operation、run 时允许的 operation。loading、clearing、reflection、package management、configuration、binding change 在 precondition 不满足时会 reject。 `vs_interop_session_copy_last_error()` 是 operation-level error fallback；structured diagnostic 通过 count、argument、formatted-diagnostic API 读取。

value surface 包括 scalar、string、enum、host object、array、map、ordered map、set、queue、stack、deque、channel。enumeration callback 接收 borrowed view。channel capacity 在 first use 前配置；close 会 wake blocked operation，send/receive failure 返回 status 和 optional owned UTF-8 error buffer。nested capability transfer/invalid type conversion 是 engine error，而非 successful coercion。

callback context helper 是 native host function、generic type factory、instance interceptor、external serialization/deserialization callback 返回 error 的 supported path。除非 implementation 明确报告 engine error，否则 callback 不得 reentrant destroy/clear owning session。callback user data、host-object payload、managed root 必须覆盖每个可能的 callback entry。

## 详细页面

- [Interop ABI 与生命周期](abi-and-lifetime.md)
- [Interop value、callback 与 package](values-and-callbacks.md)

## 事实来源

以 `Public/VeroScriptInterop.h`、private implementation/test、CMake package configuration 和消费 ABI 的 C#/Python adapter 为准。ownership、layout、callback、threading、status、locale、capability 或 versioning 变更时同步更新本文档。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
