# VeroScript Interop 技術文件

`VeroScriptInterop` 是供 non-C++ host 使用的 stable C11 process ABI。本文檔供 binding maintainer 使用。ABI header、implementation、test、generated consumer binding 是 authoritative；不可建立 C contract 裡沒有的 managed-language abstraction。

## ABI 公開介面

public header 定義 opaque session/value/callable/call-context/buffer/package/interception/generic-host-type/serialization context、string/byte view、status/enum、session option、source overlay、runtime configuration、JIT policy/statistic、native signature、host-type description、mutation event、visitor callback。optional session JIT-cache capability 在保留 ABI 0.1.1/original 32-byte policy layout 時新增 append-only switch、retained-resource limit、cumulative cache-context statistic。

exported function 涵蓋 runtime/version/capability query、buffer、locale catalog、session create/destroy、configuration、source/file loading、execution、value、call、package、host binding、interceptor、mutation、reflection、serialization。exported name、layout、calling convention、enum value、status meaning 是 ABI，不是 prose。

## ownership 與 view

opaque handle 有明確 create、borrow、retain、destroy rule。view 不是 owner。returned string/byte view 不得存活超過 owner；buffer 應在 owner release 前 copy。callback context 只在 documented callback call 內 valid。binding 必須顯示這些約束，不能依賴 C ABI 未保證的 garbage collector/reference-counting assumption。

## callback 與 thread

callback result 經 ABI callback status contract 回傳。callback exception、failed conversion、invalid user data 必須無 native resource leak 地 report。host callback 可能由 script worker 進入，因此 host-owned state 要自己 synchronization。creator-thread restriction/callback reentrancy 必須與 core 與各 language adapter 對齊。

## 版本管理

`vs_interop_get_abi_version()` 回報 ABI contract。runtime semantic version、capability bit、package metadata、ABI version 是不同值。不可把 pre-release numeric marker 放進 user-facing version prose，也不可把 ABI number 視為 product version。additive field/function 需要 compatibility decision；layout/enum change 需要 ABI review/consumer update。

## 在地化

locale function select/enumerate canonical BCP 47 catalog directory。locale discovery 不可假設 fixed language count。catalog key、exported function name、status code、ABI value、diagnostic ID、version string 永不 translate。

## session、value、callback 細節

session API 區分 owner-thread operation、idle-session operation、run 中允許 operation。loading、clearing、reflection、package management、configuration、binding change 在 precondition 不符合時 reject。 `vs_interop_session_copy_last_error()` 是 operation-level error fallback；structured diagnostic 透過 count、argument、formatted-diagnostic API 讀取。

value surface 包含 scalar、string、enum、host object、array、map、ordered map、set、queue、stack、deque、channel。enumeration callback 接收 borrowed view。channel capacity 在 first use 前設定；close 會 wake blocked operation，send/receive failure 回傳 status 和 optional owned UTF-8 error buffer。nested capability transfer/invalid type conversion 是 engine error，不是 successful coercion。

callback context helper 是 native host function、generic type factory、instance interceptor、external serialization/deserialization callback 回傳 error 的 supported path。除非 implementation 明確報 engine error，callback 不可 reentrant destroy/clear owning session。callback user data、host-object payload、managed root 必須存活至每一可能 callback entry 之後。

## 詳細頁面

- [Interop ABI 與生命週期](abi-and-lifetime.md)
- [Interop value、callback、package](values-and-callbacks.md)

## 規格依據

以 `Public/VeroScriptInterop.h`、private implementation/test、CMake package configuration、消費 ABI 的 C#/Python adapter 為準。ownership、layout、callback、threading、status、locale、capability、versioning 變更時，同步更新本文檔。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
